// watcher_tray.cpp —— 带系统托盘的文件监控守护进程
//
// 功能改进相比 watcher.cpp：
//   1) 默认无控制台窗口，最小化到右下角托盘
//   2) 托盘图标右键菜单：显示日志窗口 / 重载配置 / 退出
//   3) 双击托盘图标弹出日志窗口（实时显示事件）
//   4) 命令行也支持多目录：watcher_tray.exe -d "C:\A" -d "D:\B" 或 -c config.txt
//
// 编译：作为 Windows GUI 程序（/SUBSYSTEM:WINDOWS）
//   cl /std:c++17 /EHsc /DUNICODE /D_UNICODE /O2 watcher_tray.cpp
//       /Fe:watcher_tray.exe /link User32.lib Shell32.lib /SUBSYSTEM:WINDOWS

#include "../common/shared.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>     // CoInitializeEx / CoCreateInstance
#include <shlobj.h>      // SHGetFolderPathW / IShellLink / IPersistFile
#include <stdio.h>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <deque>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace QuickJumpFolders;

// ============ 全局状态 ============
constexpr UINT WM_TRAYICON       = WM_USER + 100;
constexpr UINT IDM_TRAY_SHOW     = 1001;
constexpr UINT IDM_TRAY_RELOAD   = 1002;
constexpr UINT IDM_TRAY_CLEAR    = 1003;
constexpr UINT IDM_TRAY_EXIT     = 1099;
constexpr UINT TRAY_ICON_ID      = 1;

static HWND g_hMainWnd       = nullptr;   // 隐藏的主窗口（消息泵）
static HWND g_hLogWnd         = nullptr;  // 日志窗口（按需创建）
static HWND g_hLogEdit        = nullptr;  // 日志窗口里的 Edit 控件
static NOTIFYICONDATAW g_nid   = {};
static std::atomic<bool> g_stopFlag{false};
static std::vector<std::thread> g_watchThreads;
static std::vector<HANDLE> g_watchHandles;  // 用来 CancelIoEx 优雅停止
static std::mutex g_watchMutex;

// 目录去重：避免同一个目录连续多次触发更新
static std::mutex g_ipcMutex;
static std::wstring g_lastWrittenDir;

// 上次写入快捷方式的目标，文件持久化（仅作记录）
static std::mutex g_pinMutex;
static std::wstring g_lastPinnedDir;

// 日志缓存（最近 N 行）
static std::mutex g_logMutex;
static std::deque<std::wstring> g_logLines;
constexpr size_t kMaxLogLines = 500;

// ============ 日志 ============
static void AppendLog(const std::wstring& line) {
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logLines.push_back(line);
        while (g_logLines.size() > kMaxLogLines) g_logLines.pop_front();
    }
    // 通过 PostMessage 通知 UI 线程刷新日志窗口（如果开着的话）
    if (g_hLogEdit) {
        PostMessageW(g_hMainWnd, WM_USER + 200, 0, 0);
    }
}

static void Logf(const wchar_t* tag, const wchar_t* fmt, ...) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t body[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t full[1200];
    swprintf_s(full, L"[%02d:%02d:%02d.%03d] [%-10s] %s",
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag, body);
    AppendLog(full);
}

// ============ Latest changed directory dispatcher ============
// 前向声明：定义在下面"快捷方式管理"那一节
static void UpdateQuickAccessPin(const std::wstring& newDir);

static void UpdateLastChangedDir(const std::wstring& dir) {
    {
        std::lock_guard<std::mutex> lock(g_ipcMutex);
        if (dir == g_lastWrittenDir) return;
        g_lastWrittenDir = dir;
    }
    Logf(L"DIR", L"目标目录 → %s", dir.c_str());
    UpdateQuickAccessPin(dir);
}

