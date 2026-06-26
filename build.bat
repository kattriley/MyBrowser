@echo off
setlocal enabledelayedexpansion

set MINGW_ROOT=C:\Users\live_\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.MSVCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260602-msvcrt-x86_64
set PATH=%MINGW_ROOT%\bin;C:\Windows\System32

set CXX=%MINGW_ROOT%\bin\x86_64-w64-mingw32-clang++.exe
set WINDRES=%MINGW_ROOT%\bin\windres.exe
set AR=%MINGW_ROOT%\bin\llvm-ar.exe

set SRC_DIR=%~dp0src
set BUILD_DIR=%~dp0build
set WEBVIEW2_DIR=%BUILD_DIR%\webview2

echo == Downloading WebView2 SDK ==
if not exist "%WEBVIEW2_DIR%\build\native\include\WebView2.h" (
    if not exist "%BUILD_DIR%\webview2.zip" (
        curl -L -o "%BUILD_DIR%\webview2.zip" "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40"
    )
    if not exist "%BUILD_DIR%\webview2" mkdir "%BUILD_DIR%\webview2"
    cd /d "%BUILD_DIR%\webview2"
    tar -xf "%BUILD_DIR%\webview2.zip" 2>nul
    cd /d "%~dp0"
)

set WV_INCLUDE=%WEBVIEW2_DIR%\build\native\include
set WV_LIB=%WEBVIEW2_DIR%\build\native\x64

echo == Compiling resources ==
%WINDRES% -O coff "%SRC_DIR%\resources.rc" "%BUILD_DIR%\resources.res"

echo == Compiling ==
%CXX% -std=c++17 -O2 -s -static ^
    -I "%SRC_DIR%" -I "%WV_INCLUDE%" ^
    -o "%BUILD_DIR%\mybrowser.exe" ^
    "%SRC_DIR%\main.cpp" "%SRC_DIR%\browser_window.cpp" ^
    "%BUILD_DIR%\resources.res" ^
    -L"%BUILD_DIR%" -L"%WV_LIB%" ^
    -lwebview2loader -lole32 -loleaut32 -luuid -static-libgcc -static-libstdc++

echo == Copying WebView2Loader.dll ==
copy /Y "%WV_LIB%\WebView2Loader.dll" "%BUILD_DIR%\"

echo == Done ==
if exist "%BUILD_DIR%\mybrowser.exe" (
    echo Build successful!
    dir "%BUILD_DIR%\mybrowser.exe"
) else (
    echo Build FAILED!
)
