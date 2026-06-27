#pragma once
#include <windows.h>

class BrowserWindow;

constexpr int kWindowWidth  = 1280;
constexpr int kWindowHeight = 800;

constexpr int kIdGoogleButton     = 101;
constexpr int kIdPinterestButton  = 102;
constexpr int kIdRobloxButton     = 103;
constexpr int kIdDiscordButton    = 109;
constexpr int kIdImportCookies    = 104;
constexpr int kIdExportCookies    = 105;
constexpr int kIdSettingsButton   = 106;
constexpr int kIdSpotifyButton    = 107;
constexpr int kIdDownloads        = 111;
constexpr int kIdSiteDark         = 112;

constexpr int kIdAccelReload      = 1001;

constexpr int kIdToggleTheme      = 200;
constexpr int kIdLangEnglish      = 201;
constexpr int kIdLangDutch        = 202;
constexpr int kIdCredits          = 203;

constexpr int kToolbarHeight = 36;
constexpr int kBtnW = 82;
constexpr int kBtnH = 24;
constexpr int kBtnGap = 3;
constexpr int kBtnTop = 6;

extern HWND gMainHwnd;
extern BrowserWindow* gBrowser;

HWND CreateMainWindow(HINSTANCE hInstance);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
