#include "main.h"
#include "browser_window.h"
#include <urlmon.h>
#include <commctrl.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

HWND gMainHwnd = nullptr;
BrowserWindow* gBrowser = nullptr;

static HBRUSH gToolbarBrush = nullptr;
static HBRUSH gSiteRowBrush = nullptr;
static HFONT gToolbarFont = nullptr;
static HFONT gAddrFont = nullptr;
static HFONT gTabFont = nullptr;

static void LoadToolbarFont() {
  const wchar_t* fontName = L"Segoe UI Variable";
  const wchar_t* fallback = L"Segoe UI";

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

  LOGFONTW lfAddr = {};
  lfAddr.lfHeight = -13;
  lfAddr.lfWeight = FW_NORMAL;
  wcscpy_s(lfAddr.lfFaceName, fontName);
  gAddrFont = CreateFontIndirectW(&lfAddr);

  gTabFont = gToolbarFont;
}

static bool gToolbarReady = false;

static WNDPROC gOrigAddrProc = nullptr;
static LRESULT CALLBACK AddrEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_CHAR && wParam == VK_RETURN) {
    if (gBrowser) {
      wchar_t url[1024];
      GetWindowTextW(hwnd, url, 1024);
      std::wstring ws(url);
      if (ws.find(L'.') != std::wstring::npos &&
          ws.find(L' ') == std::wstring::npos &&
          ws.find(L"://") == std::wstring::npos) {
        ws = L"https://" + ws;
      }
      gBrowser->Navigate(ws);
    }
    return 0;
  }
  return CallWindowProcW(gOrigAddrProc, hwnd, msg, wParam, lParam);
}

// -------------------------------------------------------------------
// Create toolbar parts
// -------------------------------------------------------------------
static void CreateToolbar(HWND parent) {
  if (!gToolbarFont) LoadToolbarFont();

  // Tab bar area (just an owner-draw area where we'll place tab buttons)
  // For simplicity, use actual button children

  // Navigation + address bar row (row 1)
  int y = kTabBarHeight + kBtnTop;
  int x = 6;

  auto navBtn = [&](int id, const wchar_t* text, int w) {
    HWND h = CreateWindowExW(0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      x, y, w, kBtnH, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (h && gToolbarFont) SendMessageW(h, WM_SETFONT, (WPARAM)gToolbarFont, TRUE);
    x += w + kBtnGap;
  };

  // Back, Forward, Reload
  navBtn(kIdBack,    L"\x25C0", kNavBtnW);   // ◀
  navBtn(kIdForward, L"\x25B6", kNavBtnW);   // ▶
  navBtn(kIdReload,  L"\x21BB", kNavBtnW);   // ↻

  // Address bar edit
  x += 4;
  int addrW = 580;
  RECT rc;
  GetClientRect(parent, &rc);
  int minAddr = 200;
  addrW = (rc.right - x - kNavBtnW - kBtnGap - 4);
  if (addrW < minAddr) addrW = minAddr;

  HWND addrEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    x, y, addrW, kBtnH, parent, (HMENU)(INT_PTR)kIdAddrEdit, nullptr, nullptr);
  if (addrEdit) {
    if (gAddrFont) SendMessageW(addrEdit, WM_SETFONT, (WPARAM)gAddrFont, TRUE);
    // Subclass for ENTER key
    gOrigAddrProc = (WNDPROC)SetWindowLongPtrW(addrEdit, GWLP_WNDPROC, (LONG_PTR)AddrEditProc);
    // store HWND for later - gBrowser might not be set yet
    SetWindowLongPtrW(addrEdit, GWLP_USERDATA, (LONG_PTR)addrEdit);
  }
  x += addrW + kBtnGap;

  // Go button
  navBtn(kIdGoAddr, L"Go", kNavBtnW + 6);

  // Search in page
  navBtn(kIdSearch, L"\xD83D\xDD0D", kNavBtnW);   // 🔍

  // Settings
  navBtn(kIdSettingsButton, L"\x2699", kNavBtnW);  // ⚙

  // Site buttons row (row 2)
  y = kTabBarHeight + kToolbarHeight + kBtnTop;
  x = 6;

  auto siteBtn = [&](int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      x, y, kBtnW, kBtnH, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (h && gToolbarFont) SendMessageW(h, WM_SETFONT, (WPARAM)gToolbarFont, TRUE);
    x += kBtnW + kBtnGap;
  };

  siteBtn(kIdGoogleButton,    L"\xD83D\xDD0D Google");    // 🔍
  siteBtn(kIdPinterestButton, L"\xD83D\xDCCC Pinterest"); // 📌
  siteBtn(kIdRobloxButton,    L"\x2B50 Roblox");          // ⭐
  siteBtn(kIdDiscordButton,   L"\xD83C\xDFAE Discord");   // 🎮
  siteBtn(kIdSpotifyButton,   L"\xD83C\xDFB5 Spotify");   // 🎵
  siteBtn(kIdDownloads,       L"\xD83D\xDCE9 Downloads"); // 📩
  siteBtn(kIdSiteDark,        L"\xD83C\xDF19 Dark");      // 🌙
  x += 8;
  siteBtn(kIdImportCookies,   L"\xD83D\xDCE5 Import");    // 📥
  siteBtn(kIdExportCookies,   L"\xD83D\xDCE4 Export");    // 📤

  // Tab bar
  // Create tab buttons at the very top
  int tabY = 0;
  HWND newTabBtn = CreateWindowExW(0, L"BUTTON", L"+",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
    0, tabY, 28, kTabBarHeight, parent, (HMENU)(INT_PTR)kIdNewTab, nullptr, nullptr);
  if (newTabBtn && gTabFont) SendMessageW(newTabBtn, WM_SETFONT, (WPARAM)gTabFont, TRUE);
}

