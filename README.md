# Nara

A minimalist web browser built on Microsoft Edge WebView2, written in C++ with the Win32 API.

Made by **yutaa** — github.com/kattriley

## Downloads

Download the latest release from [releases](https://github.com/kattriley/nara/releases).  
Place `nara.exe` and `WebView2Loader.dll` in the same folder and run `nara.exe`.

## Features

- **WebView2 engine** — same rendering engine as Microsoft Edge
- **Address bar** — type a URL and press Enter or click Go (https:// added automatically)
- **Multiple tabs** — click \[+\] for a new tab, click tabs to switch
- **Back / Forward / Reload** — navigation buttons in the toolbar
- **Search in page** — 🔍 opens a JS prompt and highlights text on the page
- **Quick links** — 🔍 Google, 📌 Pinterest, ⭐ Roblox, 🎮 Discord, 🎵 Spotify
- **Downloads** — 📩 (placeholder)
- **Dark mode** — 🌙 applies dark theme to the entire WebView
- **Site dark mode** — 🌙 in toolbar, per-site toggle (invert/hue-rotate)
- **Cookie manager** — 📥 import / 📤 export to `%APPDATA%\Nara\cookies.txt`
- **Password manager** — save login credentials (Base64 stored in `%APPDATA%\Nara\passwords.txt`), auto-fill, view, and clear
- **Bookmarks** — add and view via `%APPDATA%\Nara\bookmarks.txt`
- **Dark/Light theme** — toggle via the Settings menu (⚙)
- **Language** — English / Nederlands / Polski via Settings menu
- **Credits** — ⚙ → Credits
- **Ctrl+R** — reload current page
- **Inter font** — automatically downloaded from GitHub if not installed
- **No console** — `-mwindows` flag, clean Windows application

## Building

### Requirements

- LLVM MinGW toolchain (tested with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw))
- CMake 3.20+
- WebView2 SDK (automatically downloaded via CMake)

### Build with CMake

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=windres
cmake --build build
```

### Build without CMake

```bash
windres -O coff src/resources.rc build/resources.res
x86_64-w64-mingw32-clang++ -std=c++17 -O2 -s -static -fuse-ld=lld -mwindows \
    -I src -I build/webview2/build/native/include \
    -o build/nara.exe \
    src/main.cpp src/browser_window.cpp build/resources.res \
    -Lbuild -Lbuild/webview2/build/native/x64 \
    -l:WebView2Loader.dll.lib -lgdi32 -lole32 -loleaut32 -luuid -lcomctl32 \
    -static-libgcc -static-libstdc++
```

## File structure

```
src/
├── main.cpp              — WinMain, WndProc, toolbar creation, tab bar
├── main.h                — button IDs, constants
├── lang.h                — translation tables (EN/NL/PL), T() macro
├── browser_window.cpp    — BrowserWindow: tabs, address bar, navigation, cookies, passwords, bookmarks
├── browser_window.h      — BrowserWindow class, TabInfo struct
└── resources.rc          — (optional) icon resources
```

## License

MIT














%APPDATA%\Nara\