// ============ 容器目录 + .lnk 快捷方式管理 ============
//
// 思路：在用户 profile 下放一个或多个固定的"容器目录"（默认两个：
// %USERPROFILE%\QuickJumpFolders_latest 和 ...latest2）。每次 watcher 检测
// 到目录变化时，把每个容器里的 latest.lnk 重写成指向最新目录。
//
// 用户的一次性配置：把容器目录拖到资源管理器左侧"快速访问"。
// 之后点开容器目录 → 双击 latest.lnk → 进入真实的最新目录路径，
// 路径栏显示的是真实路径，向上导航能正常工作（这是 junction 做不到的）。
//
// 持久化：g_lastPinnedDir 写到 %APPDATA%\QuickJumpFolders\last_pinned.txt，
// 仅作日志/状态记录用，不参与去重逻辑。

static std::wstring GetPinStateFilePath() {
    wchar_t buf[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) {
        return L"";
    }
    std::wstring dir = buf;
    dir += L"\\QuickJumpFolders";
    CreateDirectoryW(dir.c_str(), nullptr);  // 已存在会返回 ERROR_ALREADY_EXISTS，无视
    return dir + L"\\last_pinned.txt";
}

static std::wstring LoadLastPinnedDir() {
    std::wstring path = GetPinStateFilePath();
    if (path.empty()) return L"";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 2 || sz.QuadPart > 8192) { CloseHandle(h); return L""; }
    std::vector<BYTE> raw((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(h, raw.data(), (DWORD)raw.size(), &got, nullptr);
    CloseHandle(h);
    if (raw.size() < 2 || raw[0] != 0xFF || raw[1] != 0xFE) return L"";
    size_t wn = (raw.size() - 2) / sizeof(wchar_t);
    std::wstring s(reinterpret_cast<wchar_t*>(raw.data() + 2), wn);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L'\0'))
        s.pop_back();
    return s;
}

static void SaveLastPinnedDir(const std::wstring& dir) {
    std::wstring path = GetPinStateFilePath();
    if (path.empty()) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD written = 0;
    WriteFile(h, bom, 2, &written, nullptr);
    if (!dir.empty()) {
        WriteFile(h, dir.c_str(), (DWORD)(dir.size() * sizeof(wchar_t)), &written, nullptr);
    }
    CloseHandle(h);
}

// ============ 快捷方式（.lnk）方案 ============
//
// 用 IShellLink 在固定容器目录里维护一个 .lnk，用户体验：
//   1) 容器目录 %USERPROFILE%\QuickJumpFolders_latest 是个普通文件夹
//   2) 用户手动把它拖到"快速访问"（一次性）
//   3) 容器里只有一个 "最新修改.lnk"，watcher 每次重写它指向最新目录
// 用户点 QA → 进容器 → 双击快捷方式 → 进真实目标路径（"上一级"按钮能正常工作，
// 这是 junction 做不到的）

// 维护多个容器目录，每个都装一份"最新修改.lnk"。用户可以把它们分别
// 拖到不同位置（比如两个固定到快速访问，一个放桌面），都会同步更新。
static std::vector<std::wstring> GetContainerDirPaths() {
    wchar_t buf[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf))) {
        return {};
    }
    std::wstring profile = buf;
    return {
        profile + L"\\QuickJumpFolders_latest",
        profile + L"\\QuickJumpFolders_latest2",
    };
}

static std::wstring GetShortcutFileName() {
    return L"latest.lnk";
}

// 确保 path 是一个普通目录。如果是旧版本留下的 junction（reparse point），
// 把 reparse 移除让它变回普通空目录。
static bool EnsurePlainDirectory(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (CreateDirectoryW(path.c_str(), nullptr)) return true;
        Logf(L"LNK", L"创建容器目录失败 %s err=%lu", path.c_str(), GetLastError());
        return false;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        Logf(L"LNK", L"路径已被普通文件占用: %s", path.c_str());
        return false;
    }
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        // 是旧的 junction，删掉重建（RemoveDirectoryW 对 junction 只删链接、不动目标）
        if (!RemoveDirectoryW(path.c_str())) {
            Logf(L"LNK", L"移除旧 junction 失败 %s err=%lu",
                 path.c_str(), GetLastError());
            return false;
        }
        if (!CreateDirectoryW(path.c_str(), nullptr)) {
            Logf(L"LNK", L"重建容器目录失败 %s err=%lu",
                 path.c_str(), GetLastError());
            return false;
        }
        Logf(L"LNK", L"已把旧 junction 转换为普通目录: %s", path.c_str());
    }
    return true;
}