// Recreate tab buttons
static void UpdateTabBar(HWND hwnd) {
  if (!gBrowser) return;

  // Destroy old tab buttons (keep + button and toolbar)
  HWND child = GetWindow(hwnd, GW_CHILD);
  while (child) {
    HWND next = GetWindow(child, GW_HWNDNEXT);
    int id = GetDlgCtrlID(child);
    if (id == kIdNewTab || id == 0 || (id >= 100 && id < 500)) {
      child = next;
      continue;
    }
    if (id >= kIdTabBase && id < kIdTabBase + 100) {
      DestroyWindow(child);
    }
    child = next;
  }

  int nTabs = gBrowser->TabCount();
  int tabW = 140;
  int x = 2;

  // Position + button first, then create tab buttons
  HWND newBtn = GetDlgItem(hwnd, kIdNewTab);
  if (newBtn) {
    SetWindowPos(newBtn, nullptr, x, 0, 28, kTabBarHeight, SWP_NOZORDER);
    x += 30;
  }

  int activeTab = gBrowser->ActiveTab();
  for (int i = 0; i < nTabs && i < 20; i++) {
    std::wstring label = gBrowser->TabTitle(i);
    if (label.empty()) label = L"Tab " + std::to_wstring(i + 1);
    if (label.size() > 16) { label = label.substr(0, 14) + L".."; }

    HWND hBtn = CreateWindowExW(0, L"BUTTON", label.c_str(),
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      x, 0, tabW, kTabBarHeight, hwnd, (HMENU)(INT_PTR)(kIdTabBase + i),
      nullptr, nullptr);
    if (hBtn && gTabFont) {
      SendMessageW(hBtn, WM_SETFONT, (WPARAM)gTabFont, TRUE);
      if (i == activeTab) {
        SetWindowLongPtrW(hBtn, GWL_STYLE, GetWindowLongPtrW(hBtn, GWL_STYLE) | BS_DEFPUSHBUTTON);
        InvalidateRect(hBtn, nullptr, TRUE);
      }
    }
    x += tabW + 2;
  }
}

