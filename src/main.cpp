#include "main.h"
#include "browser_window.h"
#include <urlmon.h>

HWND gMainHwnd = nullptr;
BrowserWindow* gBrowser = nullptr;

static HBRUSH gToolbarBrush = nullptr;
static HFONT gToolbarFont = nullptr;

static void LoadToolbarFont() {
  const wchar_t* fontName = L"Segoe UI Variable";
  const wchar_t* fallback = L"Segoe UI";

  // Check if font exists via registry
  HKEY hKey;
  wchar_t fontPath[MAX_PATH];
  bool found = false;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    wchar_t valueName[256];
    DWORD valNameSz = 256;
    DWORD valSz = sizeof(fontPath);
    DWORD type = 0;
    DWORD idx = 0;
    while (RegEnumValueW(hKey, idx++, valueName, &valNameSz,
                         nullptr, &type, (BYTE*)fontPath, &valSz) == ERROR_SUCCESS) {
      if (wcsstr(valueName, fontName)) { found = true; break; }
      valNameSz = 256; valSz = sizeof(fontPath);
    }
    RegCloseKey(hKey);
  }

  if (!found) {
    // Try downloading Inter font
    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    wcscat_s(tmpPath, L"NaraInter.ttf");
    if (GetFileAttributesW(tmpPath) == INVALID_FILE_ATTRIBUTES) {
      URLDownloadToFileW(nullptr,
        L"https://github.com/rsms/inter/raw/master/docs/font-files/Inter-Regular.ttf",
        tmpPath, 0, nullptr);
    }
    if (GetFileAttributesW(tmpPath) != INVALID_FILE_ATTRIBUTES) {
      AddFontResourceW(tmpPath);
      fontName = L"Inter";
      found = true;
    }
  }

  if (!found) fontName = fallback;

  LOGFONTW lf = {};
  lf.lfHeight = -11;
  lf.lfWeight = FW_NORMAL;
  wcscpy_s(lf.lfFaceName, fontName);
  gToolbarFont = CreateFontIndirectW(&lf);
}

static void CreateToolbar(HWND parent) {
  if (!gToolbarFont) LoadToolbarFont();
  int x = 6, y = kBtnTop;
  auto btn = [&](int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      x, y, kBtnW, kBtnH, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (h && gToolbarFont) SendMessageW(h, WM_SETFONT, (WPARAM)gToolbarFont, TRUE);
    x += kBtnW + kBtnGap;
  };
  btn(kIdGoogleButton,    L"\xD83D\xDD0D Google");
  btn(kIdPinterestButton, L"\xD83D\xDCCC Pinterest");
  btn(kIdRobloxButton,    L"\x2B50 Roblox");
  btn(kIdDiscordButton,   L"\xD83C\xDFAE Discord");
  btn(kIdSpotifyButton,   L"\xD83C\xDFB5 Spotify");
  btn(kIdDownloads,       L"\xD83D\xDCE9 Downloads");
  btn(kIdSiteDark,        L"\xD83C\xDF19 Dark");
  x += 8;
  btn(kIdImportCookies,   L"\xD83D\xDCE5 Import");
  btn(kIdExportCookies,   L"\xD83D\xDCE4 Export");
  btn(kIdSettingsButton,  L"\x2699 Settings");
}

static void ResizeChildren(HWND hwnd) {
  if (!gBrowser) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  rc.top += kToolbarHeight;
  gBrowser->Resize();
}