// 在 lnkPath 创建/更新一个指向 targetPath 的 .lnk 快捷方式。
// 使用 IShellLink + IPersistFile（纯文件 I/O 包了一层 COM，不会卡线程）。
static bool CreateOrUpdateShortcut(const std::wstring& lnkPath,
                                   const std::wstring& targetPath) {
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hrInit);
    if (hrInit == RPC_E_CHANGED_MODE) {
        hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        needUninit = SUCCEEDED(hrInit);
    }

    bool ok = false;
    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
    if (SUCCEEDED(hr) && psl) {
        psl->SetPath(targetPath.c_str());
        psl->SetWorkingDirectory(targetPath.c_str());
        psl->SetDescription(L"QuickJumpFolders - 最近修改的目录");

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_PPV_ARGS(&ppf));
        if (SUCCEEDED(hr) && ppf) {
            hr = ppf->Save(lnkPath.c_str(), TRUE);
            ok = SUCCEEDED(hr);
            if (!ok) {
                Logf(L"LNK", L"IPersistFile::Save 失败 hr=0x%08X path=%s",
                     hr, lnkPath.c_str());
            }
            ppf->Release();
        }
        psl->Release();
    } else {
        Logf(L"LNK", L"CoCreateInstance(ShellLink) 失败 hr=0x%08X", hr);
    }

    if (needUninit) CoUninitialize();
    return ok;
}

// 更新"最新目录"——通过更新容器目录里的 .lnk 快捷方式实现。
//
// 用户需要做的一次性配置：手动把 %USERPROFILE%\QuickJumpFolders_latest 拖进
// 文件资源管理器的"快速访问"。点开后里面会有一个 latest.lnk，
// 双击进入真实路径，向上导航能正常回到真实父目录。
//
// 不做去重——更新一个 .lnk 文件本质上就是几 KB 的写盘，重复无害；
// 去重反而会因为持久化状态命中导致漏更新（重启后第一次事件可能被吞）。
static void UpdateQuickAccessPin(const std::wstring& newDir) {
    {
        std::lock_guard<std::mutex> lock(g_pinMutex);
        g_lastPinnedDir = newDir;
        SaveLastPinnedDir(newDir);
    }

    auto containers = GetContainerDirPaths();
    if (containers.empty()) {
        Logf(L"LNK", L"找不到用户 profile 目录");
        return;
    }

    for (const auto& containerDir : containers) {
        if (!EnsurePlainDirectory(containerDir)) continue;
        std::wstring lnkPath = containerDir + L"\\" + GetShortcutFileName();
        bool ok = CreateOrUpdateShortcut(lnkPath, newDir);
        Logf(L"LNK", L"更新 %s → %s [%s]",
             containerDir.c_str(), newDir.c_str(), ok ? L"OK" : L"FAIL");
    }
}

// ============ 监控线程 ============
static const wchar_t* ActionName(DWORD action) {
    switch (action) {
        case FILE_ACTION_ADDED:            return L"CREATED";
        case FILE_ACTION_REMOVED:          return L"DELETED";
        case FILE_ACTION_MODIFIED:         return L"MODIFIED";
        case FILE_ACTION_RENAMED_OLD_NAME: return L"RENAME<-";
        case FILE_ACTION_RENAMED_NEW_NAME: return L"RENAME->";
        default:                           return L"UNKNOWN";
    }
}