// -------------------------------------------------------------------
// Resize children
// -------------------------------------------------------------------
static void ResizeChildren(HWND hwnd) {
  if (!gBrowser) return;

  RECT rc;
  GetClientRect(hwnd, &rc);

  // Tab bar is at the top
  // Find the + button
  HWND child = GetWindow(hwnd, GW_CHILD);
  while (child) {
    int id = GetDlgCtrlID(child);
    if (id == kIdNewTab) {
      SetWindowPos(child, nullptr, 0, 0, 28, kTabBarHeight, SWP_NOZORDER);
    }
    child = GetWindow(child, GW_HWNDNEXT);
  }

  // Resize address bar dynamically
  if (gBrowser->addrEdit_) {
    RECT editRc;
    GetWindowRect(gBrowser->addrEdit_, &editRc);
    HWND goBtn = GetDlgItem(hwnd, kIdGoAddr);
    HWND searchBtn = GetDlgItem(hwnd, kIdSearch);
    HWND settingsBtn = GetDlgItem(hwnd, kIdSettingsButton);
    HWND backBtn = GetDlgItem(hwnd, kIdBack);
    HWND fwdBtn = GetDlgItem(hwnd, kIdForward);
    HWND reloadBtn = GetDlgItem(hwnd, kIdReload);

    // Calculate available width
    int x = 6;
    if (backBtn) {
      RECT btnRc;
      GetWindowRect(backBtn, &btnRc);
      // nav buttons start at x=6, each is kNavBtnW + kBtnGap
    }
    int navBtnsWidth = 3 * (kNavBtnW + kBtnGap) + 4; // back+fwd+reload + gap
    int rightBtnsWidth = (kNavBtnW + kBtnGap) * 2 + (kNavBtnW + 6); // search + settings + go
    int addrWidth = rc.right - navBtnsWidth - rightBtnsWidth - 6 - 6;
    if (addrWidth < 200) addrWidth = 200;

    int addrY = kTabBarHeight + kBtnTop;
    SetWindowPos(gBrowser->addrEdit_, nullptr,
      navBtnsWidth, addrY, addrWidth, kBtnH, SWP_NOZORDER);

    if (goBtn) SetWindowPos(goBtn, nullptr,
      navBtnsWidth + addrWidth + kBtnGap, addrY, kNavBtnW + 6, kBtnH, SWP_NOZORDER);
    if (searchBtn) SetWindowPos(searchBtn, nullptr,
      navBtnsWidth + addrWidth + kBtnGap + kNavBtnW + 6 + kBtnGap, addrY, kNavBtnW, kBtnH, SWP_NOZORDER);
    if (settingsBtn) SetWindowPos(settingsBtn, nullptr,
      navBtnsWidth + addrWidth + kBtnGap + kNavBtnW + 6 + kBtnGap + kNavBtnW + kBtnGap, addrY, kNavBtnW, kBtnH, SWP_NOZORDER);
  }

  // Resize webview
  gBrowser->Resize();
}

// -------------------------------------------------------------------
// Brush management
// -------------------------------------------------------------------
static void UpdateToolbarBrush(bool dark) {
  if (gToolbarBrush) DeleteObject(gToolbarBrush);
  gToolbarBrush = CreateSolidBrush(dark ? RGB(50, 50, 55) : RGB(240, 242, 245));
  if (gSiteRowBrush) DeleteObject(gSiteRowBrush);
  gSiteRowBrush = CreateSolidBrush(dark ? RGB(45, 45, 50) : RGB(232, 234, 237));
  InvalidateRect(gMainHwnd, nullptr, TRUE);
}



