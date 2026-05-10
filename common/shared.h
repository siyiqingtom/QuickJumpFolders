// shared.h -- common helpers used by the watcher
//
// Filename filtering (ignore Office lock files, browser temp files, etc.)
// and tiny path helpers. Kept in a separate header so the watcher
// translation unit stays focused on file-watching logic.

#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace QuickJumpFolders {

// File-name prefixes to ignore: Office lock files, system noise, etc.
inline const std::vector<std::wstring>& GetIgnorePrefixes() {
    static const std::vector<std::wstring> prefixes = {
        L"~$",        // Word/Excel/PPT lock file
        L"~WRL",      // Word temporary file
        L".~lock",    // LibreOffice lock file
        L"Thumbs.db", // Explorer thumbnail cache
        L"desktop.ini"
    };
    return prefixes;
}

// File extensions to ignore (mostly browser/editor temp suffixes).
inline const std::vector<std::wstring>& GetIgnoreExtensions() {
    static const std::vector<std::wstring> exts = {
        L".tmp", L".temp", L".swp", L".bak", L".log",
        L".crdownload",  // Chrome download in progress
        L".part",        // Firefox / other downloader temp
        L".partial"
    };
    return exts;
}

inline bool ShouldIgnoreFile(const std::wstring& filename) {
    for (const auto& p : GetIgnorePrefixes()) {
        if (filename.size() >= p.size() &&
            _wcsnicmp(filename.c_str(), p.c_str(), p.size()) == 0) {
            return true;
        }
    }
    size_t dot = filename.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = filename.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        for (const auto& e : GetIgnoreExtensions()) {
            if (ext == e) return true;
        }
    }
    return false;
}

inline std::wstring GetParentDir(const std::wstring& fullPath) {
    size_t pos = fullPath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return fullPath.substr(0, pos);
}

inline std::wstring GetFileName(const std::wstring& fullPath) {
    size_t pos = fullPath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return fullPath;
    return fullPath.substr(pos + 1);
}

} // namespace QuickJumpFolders