static void WatchDirectoryThread(std::wstring rootDir) {
    while (!rootDir.empty() && (rootDir.back() == L'\\' || rootDir.back() == L'/')) {
        rootDir.pop_back();
    }

    HANDLE hDir = CreateFileW(
        rootDir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        Logf(L"ERROR", L"打开目录失败: %s (err=%lu)", rootDir.c_str(), GetLastError());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_watchMutex);
        g_watchHandles.push_back(hDir);
    }

    Logf(L"INFO", L"开始监控: %s （递归子目录）", rootDir.c_str());

    constexpr DWORD kBufSize = 64 * 1024;
    std::vector<BYTE> buffer(kBufSize);

    while (!g_stopFlag.load()) {
        DWORD bytesReturned = 0;
        BOOL ok = ReadDirectoryChangesW(
            hDir, buffer.data(), kBufSize, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned, nullptr, nullptr);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED) break;  // 我们主动 cancel 了
            Logf(L"ERROR", L"ReadDirectoryChangesW 失败 err=%lu on %s", err, rootDir.c_str());
            break;
        }
        if (bytesReturned == 0) {
            Logf(L"WARN", L"事件缓冲溢出 on %s", rootDir.c_str());
            continue;
        }

        BYTE* p = buffer.data();
        while (true) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);
            std::wstring relName(info->FileName, info->FileNameLength / sizeof(wchar_t));
            std::wstring fullPath = rootDir + L"\\" + relName;
            std::wstring fileName = GetFileName(relName);

            bool ignored = ShouldIgnoreFile(fileName);
            bool isInteresting =
                info->Action == FILE_ACTION_ADDED ||
                info->Action == FILE_ACTION_MODIFIED ||
                info->Action == FILE_ACTION_RENAMED_NEW_NAME;

            const wchar_t* tag = ActionName(info->Action);
            std::wstring suffix;
            if (ignored)             suffix = L"  [ignored]";
            else if (!isInteresting) suffix = L"  [not-tracked]";
            Logf(tag, L"%s%s", fullPath.c_str(), suffix.c_str());

            if (isInteresting && !ignored) {
                DWORD attrs = GetFileAttributesW(fullPath.c_str());
                bool isFile = (attrs != INVALID_FILE_ATTRIBUTES) &&
                              !(attrs & FILE_ATTRIBUTE_DIRECTORY);
                if (isFile) {
                    UpdateLastChangedDir(GetParentDir(fullPath));
                }
            }

            if (info->NextEntryOffset == 0) break;
            p += info->NextEntryOffset;
        }
    }

    CloseHandle(hDir);
    Logf(L"INFO", L"停止监控: %s", rootDir.c_str());
}

// ============ 配置加载 ============
static std::vector<std::wstring> LoadConfigFile(const std::wstring& configPath) {
    std::vector<std::wstring> dirs;
    HANDLE h = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Logf(L"ERROR", L"配置文件打不开: %s", configPath.c_str());
        return dirs;
    }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    std::vector<char> raw((size_t)sz.QuadPart);
    DWORD got = 0;
    if (!raw.empty()) ReadFile(h, raw.data(), (DWORD)raw.size(), &got, nullptr);
    CloseHandle(h);

    size_t off = 0;
    if (raw.size() >= 3 && (BYTE)raw[0] == 0xEF && (BYTE)raw[1] == 0xBB && (BYTE)raw[2] == 0xBF)
        off = 3;
    std::string utf8(raw.begin() + off, raw.end());
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring content(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), content.data(), wlen);

    std::wstring line;
    for (wchar_t c : content) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.erase(line.begin());
            while (!line.empty() && (line.back()  == L' ' || line.back()  == L'\t')) line.pop_back();
            if (!line.empty() && line[0] != L'#') dirs.push_back(line);
            line.clear();
        } else line += c;
    }
    while (!line.empty() && (line.front() == L' ' || line.front() == L'\t')) line.erase(line.begin());
    while (!line.empty() && (line.back()  == L' ' || line.back()  == L'\t')) line.pop_back();
    if (!line.empty() && line[0] != L'#') dirs.push_back(line);
    return dirs;
}

// ============ 启停监控 ============
static void StopAllWatchers() {
    g_stopFlag.store(true);
    {
        std::lock_guard<std::mutex> lock(g_watchMutex);
        for (HANDLE h : g_watchHandles) {
            CancelIoEx(h, nullptr);   // 让阻塞的 ReadDirectoryChangesW 返回
        }
    }
    for (auto& t : g_watchThreads) {
        if (t.joinable()) t.join();
    }
    g_watchThreads.clear();
    {
        std::lock_guard<std::mutex> lock(g_watchMutex);
        g_watchHandles.clear();
    }
    g_stopFlag.store(false);
}

