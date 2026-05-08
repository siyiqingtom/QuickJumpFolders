# QuickJumpFolders

> Keep your file dialogs one click away from the folders you actually work in.  
> 让任何“打开文件”对话框一两下就回到你刚刚工作的目录。

[English](#english) · [中文](#中文)

---

## English

### What it does

A common workflow: edit a file in some project folder → switch to a browser / Word / messenger to upload or attach it.  
Most file dialogs still open in “Documents” or wherever you were last, so you click through a bunch of folders just to get back to where you were working.

**QuickJumpFolders** watches the directory trees you tell it to.  
Whenever you create / modify / rename a file inside them, it updates a couple of pinned shortcut containers so any file dialog can reach that folder in one or two clicks from Quick access.

### How it works

1. **Watcher thread**  
   Uses `ReadDirectoryChangesW` to recursively watch your configured roots, capturing file create / delete / rename / modify events and filtering out noisy Office lock files, browser temp files, etc.  
2. **Shortcut manager**  
   Maintains a `latest.lnk` inside two fixed container directories (`%USERPROFILE%\smart_upload_latest` and `%USERPROFILE%\smart_upload_latest2`). Whenever the tracked directory changes, it rewrites both `.lnk` files to point to the most recently touched folder.  
3. **One-time setup**  
   You drag these two container directories into Windows Explorer’s “Quick access” sidebar. The pins stay there forever, but the `latest.lnk` inside always points at the most recently modified folder.  
4. **Daily flow**  
   In any file dialog (Word / browser / IM file transfer / …), click the pinned container in Quick access → double‑click `latest.lnk` → you land on the real target folder. The address bar shows the real path, so the “Up” button navigates to the actual parent directory.

### Why not just hook Office / browsers?

The first design tried to hook `IFileOpenDialog::Show`, injecting into Office / Chrome processes to force their open dialogs to a specific folder. In practice:

- Office's COM path varies across Windows versions (`combase.dll` vs `ole32.dll`), so the hook frequently fails to attach.  
- Even when it attaches, Office's own “remember last folder” logic often overrides `SetFolder`.  
- Injectors tend to trigger AV / Defender and make deployment painful.

The shortcut‑based approach lives entirely in user space, never touches Office or browsers, and is rock‑solid at the cost of one extra double‑click.

---

### Build

Requires **Visual Studio 2017/2019/2022** or **Build Tools for Visual Studio** with the **“Desktop development with C++”** workload installed.  
Note: **VS Code (blue icon) is not Visual Studio** – it does not include a C++ compiler.

```cmd
git clone https://github.com/<your-username>/QuickJumpFolders.git
cd QuickJumpFolders
build.bat
```

`build.bat` auto‑discovers `cl.exe`. If it finds a suitable toolchain, it compiles and produces:

- `build\watcher_tray.exe`

---

### Usage

#### 1. Configure watched directories

Edit `build\watch_dirs.txt`, one directory per line (lines starting with `#` are comments):

```text
D:\Work
D:\Projects
# E:\Backup\Scratch
```

Alternatively, you can use a custom config file or command‑line flags (see **CLI flags**).

#### 2. Start the tray app

Double‑click `run.bat` or run `build\watcher_tray.exe` directly.

A tray icon appears in the system tray (bottom‑right). Right‑click it for:

- Show log  
- Reload config  
- Exit

#### 3. One‑time shortcut pinning

On startup, the tray log prints a `[HINT]` line with the two container paths, for example:

```text
C:\Users\you\smart_upload_latest
C:\Users\you\smart_upload_latest2
```

Open Explorer, navigate to these folders, and **drag each of them into the “Quick access” sidebar** on the left.  
You only need to do this once. You can also drop one on the desktop or anywhere else if it fits your workflow better.

These containers always stay pinned, but the `latest.lnk` inside is constantly updated to point to the most recently modified folder under your watched roots.

#### 4. Daily flow

1. Edit any file under a watched directory (save / rename / create / delete).  
2. The watcher updates `latest.lnk` in both container directories to the parent folder of the latest change.  
3. In any file dialog (browser upload, Word “Open…”, IM attachment, etc.), click the pinned container in Quick access.  
4. Double‑click `latest.lnk` → you are now in the latest folder, with the correct path visible in the address bar.  
5. Use the “Up” button as usual to navigate to its parent or elsewhere.

---

### CLI flags

You can override the default config file or add extra directories on the command line:

```text
watcher_tray.exe
    Read build\watch_dirs.txt or config\watch_dirs.txt by default.

watcher_tray.exe -c <file>
    Use an explicit config file.

watcher_tray.exe -d <dir1> -d <dir2> ...
    Add one or more directories from CLI (can be combined with -c).
```

The tray log will show which directories are effectively being watched after startup.

---

### Limitations

- Only tested on local NTFS / FAT volumes; network drives are untested.  
- Tracks only **the single most recent** modified folder, not a full history list.  
- The number of container directories is hard‑coded to two; edit `GetContainerDirPaths()` if you want more.  
- No built‑in autostart; if you want it to start with Windows, drop a shortcut to `run.bat` into `shell:startup`.

---

### Roadmap / ideas

Some potential future improvements:

- Track a small history of “recent folders” instead of only the latest one.  
- Optional GUI for configuring watched directories, container paths, and limits.  
- Per‑app behaviour (e.g., different roots or filters for Office vs browsers).  
- Better handling / UI for network drives and removable disks.

Contributions and ideas are welcome – feel free to open issues or PRs.

---

### License

MIT — see [LICENSE](LICENSE).

---

## 中文

### 这是什么

很多日常工作都长这样：在某个项目文件夹里改了文件 → 切到浏览器 / Word / 微信，把它上传或附加出去。  
但每次打开“打开/上传文件”对话框，默认位置大多在“文档”或上一次的路径，你要一层一层点回刚才的目录。

**QuickJumpFolders** 会在后台监控你指定的目录树。  
只要你在里面新建 / 修改 / 重命名了文件，它就会同步更新几个固定的快捷目录，让你在任何文件对话框里，从“快速访问”一两下就能跳到那个目录。

### 工作原理

1. **目录监控线程**  
   使用 `ReadDirectoryChangesW` 递归监控你配置的根目录，实时捕捉文件的创建 / 删除 / 重命名 / 修改事件，并过滤掉 Office 锁文件、浏览器临时文件等噪声。  
2. **快捷方式管理**  
   在 `%USERPROFILE%\smart_upload_latest` 和 `%USERPROFILE%\smart_upload_latest2` 两个固定容器目录里维护 `latest.lnk`。每当目标目录变化，就重写这两个 `.lnk`，让它们始终指向“最近一次真正被你操作过的目录”。  
3. **一次性配置**  
   你只需把这两个容器目录拖到资源管理器左侧的“快速访问”。Pin 会一直存在，但里面的 `latest.lnk` 会不断被更新。  
4. **日常使用**  
   在任意文件对话框（Word / 浏览器 / 微信文件传输 / …）里，点击“快速访问”中的容器目录 → 双击 `latest.lnk` → 直接进入真实目标目录。地址栏展示的是真实路径，“上一级”按钮照常工作。

### 为什么不用 hook 注入 Office / 浏览器？

最初版本尝试过 hook `IFileOpenDialog::Show`：把注入器丢进 Office / Chrome 进程里，强行改它们打开对话框时的初始目录。实测下来问题不少：

- Office 在不同 Windows 版本上的 COM 调用路径不一样（`combase.dll` vs `ole32.dll`），hook 经常打不上。  
- 即便 hook 成功，Office 自己的“记住上次访问目录”逻辑也会覆盖 `SetFolder`。  
- 各种 AV / Defender 很不喜欢注入器，拦截和误报的问题多。

最终选择了“快捷方式 + 快速访问”这个完全用户态、**不动 Office / 浏览器任何东西**的方案。代价是用户多双击一下，但稳定可靠，可预期。

---

### 安装编译

需要：

- **Visual Studio 2017 / 2019 / 2022**，或  
- **Build Tools for Visual Studio**（安装时勾选“使用 C++ 的桌面开发 / Desktop development with C++”）。

注意：**VS Code（蓝色图标）不是 Visual Studio**，它不带 C++ 编译器。

```cmd
git clone https://github.com/<你的用户名>/QuickJumpFolders.git
cd QuickJumpFolders
build.bat
```

`build.bat` 会自动寻找你机器上的 `cl.exe`，找到就直接编译，产物主要是：

- `build\watcher_tray.exe`

---

### 使用方式

#### 1. 配置要监控的目录

编辑 `build\watch_dirs.txt`，每行一个目录，以 `#` 开头的是注释：

```text
D:\工作
D:\项目
# E:\备用盘\临时
```

也可以用自定义配置文件或命令行参数，见下方 **命令行参数**。

#### 2. 启动托盘程序

双击 `run.bat`，或直接运行 `build\watcher_tray.exe`。

系统右下角托盘区域会出现一个图标，右键菜单包括：

- 显示日志  
- 重载配置  
- 退出  

#### 3. 一次性配置“快速访问”快捷方式

程序启动时，托盘日志中会有一行 `[HINT]`，告诉你两个容器目录的路径，例如：

```text
C:\Users\你\smart_upload_latest
C:\Users\你\smart_upload_latest2
```

打开资源管理器，找到这两个目录，**分别拖到左侧“快速访问”区域**。  
这一步只需要做一次。你也可以把其中一个目录拖到桌面或别的位置，按自己习惯来。

之后，这两个容器目录会一直出现在“快速访问”里，而它们里面的 `latest.lnk` 会始终指向最新的目标目录。

#### 4. 日常使用流程

1. 在任意被监控的目录下修改 / 新建 / 重命名文件。  
2. watcher 线程会立刻更新两个容器目录中的 `latest.lnk` 指向这个目录。  
3. 打开任意文件对话框（浏览器上传、Word 打开、IM 附件等），点击左侧“快速访问”中的容器目录。  
4. 双击 `latest.lnk` → 你就已经在最新目录里了，地址栏显示的是真实路径。  
5. 想去上一级/兄弟目录时，照常用“上一级”按钮即可。

---

### 命令行参数

可以通过命令行指定配置，覆盖默认行为：

```text
watcher_tray.exe
    默认读取 build\watch_dirs.txt 或 config\watch_dirs.txt。

watcher_tray.exe -c <文件>
    使用指定的配置文件。

watcher_tray.exe -d <目录1> -d <目录2> ...
    通过命令行追加多个要监控的目录（可与 -c 混用）。
```

启动后，你可以在托盘日志里确认最终实际生效的监控目录列表。

---

### 限制

- 目前只在本机 NTFS / FAT 分区上测试通过，网络盘暂未验证。  
- 只跟踪**最近一个**发生变化的目录，不维护完整历史列表。  
- 容器目录数量目前硬编码为两个，如需更多可以修改代码中的 `GetContainerDirPaths()`。  
- 没有内置开机自启；想开机自启，可以把 `run.bat` 的快捷方式丢到 `shell:startup` 里。

---

### 后续计划（想法）

一些可能的改进方向：

- 支持“最近目录列表”，而不仅仅是单一最新目录。  
- 提供简单 GUI 来配置监控目录、容器路径和数量限制。  
- 根据不同应用采用不同策略（例如对 Office 和浏览器使用不同的根目录或过滤规则）。  
- 对网络盘和移动设备做更好的兼容和提示。

欢迎提 issue / PR，一起把这个小工具打磨得更顺手。

---

### 许可证

MIT — 详见 [LICENSE](LICENSE)。