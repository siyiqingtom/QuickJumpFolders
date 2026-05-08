# smart_upload

> 自动追踪"最近修改的目录"，让上传/打开文件少点几次。
> Auto-track the most recently modified folder so file dialogs land where you want.

[English](#english) · [中文](#中文)

---

## 中文

### 这是什么

很多日常工作都涉及"在某个文件夹改文件 → 切到浏览器/Word/微信里把它上传出去"。每次上传时打开的文件对话框大多默认在"文档"或上次的位置，要点很多次才能回到刚才修改的目录。

`smart_upload` 在后台监控你指定的目录树。**只要你在里面新建/修改/重命名了文件，它就会同步更新一组固定的快捷方式，让你在任何文件对话框里一两下就能跳到那个目录。**

### 工作原理

1. **目录监控线程**：用 `ReadDirectoryChangesW` 递归监控你配置的根目录，实时捕捉文件 CRUD 事件，过滤掉 Office 锁文件、浏览器临时文件等噪声。
2. **快捷方式管理**：在 `%USERPROFILE%\smart_upload_latest` 和 `smart_upload_latest2` 两个固定容器目录里维护 `latest.lnk`，每当目标目录变化就重写这个 .lnk。
3. **用户一次性配置**：把这两个容器目录拖到资源管理器左侧"快速访问"。之后这两个 pin 永远在那里，但里面 `latest.lnk` 的目标始终是你**最近修改文件的那个目录**。
4. **使用**：在任意文件对话框（Word/浏览器/微信文件传输/...）点"快速访问"里固定的 `smart_upload_latest` → 双击 `latest.lnk` → 直接进入真实目标路径。点路径栏的"上一级"还能正常导航到真实父目录。

### 为什么不用 hook 注入 Office？

最初版本是 hook `IFileOpenDialog::Show`，被注入到 Office/Chrome 进程里强制改它们打开对话框时的初始目录。但实测：
- Office 文件对话框走的 COM 路径在不同 Windows 版本上很不一样（`combase.dll` vs `ole32.dll`），hook 经常打不上
- 即便 hook 打上，Office 自己的"记住上次访问目录"状态会覆盖 `SetFolder`
- AV/Defender 经常把注入器拦下来

最终选了"快捷方式"这个**完全在用户态、不动 Office 任何东西**的方案。代价是用户多双击一下，但稳定可靠、不依赖任何 hook。

### 安装编译

需要：**Visual Studio 2017/2019/2022** 或 **Build Tools for Visual Studio**（任选其一，安装时勾选"使用 C++ 的桌面开发"）。注意：**VSCode（蓝色图标）不是 Visual Studio**，它不带 C++ 编译器。

```cmd
git clone https://github.com/<你的用户名>/smart_upload.git
cd smart_upload
build.bat
```

`build.bat` 会自动找你机器上的 cl.exe，找到就直接编译。产物：`build\watcher_tray.exe`。

### 使用

#### 配置要监控的目录

编辑 `build\watch_dirs.txt`，每行一个目录（# 开头是注释）：

```
D:\工作
D:\项目
# E:\备用盘\临时
```

#### 启动

双击 `run.bat` 或直接运行 `build\watcher_tray.exe`。系统右下角会出现托盘图标，右键有"显示日志/重载配置/退出"。

#### 一次性配置快捷方式 pin

启动时托盘日志里会有一行 `[HINT]`，告诉你两个容器目录的路径：

```
C:\Users\你\smart_upload_latest
C:\Users\你\smart_upload_latest2
```

打开资源管理器，找到这两个目录，**分别拖到左侧"快速访问"**——只这一次。也可以一个 pin 一个放桌面，按你习惯。

#### 日常使用

随便改一个被监控目录里的文件 → watcher 立刻更新两个容器里的 `latest.lnk` → 在 Word/浏览器/任何文件对话框点快速访问里的 `smart_upload_latest` → 双击 `latest.lnk` → 你已经在最新目录里了。

### 命令行参数

```
watcher_tray.exe                        默认读 build\watch_dirs.txt 或 config\watch_dirs.txt
watcher_tray.exe -c <文件>               指定配置文件
watcher_tray.exe -d <目录1> -d <目录2>    命令行指定多个目录（可与 -c 混用）
```

### 限制

- 只支持本机 NTFS / FAT 目录，网络盘没测过
- 一次只跟踪"最近一个"修改的目录
- 容器目录硬编码两个，要更多自己改 `GetContainerDirPaths()`
- 不会自动启动；想开机自启可以把 `run.bat` 的快捷方式放到 `shell:startup`

---

## English

### What it does

A common workflow: edit a file in some folder → switch to browser/Word/messenger to upload it. Most file dialogs default to "Documents" or wherever you were last, so you click a lot to get back to the folder you just edited.

`smart_upload` watches the directory trees you tell it to. **Whenever you create/modify/rename a file inside, it updates a couple of pinned shortcuts so any file dialog can reach that folder in one or two clicks.**

### How it works

1. **Watcher thread** — `ReadDirectoryChangesW` recursively watches your configured root, debounces noisy Office lock files / browser temp files, and reports the parent directory of every interesting change.
2. **Shortcut manager** — maintains `latest.lnk` inside two fixed container directories (`%USERPROFILE%\smart_upload_latest` and `...latest2`); rewrites both whenever the tracked directory changes.
3. **One-time setup** — drag those two container directories to Windows Explorer's "Quick access". The pins stay there forever, but the `latest.lnk` inside always points to the most recently modified folder.
4. **Usage** — in any file dialog (Word / browser / IM file transfer / ...), click `smart_upload_latest` in Quick access → double-click `latest.lnk` → you land on the real target path. The address bar shows the real path, so the "Up" button navigates to the actual parent.

### Why not just hook Office?

The first design was a hook on `IFileOpenDialog::Show` injected into Office/Chrome processes that forced their open dialogs to a specific folder. In practice:
- Office's COM path varies across Windows versions (`combase.dll` vs `ole32.dll`); the hook frequently failed to attach.
- Even when attached, Office's own "remember last folder" state overrides `SetFolder`.
- AV / Defender flags injectors as suspicious.

The shortcut approach lives entirely in user space, never touches Office, and is rock-solid at the cost of one extra double-click.

### Build

Requires **Visual Studio 2017/2019/2022** or **Build Tools for Visual Studio** (with "Desktop development with C++" workload). Note: **VSCode (blue icon) is not Visual Studio** — it doesn't include a C++ compiler.

```cmd
git clone https://github.com/<your-username>/smart_upload.git
cd smart_upload
build.bat
```

`build.bat` auto-discovers `cl.exe`. Output: `build\watcher_tray.exe`.

### Usage

#### Configure watched directories

Edit `build\watch_dirs.txt`, one directory per line (lines starting with `#` are comments):

```
D:\Work
D:\Projects
# E:\Backup\Scratch
```

#### Start

Double-click `run.bat` or run `build\watcher_tray.exe`. A tray icon appears; right-click for "Show log / Reload config / Exit".

#### One-time shortcut pinning

On startup the tray log shows a `[HINT]` line with the two container paths:

```
C:\Users\you\smart_upload_latest
C:\Users\you\smart_upload_latest2
```

Open Explorer, navigate to those folders, and **drag each one onto "Quick access"** in the left sidebar — once and done. You can also drop one on the desktop or wherever, whatever fits your workflow.

#### Daily flow

Edit any file under a watched directory → watcher updates `latest.lnk` in both containers → in a file dialog click `smart_upload_latest` from Quick access → double-click `latest.lnk` → you're in the latest folder.

### CLI flags

```
watcher_tray.exe                          read build\watch_dirs.txt or config\watch_dirs.txt
watcher_tray.exe -c <file>                explicit config file
watcher_tray.exe -d <dir1> -d <dir2> ...  one or more directories from CLI (combinable with -c)
```

### Limitations

- Only tested on local NTFS / FAT volumes; network drives untested.
- Tracks only "the single most recent" modified folder, not a history.
- Container count hardcoded to two; edit `GetContainerDirPaths()` to add more.
- No autostart; drop a shortcut to `run.bat` into `shell:startup` if you want one.

### License

MIT — see [LICENSE](LICENSE).