static void StartWatchers(const std::vector<std::wstring>& dirs) {
    for (const auto& d : dirs) {
        g_watchThreads.emplace_back(WatchDirectoryThread, d);
    }
}

// ============ 日志窗口 ============
static void RefreshLogEdit() {
    if (!g_hLogEdit) return;
    std::wstring all;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        for (const auto& l : g_logLines) {
            all += l;
            all += L"\r\n";
        }
    }
    SetWindowTextW(g_hLogEdit, all.c_str());
    // 滚到底
    SendMessageW(g_hLogEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(g_hLogEdit, EM_SCROLLCARET, 0, 0);
}

static LRESULT CALLBACK LogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            if (g_hLogEdit) {
                RECT rc; GetClientRect(hWnd, &rc);
                MoveWindow(g_hLogEdit, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
        case WM_CLOSE:
            ShowWindow(hWnd, SW_HIDE);   // 关掉窗口实际只是隐藏，托盘还在
            return 0;
        case WM_DESTROY:
            g_hLogWnd = nullptr;
            g_hLogEdit = nullptr;
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void ShowLogWindow() {
    if (g_hLogWnd) {
        ShowWindow(g_hLogWnd, SW_SHOW);
        SetForegroundWindow(g_hLogWnd);
        return;
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = LogWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"QuickJumpFoldersLogWnd";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    static bool reg = false;
    if (!reg) { RegisterClassW(&wc); reg = true; }

    g_hLogWnd = CreateWindowExW(
        0, L"QuickJumpFoldersLogWnd", L"QuickJumpFolders —— 实时日志",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 500,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    g_hLogEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
        0, 0, 100, 100, g_hLogWnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    // 等宽字体看着舒服
    HFONT font = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(g_hLogEdit, WM_SETFONT, (WPARAM)font, TRUE);

    RefreshLogEdit();
    ShowWindow(g_hLogWnd, SW_SHOW);
    SetForegroundWindow(g_hLogWnd);
}

// ============ 托盘菜单 ============
static void ShowTrayMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW,   L"显示日志窗口(&S)");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_RELOAD, L"重载配置(&R)");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_CLEAR,  L"清空日志(&C)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT,   L"退出(&X)");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(menu);
}

// 用户全局变量保存配置文件路径，重载用
static std::wstring g_configPath;
static std::vector<std::wstring> g_cmdLineDirs;  // 命令行 -d 指定的

static std::vector<std::wstring> CollectAllDirs() {
    std::vector<std::wstring> all = g_cmdLineDirs;
    if (!g_configPath.empty()) {
        auto fromFile = LoadConfigFile(g_configPath);
        all.insert(all.end(), fromFile.begin(), fromFile.end());
    }
    // 去重
    std::vector<std::wstring> uniq;
    for (auto& d : all) {
        bool dup = false;
        for (auto& u : uniq) if (_wcsicmp(d.c_str(), u.c_str()) == 0) { dup = true; break; }
        if (!dup) uniq.push_back(d);
    }
    return uniq;
}

static void ReloadConfig() {
    Logf(L"INFO", L"重载配置...");
    StopAllWatchers();
    auto dirs = CollectAllDirs();
    if (dirs.empty()) {
        Logf(L"ERROR", L"重载后没有任何监控目录");
        return;
    }
    StartWatchers(dirs);
    Logf(L"INFO", L"重载完成，监控 %zu 个目录", dirs.size());
}

// ============ 主窗口消息处理 ============
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            // lParam 低字节 = 实际事件
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayMenu(hWnd);
            } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                ShowLogWindow();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_TRAY_SHOW:   ShowLogWindow(); break;
                case IDM_TRAY_RELOAD: ReloadConfig(); break;
                case IDM_TRAY_CLEAR: {
                    std::lock_guard<std::mutex> lock(g_logMutex);
                    g_logLines.clear();
                    if (g_hLogEdit) SetWindowTextW(g_hLogEdit, L"");
                    break;
                }
                case IDM_TRAY_EXIT:
                    PostMessageW(hWnd, WM_CLOSE, 0, 0);
                    break;
            }
            return 0;

        case WM_USER + 200:  // 日志更新通知
            if (g_hLogEdit && IsWindowVisible(g_hLogWnd)) {
                RefreshLogEdit();
            }
            return 0;

        case WM_CLOSE:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ============ 命令行解析 ============