static void UpdateToolbarBrush(bool dark) {
  if (gToolbarBrush) DeleteObject(gToolbarBrush);
  gToolbarBrush = CreateSolidBrush(dark ? RGB(50, 50, 55) : RGB(240, 242, 245));
  InvalidateRect(gMainHwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE:
      UpdateToolbarBrush(false);
      CreateToolbar(hwnd);
      break;

    case WM_ERASEBKGND: {
      HDC dc = (HDC)wParam;
      RECT rc;
      GetClientRect(hwnd, &rc);
      RECT toolbarRc = rc;
      toolbarRc.bottom = kToolbarHeight;
      FillRect(dc, &toolbarRc, gToolbarBrush);
      RECT webRc = rc;
      webRc.top = kToolbarHeight;
      FillRect(dc, &webRc, (HBRUSH)GetStockObject(WHITE_BRUSH));
      return TRUE;
    }

    case WM_SIZE:
      ResizeChildren(hwnd);
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case kIdGoogleButton:
          if (gBrowser) gBrowser->Navigate(L"https://www.google.com");
          break;
        case kIdPinterestButton:
          if (gBrowser) gBrowser->Navigate(L"https://www.pinterest.com");
          break;
        case kIdRobloxButton:
          if (gBrowser) gBrowser->Navigate(L"https://www.roblox.com");
          break;
        case kIdDiscordButton:
          if (gBrowser) gBrowser->Navigate(L"https://discord.com");
          break;
        case kIdSpotifyButton:
          if (gBrowser) gBrowser->Navigate(L"https://open.spotify.com");
          break;
        case kIdDownloads:
          if (gBrowser) gBrowser->ShowDownloads();
          break;
        case kIdSiteDark:
          if (gBrowser) gBrowser->ToggleSiteDark();
          break;
        case kIdImportCookies:
          if (gBrowser) gBrowser->ImportCookies();
          break;
        case kIdExportCookies:
          if (gBrowser) gBrowser->ExportCookies();
          break;
        case kIdAccelReload:
          if (gBrowser) gBrowser->Reload();
          break;
        case kIdSettingsButton: {
          HMENU menu = CreatePopupMenu();
          AppendMenuW(menu, MF_STRING, kIdToggleTheme,
                      gBrowser && gBrowser->IsDark() ? L"Light Mode" : L"Dark Mode");
          HMENU langMenu = CreatePopupMenu();
          AppendMenuW(langMenu, MF_STRING, kIdLangEnglish, L"English");
          AppendMenuW(langMenu, MF_STRING, kIdLangDutch, L"Nederlands");
          AppendMenuW(menu, MF_POPUP, (UINT_PTR)langMenu, L"Language");
          AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
          AppendMenuW(menu, MF_STRING, kIdCredits, L"Credits");
          POINT pt;
          GetCursorPos(&pt);
          SetForegroundWindow(hwnd);
          TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                         pt.x, pt.y, 0, hwnd, nullptr);
          DestroyMenu(langMenu);
          DestroyMenu(menu);
          break;
        }
        case kIdToggleTheme:
          if (gBrowser) {
            gBrowser->ToggleTheme();
            UpdateToolbarBrush(gBrowser->IsDark());
          }
          break;
        case kIdLangEnglish:
          MessageBoxW(hwnd, L"Language set to English.\n\nUI translation coming soon!", L"Language", MB_OK);
          break;
        case kIdLangDutch:
          MessageBoxW(hwnd, L"Taal ingesteld op Nederlands.\n\nUI vertaling komt binnenkort!", L"Taal", MB_OK);
          break;
        case kIdCredits: {
          MessageBoxW(hwnd,
            L"Nara Browser\n\n"
            L"Made by yutaa\n"
            L"github.com/kattriley\n\n"
            L"Powered by WebView2",
            L"Credits", MB_OK);
          break;
        }

      }
      break;

    case WM_DESTROY:
      if (gToolbarBrush) DeleteObject(gToolbarBrush);
      PostQuitMessage(0);
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateMainWindow(HINSTANCE hInstance) {
  const wchar_t kClassName[] = L"NaraWindow";

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wc.lpszClassName = kClassName;

  RegisterClassExW(&wc);

  DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
  RECT rc = {0, 0, kWindowWidth, kWindowHeight};
  AdjustWindowRect(&rc, style, FALSE);

  HWND hwnd = CreateWindowExW(
      0, kClassName, L"Nara",
      style, CW_USEDEFAULT, CW_USEDEFAULT,
      rc.right - rc.left, rc.bottom - rc.top,
      nullptr, nullptr, hInstance, nullptr);

  gMainHwnd = hwnd;

  HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
  if (hIcon) {
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
  }

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  return hwnd;
}

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE, LPSTR, int) {
  HWND hwnd = CreateMainWindow(hInstance);

  ACCEL accel = { FCONTROL, 'R', kIdAccelReload };
  HACCEL hAccel = CreateAcceleratorTableW(&accel, 1);

  BrowserWindow browser;
  gBrowser = &browser;
  browser.Initialize(hwnd);

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  if (hAccel) DestroyAcceleratorTable(hAccel);
  return (int)msg.wParam;
}