// ===================================================================
// Window procedure
// ===================================================================
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

      // Tab bar area
      RECT tabRc = {0, 0, rc.right, kTabBarHeight};
      FillRect(dc, &tabRc, (HBRUSH)GetStockObject(WHITE_BRUSH));

      RECT navRc = {0, kTabBarHeight, rc.right, kTabBarHeight + kToolbarHeight};
      FillRect(dc, &navRc, gToolbarBrush ? gToolbarBrush : (HBRUSH)GetStockObject(WHITE_BRUSH));

      RECT siteRc = {0, kTabBarHeight + kToolbarHeight, rc.right, kTabBarHeight + kToolbarHeight + kSiteRowHeight};
      HBRUSH siteBrush = gSiteRowBrush ? gSiteRowBrush : gToolbarBrush;
      FillRect(dc, &siteRc, siteBrush ? siteBrush : (HBRUSH)GetStockObject(WHITE_BRUSH));

      RECT webRc = {0, kTabBarHeight + kToolbarHeight + kSiteRowHeight, rc.right, rc.bottom};
      FillRect(dc, &webRc, (HBRUSH)GetStockObject(WHITE_BRUSH));
      return TRUE;
    }

    case WM_SIZE:
      UpdateTabBar(hwnd);
      ResizeChildren(hwnd);
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        // Navigation
        case kIdBack:
          if (gBrowser) gBrowser->GoBack();
          break;
        case kIdForward:
          if (gBrowser) gBrowser->GoForward();
          break;
        case kIdReload:
          if (gBrowser) gBrowser->Reload();
          break;
        case kIdSearch:
          if (gBrowser) gBrowser->SearchInPage();
          break;

        // Go button
        case kIdGoAddr: {
          if (gBrowser && gBrowser->addrEdit_) {
            wchar_t url[1024];
            GetWindowTextW(gBrowser->addrEdit_, url, 1024);
            std::wstring ws(url);
            if (ws.find(L'.') != std::wstring::npos &&
                ws.find(L' ') == std::wstring::npos &&
                ws.find(L"://") == std::wstring::npos) {
              ws = L"https://" + ws;
            }
            gBrowser->Navigate(ws);
          }
          break;
        }

        // New tab
        case kIdNewTab:
          if (gBrowser) {
            gBrowser->NewTab(L"https://www.google.com");
            UpdateTabBar(hwnd);
          }
          break;
        default:
          // Tab selection
          if (LOWORD(wParam) >= kIdTabBase && LOWORD(wParam) < kIdTabBase + 100) {
            if (gBrowser) {
              int tabIdx = LOWORD(wParam) - kIdTabBase;
              if (tabIdx >= 0 && tabIdx < gBrowser->TabCount()) {
                gBrowser->SwitchTab(tabIdx);
                UpdateTabBar(hwnd);
              }
            }
          }
          break;

        // Site shortcuts
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

    case kMsgCreateTab:
      if (gBrowser) {
        if (lParam == 1) {
          MessageBoxA(nullptr,
            "WebView2 runtime niet gevonden.\n"
            "Installeer van: https://go.microsoft.com/fwlink/p/?LinkId=2124703",
            "Fout", MB_OK);
        } else {
          gBrowser->NewTab(L"https://www.google.com");
          UpdateTabBar(hwnd);
        }
      }
      break;

    case WM_DESTROY:
      if (gToolbarBrush) DeleteObject(gToolbarBrush);
      if (gSiteRowBrush) DeleteObject(gSiteRowBrush);
      PostQuitMessage(0);
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===================================================================
// Main window creation
// ===================================================================
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

// ===================================================================
// Entry point
// ===================================================================
int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE, LPSTR, int) {
  HWND hwnd = CreateMainWindow(hInstance);

  ACCEL accel[] = {
    { FCONTROL, 'R', kIdAccelReload },
  };
  HACCEL hAccel = CreateAcceleratorTableW(accel, 1);

  BrowserWindow browser;
  gBrowser = &browser;
  // Find the address bar edit control and store it
  gBrowser->addrEdit_ = GetDlgItem(hwnd, kIdAddrEdit);
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