//   watcher_tray.exe -c <配置文件> -d <目录1> -d <目录2> ...
//   不带任何参数则尝试默认配置文件 config\watch_dirs.txt
static void ParseCmdLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool gotConfig = false;
    for (int i = 1; i < argc; ++i) {
        if ((wcscmp(argv[i], L"-c") == 0 || wcscmp(argv[i], L"--config") == 0) && i + 1 < argc) {
            g_configPath = argv[++i];
            gotConfig = true;
        } else if ((wcscmp(argv[i], L"-d") == 0 || wcscmp(argv[i], L"--dir") == 0) && i + 1 < argc) {
            g_cmdLineDirs.push_back(argv[++i]);
        }
    }
    LocalFree(argv);

    if (!gotConfig && g_cmdLineDirs.empty()) {
        // 默认查找 exe 同目录下的 config\watch_dirs.txt
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring p = exePath;
        size_t pos = p.find_last_of(L"\\/");
        if (pos != std::wstring::npos) p = p.substr(0, pos + 1);
        p += L"watch_dirs.txt";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            g_configPath = p;
        } else {
            // 再试 ..\config\watch_dirs.txt
            std::wstring p2 = exePath;
            pos = p2.find_last_of(L"\\/");
            if (pos != std::wstring::npos) p2 = p2.substr(0, pos + 1);
            p2 += L"config\\watch_dirs.txt";
            if (GetFileAttributesW(p2.c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_configPath = p2;
            }
        }
    }
}

// ============ WinMain ============
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 防止多开
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\QuickJumpFolders_watcher_mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"QuickJumpFolders watcher 已经在运行中。",
                    L"QuickJumpFolders", MB_ICONINFORMATION);
        return 0;
    }

    ParseCmdLine();

    // 从磁盘恢复"上次固定的目录"，避免重启后留下孤儿 pin
    {
        std::lock_guard<std::mutex> lock(g_pinMutex);
        g_lastPinnedDir = LoadLastPinnedDir();
    }

    // 注册主窗口（隐藏的，只用来收消息）
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"QuickJumpFoldersMainWnd";
    RegisterClassW(&wc);

    g_hMainWnd = CreateWindowExW(0, L"QuickJumpFoldersMainWnd", L"QuickJumpFolders",
                                 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    // 创建托盘图标
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMainWnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);  // 用系统默认图标，省事
    wcscpy_s(g_nid.szTip, L"QuickJumpFolders 文件监控（双击查看日志）");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // 启动监控
    auto dirs = CollectAllDirs();
    if (dirs.empty()) {
        MessageBoxW(nullptr,
                    L"没有指定任何监控目录。\n\n请用 -d 参数或编辑配置文件 watch_dirs.txt",
                    L"QuickJumpFolders", MB_ICONWARNING);
    } else {
        StartWatchers(dirs);
        Logf(L"INFO", L"启动完成，监控 %zu 个目录。右键托盘图标查看选项。", dirs.size());

        // 提示用户首次需要手动把容器目录 pin 到快速访问
        auto containers = GetContainerDirPaths();
        for (const auto& c : containers) {
            EnsurePlainDirectory(c);
        }
        if (!containers.empty()) {
            std::wstring list;
            for (size_t i = 0; i < containers.size(); ++i) {
                if (i) list += L"  |  ";
                list += containers[i];
            }
            Logf(L"HINT",
                 L"一次性配置：把以下目录分别拖到资源管理器左侧"
                 L"\"快速访问\"（也可以一个固定一个放桌面等），"
                 L"里面的 latest.lnk 都会同步指向最新目录。"
                 L"目录： %s",
                 list.c_str());
        }
    }

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopAllWatchers();
    CloseHandle(hMutex);
    return 0;
}
