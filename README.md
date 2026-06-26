# nara

Een minimalistische web browser gebouwd op Microsoft Edge WebView2, geschreven in C++ met de Win32 API.

## Features

- **WebView2 engine** — dezelfde rendering engine als Microsoft Edge
- **Snelkoppelingen** — Google, Pinterest, Roblox
- **Cookie manager** — exporteer/importeer cookies naar `%APPDATA%\Nara\cookies.txt`
- **Dark/Light mode** — toggle via Settings knop
- **Wachtwoordenmanager** — sla inloggegevens op en vul ze automatisch in
- **Ctrl+R** — herlaad de huidige pagina

## Builden

### Vereisten

- LLVM MinGW toolchain (getest met [llvm-mingw](https://github.com/mstorsjo/llvm-mingw))
- CMake 3.20+ of compileer handmatig
- WebView2 SDK (wordt automatisch gedownload via CMake of handmatig van NuGet)

### Builden met CMake

```bash
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=windres
cmake --build build
```

### Handmatig builden

```bash
windres -O coff src/resources.rc build/resources.res
x86_64-w64-mingw32-clang++ -std=c++17 -O2 -s -static -fuse-ld=lld -mwindows \
    -I src -I build/webview2/build/native/include \
    -o build/mybrowser.exe \
    src/main.cpp src/browser_window.cpp build/resources.res \
    -Lbuild -Lbuild/webview2/build/native/x64 \
    -l:WebView2Loader.dll.lib -lgdi32 -lole32 -loleaut32 -luuid -lcomctl32 \
    -static-libgcc -static-libstdc++
```

## Licentie

MIT
