#include "browser_window.h"
#include "main.h"
#include <fstream>
#include <sstream>
#include <vector>

// -------------------------------------------------------------------
// UTF-8 / Wide helpers
// -------------------------------------------------------------------
static std::string WideToUTF8(const wchar_t* w) {
  if (!w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n) - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

static std::wstring UTF8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n) - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

static std::wstring FormatDouble(double d) {
  wchar_t buf[64];
  swprintf_s(buf, L"%.15g", d);
  return buf;
}

static double ParseDouble(const std::wstring& s) {
  return wcstod(s.c_str(), nullptr);
}

// -------------------------------------------------------------------
// Base64
// -------------------------------------------------------------------
static const char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string Base64Encode(const std::string& in) {
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(kBase64[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(kBase64[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

static std::string Base64Decode(const std::string& in) {
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++)
    T[static_cast<unsigned char>(kBase64[i])] = i;
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (c == '=') break;
    if (T[c] == -1) continue;
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

// -------------------------------------------------------------------
// Path helpers
// -------------------------------------------------------------------
static std::wstring GetCookiePath() {
  wchar_t buf[MAX_PATH];
  GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
  wcscat_s(buf, L"\\Nara");
  CreateDirectoryW(buf, nullptr);
  wcscat_s(buf, L"\\cookies.txt");
  return buf;
}

static std::wstring GetPasswordPath() {
  wchar_t buf[MAX_PATH];
  GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
  wcscat_s(buf, L"\\Nara");
  CreateDirectoryW(buf, nullptr);
  wcscat_s(buf, L"\\passwords.txt");
  return buf;
}

// -------------------------------------------------------------------
// GetCookies callback for export
// -------------------------------------------------------------------
struct ExportHandler : ICoreWebView2GetCookiesCompletedHandler {
  std::wstring filePath;
  ExportHandler(const std::wstring& fp) : filePath(fp) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(HRESULT errorCode, ICoreWebView2CookieList* result) override {
    if (FAILED(errorCode) || !result) return errorCode;
    UINT32 count = 0;
    result->get_Count(&count);

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return S_OK;

    std::string buf;
    for (UINT32 i = 0; i < count; ++i) {
      ICoreWebView2Cookie* c = nullptr;
      result->GetValueAtIndex(i, &c);
      if (!c) continue;

      LPWSTR name = nullptr, value = nullptr, domain = nullptr, path = nullptr;
      double expires = 0;
      BOOL isSecure = FALSE, isHttpOnly = FALSE, isSession = FALSE;
      COREWEBVIEW2_COOKIE_SAME_SITE_KIND sameSite = COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE;

      c->get_Name(&name);
      c->get_Value(&value);
      c->get_Domain(&domain);
      c->get_Path(&path);
      c->get_Expires(&expires);
      c->get_IsSecure(&isSecure);
      c->get_IsHttpOnly(&isHttpOnly);
      c->get_IsSession(&isSession);
      c->get_SameSite(&sameSite);

      buf += WideToUTF8(name) + '\x01';
      buf += WideToUTF8(value) + '\x01';
      buf += WideToUTF8(domain) + '\x01';
      buf += WideToUTF8(path) + '\x01';
      buf += WideToUTF8(FormatDouble(expires).c_str()) + '\x01';
      buf += (isSecure ? "1" : "0") + std::string("\x01");
      buf += (isHttpOnly ? "1" : "0") + std::string("\x01");
      buf += std::to_string(static_cast<int>(sameSite)) + '\x01';
      buf += (isSession ? "1" : "0") + std::string("\n");

      CoTaskMemFree(name);
      CoTaskMemFree(value);
      CoTaskMemFree(domain);
      CoTaskMemFree(path);
      c->Release();
    }

    DWORD written = 0;
    WriteFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr);
    CloseHandle(hFile);
    return S_OK;
  }
};

// -------------------------------------------------------------------
// Environment created → create first tab
// -------------------------------------------------------------------
struct EnvHandler : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
  BrowserWindow* bw;
  EnvHandler(BrowserWindow* b) : bw(b) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(HRESULT hr, ICoreWebView2Environment* env) override;
};

// -------------------------------------------------------------------
// Tab controller creation completed
// -------------------------------------------------------------------
struct TabCtrlHandler : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
  BrowserWindow* bw;
  int tabIndex;
  TabCtrlHandler(BrowserWindow* b, int idx) : bw(b), tabIndex(idx) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(HRESULT hr, ICoreWebView2Controller* controller) override;
};

// -------------------------------------------------------------------
// Navigation completed → update address bar + title
// -------------------------------------------------------------------
struct NavCompletedHandler : ICoreWebView2NavigationCompletedEventHandler {
  BrowserWindow* bw;
  int tabIndex;
  NavCompletedHandler(BrowserWindow* b, int idx) : bw(b), tabIndex(idx) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) override {
    if (tabIndex < 0 || tabIndex >= (int)bw->tabs_.size()) return S_OK;
    auto& tab = bw->tabs_[tabIndex];
    if (!tab.webview) return S_OK;

    LPWSTR src = nullptr;
    tab.webview->get_Source(&src);
    if (src) {
      tab.url = src;
      CoTaskMemFree(src);
    }

    LPWSTR title = nullptr;
    ICoreWebView2_5* wv5 = nullptr;
    if (SUCCEEDED(tab.webview->QueryInterface(IID_ICoreWebView2_5, (void**)&wv5)) && wv5) {
      wv5->get_DocumentTitle(&title);
      wv5->Release();
    }
    if (title) {
      tab.title = title;
      CoTaskMemFree(title);
    }

    tab.isLoading = false;

    if (tabIndex == bw->activeTab_) {
      if (bw->addrEdit_) {
        SetWindowTextW(bw->addrEdit_, tab.url.c_str());
      }
      SetWindowTextW(bw->GetParent(), tab.title.empty() ? L"Nara" : tab.title.c_str());
      // Trigger tab bar update
      PostMessageW(bw->GetParent(), WM_SIZE, 0, 0);
    }
    return S_OK;
  }
};

// -------------------------------------------------------------------
// Source changed → update URL while typing
// -------------------------------------------------------------------
struct SrcChangedHandler : ICoreWebView2SourceChangedEventHandler {
  BrowserWindow* bw;
  int tabIndex;
  SrcChangedHandler(BrowserWindow* b, int idx) : bw(b), tabIndex(idx) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(ICoreWebView2*, ICoreWebView2SourceChangedEventArgs*) override {
    if (tabIndex < 0 || tabIndex >= (int)bw->tabs_.size()) return S_OK;
    auto& tab = bw->tabs_[tabIndex];
    if (!tab.webview) return S_OK;
    LPWSTR src = nullptr;
    tab.webview->get_Source(&src);
    if (src) {
      tab.url = src;
      CoTaskMemFree(src);
    }
    if (tabIndex == bw->activeTab_ && bw->addrEdit_) {
      SetWindowTextW(bw->addrEdit_, tab.url.c_str());
    }
    return S_OK;
  }
};

// -------------------------------------------------------------------
// EnvHandler::Invoke
// -------------------------------------------------------------------
HRESULT EnvHandler::Invoke(HRESULT hr, ICoreWebView2Environment* env) {
  if (FAILED(hr) || !env) {
    PostMessageW(bw->parent_, kMsgCreateTab, 0, 1); // 1 = error
    return hr;
  }
  bw->env_ = env;
  bw->env_->AddRef();
  PostMessageW(bw->parent_, kMsgCreateTab, 0, 0); // 0 = ok
  return S_OK;
}

// -------------------------------------------------------------------
// TabCtrlHandler::Invoke
// -------------------------------------------------------------------
HRESULT TabCtrlHandler::Invoke(HRESULT hr, ICoreWebView2Controller* controller) {
  if (FAILED(hr) || !controller) {
    if (tabIndex < (int)bw->tabs_.size())
      bw->tabs_.erase(bw->tabs_.begin() + tabIndex);
    if (tabIndex <= bw->activeTab_ && bw->activeTab_ > 0)
      bw->activeTab_--;
    return hr;
  }

  if (tabIndex >= (int)bw->tabs_.size()) return E_FAIL;
  TabInfo& tab = bw->tabs_[tabIndex];
  tab.controller = controller;
  tab.controller->AddRef();
  controller->get_CoreWebView2(&tab.webview);

  // Only show if active
  controller->put_IsVisible(tabIndex == bw->activeTab_ ? TRUE : FALSE);

  // Settings
  ICoreWebView2Settings* settings = nullptr;
  if (SUCCEEDED(tab.webview->get_Settings(&settings)) && settings) {
    settings->put_IsScriptEnabled(TRUE);
    settings->put_AreDefaultScriptDialogsEnabled(TRUE);
    settings->put_IsWebMessageEnabled(TRUE);
    settings->Release();
  }

  EventRegistrationToken navToken, srcToken;
  tab.webview->add_NavigationCompleted(new NavCompletedHandler(bw, tabIndex), &navToken);
  tab.webview->add_SourceChanged(new SrcChangedHandler(bw, tabIndex), &srcToken);

  // Apply theme
  if (bw->isDark_) {
    ICoreWebView2Controller2* ctrl2 = nullptr;
    if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2)) && ctrl2) {
      COREWEBVIEW2_COLOR bg = {30, 30, 30, 255};
      ctrl2->put_DefaultBackgroundColor(bg);
      ctrl2->Release();
    }
    ICoreWebView2_13* wv13 = nullptr;
    if (SUCCEEDED(tab.webview->QueryInterface(IID_ICoreWebView2_13, (void**)&wv13)) && wv13) {
      ICoreWebView2Profile* profile = nullptr;
      if (SUCCEEDED(wv13->get_Profile(&profile)) && profile) {
        profile->put_PreferredColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK);
        profile->Release();
      }
      wv13->Release();
    }
  }

  bw->NavigateTab(tabIndex, tab.url.empty() ? L"https://www.google.com" : tab.url);
  bw->Resize();
  return S_OK;
}

// ===================================================================
// BrowserWindow
// ===================================================================

BrowserWindow::BrowserWindow() : env_(nullptr) {}

BrowserWindow::~BrowserWindow() {
  for (auto& t : tabs_) {
    if (t.controller) t.controller->Release();
  }
  if (env_) env_->Release();
}

bool BrowserWindow::Initialize(HWND parent) {
  parent_ = parent;
  EnvHandler* handler = new EnvHandler(this);
  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, handler);
  return SUCCEEDED(hr);
}

ICoreWebView2* BrowserWindow::WebView() const {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return nullptr;
  return tabs_[activeTab_].webview;
}

TabInfo& BrowserWindow::ActiveTabRef() {
  static TabInfo empty = {};
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return empty;
  return tabs_[activeTab_];
}

// -------------------------------------------------------------------
// Tab management
// -------------------------------------------------------------------
void BrowserWindow::NewTab(const std::wstring& url) {
  TabInfo tab;
  tab.url = url;
  tab.title = L"Nieuwe tab";
  tab.isLoading = true;
  tabs_.push_back(tab);

  int idx = (int)tabs_.size() - 1;
  SwitchTab(idx);

  // Create controller directly on the main parent window
  env_->CreateCoreWebView2Controller(parent_, new TabCtrlHandler(this, idx));

  PostMessageW(parent_, WM_SIZE, 0, 0);
}

void BrowserWindow::CloseTab(int idx) {
  if (tabs_.size() <= 1) return;
  if (idx < 0 || idx >= (int)tabs_.size()) return;

  if (tabs_[idx].controller) tabs_[idx].controller->Release();

  HRESULT (__stdcall* closeFn)(void) = nullptr;
  if (tabs_[idx].controller) {
    tabs_[idx].controller->Close();
  }

  tabs_.erase(tabs_.begin() + idx);
  if (activeTab_ >= (int)tabs_.size()) activeTab_ = (int)tabs_.size() - 1;
  if (activeTab_ > idx) activeTab_--;
  SwitchTab(activeTab_);
}

void BrowserWindow::SwitchTab(int idx) {
  if (idx < 0 || idx >= (int)tabs_.size()) return;

  if (activeTab_ >= 0 && activeTab_ < (int)tabs_.size()) {
    TabInfo& old = tabs_[activeTab_];
    if (old.controller) old.controller->put_IsVisible(FALSE);
  }

  activeTab_ = idx;
  TabInfo& tab = tabs_[idx];
  if (tab.controller) {
    tab.controller->put_IsVisible(TRUE);
    tab.controller->put_Bounds(CalcWebViewRect());

    // Update address bar
    if (addrEdit_) {
      SetWindowTextW(addrEdit_, tab.url.c_str());
    }
    SetWindowTextW(parent_, tab.title.empty() ? L"Nara" : tab.title.c_str());
  }

  PostMessageW(parent_, WM_SIZE, 0, 0);
}

RECT BrowserWindow::CalcWebViewRect() {
  RECT rc;
  GetClientRect(parent_, &rc);
  int topY = kTabBarHeight + kToolbarHeight + kSiteRowHeight;
  rc.top += topY;
  return rc;
}

// -------------------------------------------------------------------
// Navigation
// -------------------------------------------------------------------
void BrowserWindow::Navigate(const std::wstring& url) {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  tab.url = url;
  if (tab.webview) {
    tab.webview->Navigate(url.c_str());
  }
}

void BrowserWindow::Navigate(const std::string& url) {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  int len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  std::wstring wurl(static_cast<size_t>(len) - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], len);
  tab.url = wurl;
  if (tab.webview) {
    tab.webview->Navigate(wurl.c_str());
  }
}

void BrowserWindow::NavigateTab(int idx, const std::wstring& url) {
  if (idx < 0 || idx >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[idx];
  tab.url = url;
  if (tab.webview) {
    tab.webview->Navigate(url.c_str());
  }
}

void BrowserWindow::GoBack() {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  if (tab.webview) tab.webview->GoBack();
}

void BrowserWindow::GoForward() {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  if (tab.webview) tab.webview->GoForward();
}

void BrowserWindow::Reload() {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  if (tab.webview) tab.webview->Reload();
}

// -------------------------------------------------------------------
// Search in page
// -------------------------------------------------------------------
void BrowserWindow::SearchInPage() {
  if (!WebView()) return;
  wchar_t text[256] = {};
  if (DialogBoxParamW((HINSTANCE)GetWindowLongPtrW(parent_, GWLP_HINSTANCE),
      L"FIND_IN_PAGE", parent_, nullptr, 0)) {
    // Actually let's do a simple input dialog via JavaScript prompt
  }
  // Simpler: use a Windows input dialog
  wchar_t searchText[256] = {};
  // Use the existing edit control or prompt
  // For simplicity, use the address bar as search term input
  // Actually, let's do it properly with Prompt
  const wchar_t* script =
    L"(function(){"
    L"  var t = prompt('Zoek op pagina:');"
    L"  if (t && t.length > 0) {"
    L"    window.find(t, false, false, true);"
    L"  }"
    L"})()";
  WebView()->ExecuteScript(script, nullptr);
}

// -------------------------------------------------------------------
// Resize
// -------------------------------------------------------------------
void BrowserWindow::Resize() {
  if (activeTab_ < 0 || activeTab_ >= (int)tabs_.size()) return;
  TabInfo& tab = tabs_[activeTab_];
  if (!tab.controller) return;
  RECT rc = CalcWebViewRect();
  tab.controller->put_Bounds(rc);
}

// -------------------------------------------------------------------
// Cookie export
// -------------------------------------------------------------------
void BrowserWindow::ExportCookies() {
  if (!WebView()) return;
  ICoreWebView2_2* wv2 = nullptr;
  HRESULT hr = WebView()->QueryInterface(IID_ICoreWebView2_2, (void**)&wv2);
  if (FAILED(hr) || !wv2) {
    MessageBoxA(nullptr, "Kan CookieManager niet openen.", "Fout", MB_OK);
    return;
  }
  ICoreWebView2CookieManager* mgr = nullptr;
  wv2->get_CookieManager(&mgr);
  wv2->Release();
  if (!mgr) return;

  mgr->GetCookies(L"", new ExportHandler(GetCookiePath()));
  mgr->Release();
  MessageBoxA(nullptr, "Cookies geexporteerd naar cookies.txt", "Export", MB_OK);
}

// -------------------------------------------------------------------
// Cookie import
// -------------------------------------------------------------------
void BrowserWindow::ImportCookies() {
  if (!WebView()) return;
  ICoreWebView2_2* wv2 = nullptr;
  HRESULT hr = WebView()->QueryInterface(IID_ICoreWebView2_2, (void**)&wv2);
  if (FAILED(hr) || !wv2) {
    MessageBoxA(nullptr, "Kan CookieManager niet openen.", "Fout", MB_OK);
    return;
  }
  ICoreWebView2CookieManager* mgr = nullptr;
  wv2->get_CookieManager(&mgr);
  wv2->Release();
  if (!mgr) return;

  std::wstring path = GetCookiePath();
  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    MessageBoxA(nullptr, "Geen cookies.txt gevonden.", "Import", MB_OK);
    mgr->Release();
    return;
  }

  DWORD size = GetFileSize(hFile, nullptr);
  std::string data(static_cast<size_t>(size), '\0');
  DWORD read = 0;
  ReadFile(hFile, &data[0], size, &read, nullptr);
  CloseHandle(hFile);

  if (data.empty() || data.back() != '\n') data.push_back('\n');

  size_t pos = 0;
  UINT32 imported = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) break;
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (line.empty()) continue;

    std::vector<std::string> fields;
    size_t start = 0;
    while (start < line.size()) {
      size_t sep = line.find('\x01', start);
      if (sep == std::string::npos) {
        fields.push_back(line.substr(start));
        break;
      }
      fields.push_back(line.substr(start, sep - start));
      start = sep + 1;
    }

    if (fields.size() < 9) continue;

    ICoreWebView2Cookie* c = nullptr;
    HRESULT hr2 = mgr->CreateCookie(
        UTF8ToWide(fields[0]).c_str(),
        UTF8ToWide(fields[1]).c_str(),
        UTF8ToWide(fields[2]).c_str(),
        UTF8ToWide(fields[3]).c_str(),
        &c);
    if (FAILED(hr2) || !c) continue;

    c->put_IsSecure(fields[5] == "1" ? TRUE : FALSE);
    c->put_IsHttpOnly(fields[6] == "1" ? TRUE : FALSE);
    c->put_SameSite(static_cast<COREWEBVIEW2_COOKIE_SAME_SITE_KIND>(std::stoi(fields[7])));
    c->put_Expires(ParseDouble(UTF8ToWide(fields[4])));

    mgr->AddOrUpdateCookie(c);
    c->Release();
    ++imported;
  }

  mgr->Release();

  char msg[64];
  snprintf(msg, sizeof(msg), "%u cookies geimporteerd.", imported);
  MessageBoxA(nullptr, msg, "Import", MB_OK);
}

// -------------------------------------------------------------------
// Theme toggle
// -------------------------------------------------------------------
void BrowserWindow::ToggleTheme() {
  isDark_ = !isDark_;
  for (auto& tab : tabs_) {
    if (!tab.controller) continue;
    if (!tab.webview) continue;

    ICoreWebView2Controller2* ctrl2 = nullptr;
    if (SUCCEEDED(tab.controller->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2)) && ctrl2) {
      COREWEBVIEW2_COLOR bg;
      if (isDark_) {
        bg.R = 30; bg.G = 30; bg.B = 30; bg.A = 255;
      } else {
        bg.R = 255; bg.G = 255; bg.B = 255; bg.A = 255;
      }
      ctrl2->put_DefaultBackgroundColor(bg);
      ctrl2->Release();
    }

    ICoreWebView2_13* wv13 = nullptr;
    if (SUCCEEDED(tab.webview->QueryInterface(IID_ICoreWebView2_13, (void**)&wv13)) && wv13) {
      ICoreWebView2Profile* profile = nullptr;
      if (SUCCEEDED(wv13->get_Profile(&profile)) && profile) {
        profile->put_PreferredColorScheme(
            isDark_ ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                    : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
        profile->Release();
      }
      wv13->Release();
    }

    const wchar_t* script = isDark_
      ? LR"(document.documentElement.style.backgroundColor='#1e1e1e';
try {
  let s=document.createElement('style');
  s.id='__mydark';
  s.textContent='html{filter:invert(.88)hue-rotate(180deg)}img,video,canvas,svg{filter:invert(1)hue-rotate(180deg)}';
  document.head.appendChild(s);
}catch(e){})"
      : LR"(try{
  let s=document.getElementById('__mydark');
  if(s)s.remove();
  document.documentElement.style.backgroundColor='';
}catch(e){})";

    tab.webview->ExecuteScript(script, nullptr);
  }
}

// -------------------------------------------------------------------
// Password Manager
// -------------------------------------------------------------------

static std::wstring ExtractDomain(const std::wstring& url) {
  size_t start = url.find(L"://");
  if (start == std::wstring::npos) start = 0;
  else start += 3;
  size_t end = url.find(L'/', start);
  if (end == std::wstring::npos) return url.substr(start);
  return url.substr(start, end - start);
}

struct PmSaveHandler : ICoreWebView2ExecuteScriptCompletedHandler {
  BrowserWindow* bw;
  std::wstring domain;
  PmSaveHandler(BrowserWindow* b, const std::wstring& d) : bw(b), domain(d) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(HRESULT, LPCWSTR resultObjectAsJson) override {
    if (!resultObjectAsJson) return S_OK;
    std::string json = WideToUTF8(resultObjectAsJson);
    if (json.size() < 2) return S_OK;
    std::string raw = json.substr(1, json.size() - 2);
    size_t sep = raw.find("|||");
    if (sep == std::string::npos || sep == 0 || sep == raw.size() - 3) {
      MessageBoxA(nullptr, "Geen inlogformulier gevonden op deze pagina.", "Wachtwoord", MB_OK);
      return S_OK;
    }
    std::string user = raw.substr(0, sep);
    std::string pass = raw.substr(sep + 3);
    if (user.empty() || pass.empty()) {
      MessageBoxA(nullptr, "Vul eerst gebruikersnaam en wachtwoord in.", "Wachtwoord", MB_OK);
      return S_OK;
    }

    std::wstring pwPath = GetPasswordPath();
    std::string domainA = WideToUTF8(domain.c_str());
    std::string encodedDomain = Base64Encode(domainA);
    std::string encodedUser = Base64Encode(user);
    std::string encodedPass = Base64Encode(pass);

    std::vector<std::string> lines;
    HANDLE hFile = CreateFileW(pwPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
      DWORD sz = GetFileSize(hFile, nullptr);
      if (sz > 0) {
        std::string existing(static_cast<size_t>(sz), '\0');
        DWORD rd = 0;
        ReadFile(hFile, &existing[0], sz, &rd, nullptr);
        size_t p = 0;
        while (p < existing.size()) {
          size_t nl = existing.find('\n', p);
          if (nl == std::string::npos) break;
          lines.push_back(existing.substr(p, nl - p));
          p = nl + 1;
        }
      }
      CloseHandle(hFile);
    }

    bool updated = false;
    std::string newLine = encodedDomain + '\x01' + encodedUser + '\x01' + encodedPass;
    for (auto& line : lines) {
      size_t firstSep = line.find('\x01');
      if (firstSep != std::string::npos && line.substr(0, firstSep) == encodedDomain) {
        line = newLine;
        updated = true;
        break;
      }
    }
    if (!updated) lines.push_back(newLine);

    hFile = CreateFileW(pwPath.c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
      std::string out;
      for (const auto& line : lines) out += line + '\n';
      DWORD written = 0;
      WriteFile(hFile, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
      CloseHandle(hFile);
      MessageBoxA(nullptr, "Wachtwoord opgeslagen!", "Wachtwoord", MB_OK);
    }
    return S_OK;
  }
};

void BrowserWindow::SavePassword() {
  if (!WebView()) return;
  LPWSTR src = nullptr;
  WebView()->get_Source(&src);
  std::wstring domain = src ? ExtractDomain(src) : L"unknown";
  CoTaskMemFree(src);

  const wchar_t* script =
    L"(function(){"
    L"var f=document.querySelector('form');"
    L"if(!f)return '|||';"
    L"var inputs=f.querySelectorAll('input');"
    L"var user='',pass='';"
    L"for(var i=0;i<inputs.length;i++){"
    L"var inp=inputs[i];"
    L"if(inp.type==='password')pass=inp.value;"
    L"else if(inp.type==='email'||inp.type==='text'){"
    L"if(!user)user=inp.value;"
    L"}"
    L"}"
    L"return user+'|||'+pass;"
    L"})()";

  WebView()->ExecuteScript(script, new PmSaveHandler(this, domain));
}

struct PmAutofillHandler : ICoreWebView2ExecuteScriptCompletedHandler {
  BrowserWindow* bw;
  std::wstring user;
  std::wstring pass;
  PmAutofillHandler(BrowserWindow* b, const std::wstring& u, const std::wstring& p)
    : bw(b), user(u), pass(p) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override { *ppv = nullptr; return E_NOINTERFACE; }
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(Invoke)(HRESULT, LPCWSTR) override { return S_OK; }
};

void BrowserWindow::AutoFillPassword() {
  if (!WebView()) return;
  LPWSTR src = nullptr;
  WebView()->get_Source(&src);
  std::wstring domain = src ? ExtractDomain(src) : L"unknown";
  CoTaskMemFree(src);

  std::wstring pwPath = GetPasswordPath();
  std::string domainA = WideToUTF8(domain.c_str());
  std::string encodedDomain = Base64Encode(domainA);

  HANDLE hFile = CreateFileW(pwPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    MessageBoxA(nullptr, "Geen opgeslagen wachtwoorden.", "Auto-fill", MB_OK);
    return;
  }

  DWORD sz = GetFileSize(hFile, nullptr);
  std::string data(static_cast<size_t>(sz), '\0');
  DWORD rd = 0;
  ReadFile(hFile, &data[0], sz, &rd, nullptr);
  CloseHandle(hFile);

  bool found = false;
  std::string userB64, passB64;
  size_t pos = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) break;
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (line.empty()) continue;

    size_t s1 = line.find('\x01');
    if (s1 == std::string::npos) continue;
    size_t s2 = line.find('\x01', s1 + 1);
    if (s2 == std::string::npos) continue;

    std::string dom = line.substr(0, s1);
    if (dom == encodedDomain) {
      userB64 = line.substr(s1 + 1, s2 - s1 - 1);
      passB64 = line.substr(s2 + 1);
      found = true;
      break;
    }
  }

  if (!found) {
    MessageBoxA(nullptr, "Geen wachtwoord voor deze site.", "Auto-fill", MB_OK);
    return;
  }

  std::string userDecoded = Base64Decode(userB64);
  std::string passDecoded = Base64Decode(passB64);
  std::wstring wuser = UTF8ToWide(userDecoded);
  std::wstring wpass = UTF8ToWide(passDecoded);

  auto escape = [](std::wstring& s) {
    size_t p = 0;
    while ((p = s.find(L'\'', p)) != std::wstring::npos) {
      s.insert(p, 1, L'\\');
      p += 2;
    }
  };
  escape(wuser);
  escape(wpass);

  std::wstring script = L"(function(u,p){"
    L"var f=document.querySelector('form');"
    L"if(!f)return;"
    L"var inputs=f.querySelectorAll('input');"
    L"var fu=false,fp=false;"
    L"for(var i=0;i<inputs.length;i++){"
    L"var inp=inputs[i];"
    L"if(inp.type==='password'&&!fp){inp.value=p;fp=true;}"
    L"else if((inp.type==='email'||inp.type==='text')&&!fu){inp.value=u;fu=true;}"
    L"else if(inp.type==='email'||inp.type==='text'){inp.value=u;}"
    L"}"
    L"})('" + wuser + L"','" + wpass + L"')";

  WebView()->ExecuteScript(script.c_str(), new PmAutofillHandler(this, wuser, wpass));
  MessageBoxA(nullptr, "Auto-fill uitgevoerd.", "Auto-fill", MB_OK);
}

void BrowserWindow::ViewPasswords() {
  std::wstring pwPath = GetPasswordPath();
  HANDLE hFile = CreateFileW(pwPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    MessageBoxA(nullptr, "Geen opgeslagen wachtwoorden.", "Wachtwoorden", MB_OK);
    return;
  }

  DWORD sz = GetFileSize(hFile, nullptr);
  std::string data(static_cast<size_t>(sz), '\0');
  DWORD rd = 0;
  ReadFile(hFile, &data[0], sz, &rd, nullptr);
  CloseHandle(hFile);

  std::string display;
  size_t pos = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) break;
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (line.empty()) continue;

    size_t s1 = line.find('\x01');
    if (s1 == std::string::npos) continue;
    size_t s2 = line.find('\x01', s1 + 1);
    if (s2 == std::string::npos) continue;

    std::string domB64 = line.substr(0, s1);
    std::string userB64 = line.substr(s1 + 1, s2 - s1 - 1);
    std::string passB64 = line.substr(s2 + 1);

    std::string domain = Base64Decode(domB64);
    std::string user = Base64Decode(userB64);
    std::string pass = Base64Decode(passB64);

    display += domain + " | " + user + " | " + pass + "\n";
  }

  if (display.empty()) {
    MessageBoxA(nullptr, "Geen opgeslagen wachtwoorden.", "Wachtwoorden", MB_OK);
    return;
  }

  MessageBoxA(nullptr, display.c_str(), "Opgeslagen Wachtwoorden", MB_OK);
}

void BrowserWindow::ClearPasswords() {
  std::wstring pwPath = GetPasswordPath();
  if (DeleteFileW(pwPath.c_str())) {
    MessageBoxA(nullptr, "Alle wachtwoorden gewist.", "Wachtwoorden", MB_OK);
  } else {
    MessageBoxA(nullptr, "Geen wachtwoorden om te wissen.", "Wachtwoorden", MB_OK);
  }
}

// -------------------------------------------------------------------
// Bookmarks
// -------------------------------------------------------------------
static std::wstring GetBookmarkPath() {
  wchar_t buf[MAX_PATH];
  GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
  wcscat_s(buf, L"\\Nara");
  CreateDirectoryW(buf, nullptr);
  wcscat_s(buf, L"\\bookmarks.txt");
  return buf;
}

void BrowserWindow::AddBookmark() {
  if (!WebView()) return;
  LPWSTR url = nullptr;
  WebView()->get_Source(&url);
  if (!url) return;

  LPWSTR title = nullptr;
  ICoreWebView2_5* wv5 = nullptr;
  if (SUCCEEDED(WebView()->QueryInterface(IID_ICoreWebView2_5, (void**)&wv5)) && wv5) {
    wv5->get_DocumentTitle(&title);
    wv5->Release();
  }
  if (!title) { title = (LPWSTR)CoTaskMemAlloc(2); title[0] = 0; }

  std::wstring path = GetBookmarkPath();
  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    SetFilePointer(hFile, 0, nullptr, FILE_END);
    std::string line = WideToUTF8(title) + "\x01" + WideToUTF8(url) + "\n";
    DWORD written = 0;
    WriteFile(hFile, line.data(), (DWORD)line.size(), &written, nullptr);
    CloseHandle(hFile);
    MessageBoxA(nullptr, "Bookmark toegevoegd!", "Bookmark", MB_OK);
  }
  CoTaskMemFree(url);
  CoTaskMemFree(title);
}

void BrowserWindow::ShowBookmarks() {
  std::wstring path = GetBookmarkPath();
  HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    MessageBoxA(nullptr, "Geen bookmarks.", "Bookmarks", MB_OK);
    return;
  }
  DWORD sz = GetFileSize(hFile, nullptr);
  std::string data((size_t)sz, '\0');
  DWORD rd = 0;
  ReadFile(hFile, &data[0], sz, &rd, nullptr);
  CloseHandle(hFile);

  std::string display;
  int idx = 0;
  size_t pos = 0;
  while (pos < data.size()) {
    size_t nl = data.find('\n', pos);
    if (nl == std::string::npos) break;
    std::string line = data.substr(pos, nl - pos);
    pos = nl + 1;
    if (line.empty()) continue;
    size_t sep = line.find('\x01');
    if (sep == std::string::npos) continue;
    display += std::to_string(++idx) + ". " + line.substr(0, sep) + "\n";
    display += "   " + line.substr(sep + 1) + "\n";
  }
  if (display.empty()) display = "Geen bookmarks.";
  MessageBoxA(nullptr, display.c_str(), "Bookmarks", MB_OK);
}

// -------------------------------------------------------------------
// Downloads
// -------------------------------------------------------------------
void BrowserWindow::ShowDownloads() {
  MessageBoxA(nullptr, "Downloads komen binnenkort!", "Downloads", MB_OK);
}

// -------------------------------------------------------------------
// Site dark mode toggle
// -------------------------------------------------------------------
void BrowserWindow::ToggleSiteDark() {
  if (!WebView()) return;
  siteDark_ = !siteDark_;
  const wchar_t* script = siteDark_
    ? LR"(try{
let s=document.createElement('style');
s.id='__naradark';
s.textContent='html{filter:invert(.88)hue-rotate(180deg)}img,video,canvas,svg{filter:invert(1)hue-rotate(180deg)}*{background-color:transparent!important}';
document.head.appendChild(s);
}catch(e){})"
    : LR"(try{
let s=document.getElementById('__naradark');
if(s)s.remove();
}catch(e){})";
  WebView()->ExecuteScript(script, nullptr);
}

// -------------------------------------------------------------------
// Multi-browser cookie import (SQLite parser)
// -------------------------------------------------------------------

static uint64_t ReadVarint(const uint8_t*& p) {
  uint64_t v = 0;
  for (int i = 0; i < 9; i++) {
    v = (v << 7) | (*p & 0x7F);
    if (!(*p++ & 0x80)) break;
  }
  return v;
}

static UINT32 ParseSQLiteCookies(const std::vector<uint8_t>& data,
                                  uint32_t pageSize,
                                  ICoreWebView2CookieManager* mgr,
                                  const char* tableName);

static UINT32 ImportCookiesFromFile(ICoreWebView2CookieManager* mgr,
                                     const wchar_t* filePath,
                                     const char* tableName) {
  HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    DWORD fsize = GetFileSize(hFile, nullptr);
    if (fsize < 100) { CloseHandle(hFile); return UINT32_MAX; }
    std::vector<uint8_t> data(fsize);
    DWORD rd = 0;
    ReadFile(hFile, data.data(), fsize, &rd, nullptr);
    CloseHandle(hFile);
    return ParseSQLiteCookies(data, 0, mgr, tableName);
  }

  wchar_t tmpPath[MAX_PATH];
  GetTempPathW(MAX_PATH, tmpPath);
  wcscat_s(tmpPath, L"nara_cookies_import.tmp");
  if (!CopyFileW(filePath, tmpPath, FALSE)) return UINT32_MAX;
  hFile = CreateFileW(tmpPath, GENERIC_READ, FILE_SHARE_READ,
                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) { DeleteFileW(tmpPath); return UINT32_MAX; }
  DWORD fsize = GetFileSize(hFile, nullptr);
  if (fsize < 100) { CloseHandle(hFile); DeleteFileW(tmpPath); return UINT32_MAX; }
  std::vector<uint8_t> data(fsize);
  DWORD rd = 0;
  ReadFile(hFile, data.data(), fsize, &rd, nullptr);
  CloseHandle(hFile);
  DeleteFileW(tmpPath);
  return ParseSQLiteCookies(data, 0, mgr, tableName);
}

static UINT32 ParseSQLiteCookies(const std::vector<uint8_t>& data,
                                  uint32_t pageSize,
                                  ICoreWebView2CookieManager* mgr,
                                  const char* tableName) {
  if (data.size() < 100) return 0;
  if (memcmp(data.data(), "SQLite format 3\0", 16) != 0) return 0;

  if (pageSize == 0) {
    pageSize = *(uint16_t*)(data.data() + 16);
    pageSize = (pageSize << 8) | (pageSize >> 8);
    if (pageSize == 1) pageSize = 65536;
  }

  struct FindTable {
    const std::vector<uint8_t>& data;
    uint32_t pageSize;
    const char* tableName;
    size_t tableLen;
    uint32_t result = 0;

    void SearchPage(uint32_t pg) {
      if (pg == 0 || (pg - 1) * pageSize >= data.size()) return;
      const uint8_t* p = data.data() + (pg == 1 ? 0 : (pg - 1) * pageSize);
      uint8_t type = p[0];
      uint16_t cells = *(uint16_t*)(p + 3);
      cells = (cells >> 8) | (cells << 8);

      auto checkRow = [&](const uint8_t* cell) -> bool {
        const uint8_t* cp = cell;
        ReadVarint(cp); ReadVarint(cp); ReadVarint(cp);
        uint64_t ser[5];
        for (int j = 0; j < 5; j++) ser[j] = ReadVarint(cp);
        if (ser[0] >= 12) cp += (ser[0] - 12) / 2;
        if (ser[1] >= 12) cp += (ser[1] - 12) / 2;
        if (ser[2] >= 12) {
          size_t len = (ser[2] - 12) / 2;
          if (len == tableLen && memcmp(cp, tableName, len) == 0) {
            cp += len;
            if (ser[3] == 4) result = (cp[0]<<24)|(cp[1]<<16)|(cp[2]<<8)|cp[3];
            else if (ser[3] == 3) result = (cp[0]<<16)|(cp[1]<<8)|cp[2];
            else if (ser[3] == 2) result = (cp[0]<<8)|cp[1];
            else if (ser[3] == 1) result = cp[0];
            return true;
          }
        }
        return false;
      };

      if (type == 0x0D) {
        uint16_t* cellPtrs = (uint16_t*)(p + 8);
        for (int i = 0; i < cells; i++) {
          uint16_t off = (cellPtrs[i] >> 8) | (cellPtrs[i] << 8);
          if (checkRow(p + off)) return;
        }
      } else if (type == 0x05) {
        uint16_t* cellPtrs = (uint16_t*)(p + 8);
        for (int i = 0; i < cells; i++) {
          uint16_t off = (cellPtrs[i] >> 8) | (cellPtrs[i] << 8);
          const uint8_t* cp = p + off;
          uint32_t child = (cp[0]<<24)|(cp[1]<<16)|(cp[2]<<8)|cp[3];
          if (checkRow(cp + 4)) return;
          SearchPage(child);
          if (result) return;
        }
      }
    }
  };

  FindTable ft{data, pageSize, tableName, strlen(tableName), 0};
  ft.SearchPage(1);
  uint32_t rootPage = ft.result;
  if (!rootPage) return 0;

  UINT32 imported = 0;
  struct Walker {
    const std::vector<uint8_t>& data;
    uint32_t pageSize;
    ICoreWebView2CookieManager* mgr;
    UINT32& imported;

    void Walk(uint32_t pg) {
      if (pg == 0 || (pg - 1) * pageSize >= data.size()) return;
      const uint8_t* p = data.data() + (pg - 1) * pageSize;
      uint8_t type = p[0];
      uint16_t cells = *(uint16_t*)(p + 3);
      cells = (cells >> 8) | (cells << 8);
      uint16_t* cellPtrs = (uint16_t*)(p + 8);

      if (type == 0x0D) {
        for (int i = 0; i < cells; i++) {
          uint16_t off = (cellPtrs[i] >> 8) | (cellPtrs[i] << 8);
          const uint8_t* cp = p + off;
          ReadVarint(cp); ReadVarint(cp); ReadVarint(cp);

          uint64_t ser[18];
          int nSer = 0;
          while (nSer < 18 && cp < p + pageSize) {
            if (nSer == 0) { ser[nSer++] = ReadVarint(cp); continue; }
            ser[nSer++] = ReadVarint(cp);
          }

          std::string host_key, name, value, path;
          int64_t expires_utc = 0;
          int is_secure = 0, is_httponly = 0, samesite = -1;

          auto readText = [&](uint64_t st) -> std::string {
            if (st < 12) return {};
            std::string s(reinterpret_cast<const char*>(cp), (st - 12) / 2);
            cp += (st - 12) / 2; return s;
          };
          auto readInt = [&](uint64_t st) -> int64_t {
            int64_t v = 0;
            if (st == 0) return 0;
            if (st == 1) { v = (int8_t)*cp; cp += 1; }
            else if (st == 2) { v = (int16_t)((cp[0]<<8)|cp[1]); cp += 2; }
            else if (st == 3) { int32_t x = (cp[0]<<16)|(cp[1]<<8)|cp[2]; if (x & 0x800000) x |= ~0xffffff; v = x; cp += 3; }
            else if (st == 4) { v = (int32_t)((cp[0]<<24)|(cp[1]<<16)|(cp[2]<<8)|cp[3]); cp += 4; }
            else if (st == 5) { cp += 6; }
            else if (st == 6) { uint64_t uv = ((uint64_t)cp[0]<<56)|((uint64_t)cp[1]<<48)|((uint64_t)cp[2]<<40)|((uint64_t)cp[3]<<32)|((uint64_t)cp[4]<<24)|((uint64_t)cp[5]<<16)|((uint64_t)cp[6]<<8)|(uint64_t)cp[7]; v = (int64_t)uv; cp += 8; }
            else if (st == 7) { cp += 8; }
            else if (st == 8) { v = 0; }
            else if (st == 9) { v = 1; }
            return v;
          };

          {
            int tCol = 0, iCol = 0;
            for (int ci = 0; ci < nSer; ci++) {
              uint64_t st = ser[ci];
              if (st >= 12) {
                if ((st - 12) % 2 == 0) {
                  cp += (st - 12) / 2;
                  continue;
                }
                std::string txt(reinterpret_cast<const char*>(cp), (st - 13) / 2);
                cp += (st - 13) / 2;
                if (tCol == 0) host_key = std::move(txt);
                else if (tCol == 1) name = std::move(txt);
                else if (tCol == 2) value = std::move(txt);
                else if (tCol == 3) path = std::move(txt);
                tCol++;
              } else if (st >= 1 && st <= 9) {
                int64_t v = readInt(st);
                if (iCol == 0) expires_utc = v;
                else if (iCol == 1) is_secure = (int)v;
                else if (iCol == 2) is_httponly = (int)v;
                else if (iCol == 3) samesite = (int)v;
                iCol++;
              }
            }
          }

          if (host_key.empty() || name.empty()) continue;

          ICoreWebView2Cookie* c = nullptr;
          HRESULT hr2 = mgr->CreateCookie(
              UTF8ToWide(name).c_str(), UTF8ToWide(value).c_str(),
              UTF8ToWide(host_key).c_str(), UTF8ToWide(path.empty() ? "/" : path).c_str(), &c);
          if (FAILED(hr2) || !c) continue;
          c->put_IsSecure(is_secure ? TRUE : FALSE);
          c->put_IsHttpOnly(is_httponly ? TRUE : FALSE);
          if (samesite >= 0) c->put_SameSite(static_cast<COREWEBVIEW2_COOKIE_SAME_SITE_KIND>(samesite));
          if (expires_utc > 0) c->put_Expires((double)expires_utc / 1000000.0);
          mgr->AddOrUpdateCookie(c);
          c->Release();
          imported++;
        }
      } else if (type == 0x05) {
        for (int i = 0; i < cells; i++) {
          uint16_t off = (cellPtrs[i] >> 8) | (cellPtrs[i] << 8);
          uint32_t child = (p[off+0]<<24)|(p[off+1]<<16)|(p[off+2]<<8)|p[off+3];
          Walk(child);
        }
      }
    }
  };

  Walker{data, pageSize, mgr, imported}.Walk(rootPage);
  return imported;
}

void BrowserWindow::ImportChromeCookies() {
  if (!WebView()) return;

  ICoreWebView2_2* wv2 = nullptr;
  HRESULT hr = WebView()->QueryInterface(IID_ICoreWebView2_2, (void**)&wv2);
  if (FAILED(hr) || !wv2) return;
  ICoreWebView2CookieManager* mgr = nullptr;
  wv2->get_CookieManager(&mgr);
  wv2->Release();
  if (!mgr) return;

  struct BrowserDef {
    const wchar_t* env;
    const wchar_t* rootSubpath;
    const char* table;
    const wchar_t* name;
  };

  BrowserDef browsers[] = {
    {L"LOCALAPPDATA", L"\\Google\\Chrome\\User Data", "cookies", L"Chrome"},
    {L"LOCALAPPDATA", L"\\Microsoft\\Edge\\User Data", "cookies", L"Edge"},
    {L"LOCALAPPDATA", L"\\BraveSoftware\\Brave-Browser\\User Data", "cookies", L"Brave"},
    {L"LOCALAPPDATA", L"\\Chromium\\User Data", "cookies", L"Chromium"},
    {L"LOCALAPPDATA", L"\\Vivaldi\\User Data", "cookies", L"Vivaldi"},
    {L"APPDATA", L"\\Opera Software\\Opera Stable", "cookies", L"Opera"},
  };

  UINT32 total = 0;
  std::wstring foundList;
  std::wstring diag;

  for (auto& b : browsers) {
    wchar_t root[MAX_PATH];
    GetEnvironmentVariableW(b.env, root, MAX_PATH);
    wcscat_s(root, b.rootSubpath);

    DWORD rootAttr = GetFileAttributesW(root);
    if (rootAttr == INVALID_FILE_ATTRIBUTES || !(rootAttr & FILE_ATTRIBUTE_DIRECTORY)) {
      diag += b.name; diag += L": dir niet gevonden\r\n";
      continue;
    }

    wchar_t pattern[MAX_PATH];
    wcscpy_s(pattern, root);
    wcscat_s(pattern, L"\\*");

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
      diag += b.name; diag += L": FindFirstFile mislukt\r\n";
      continue;
    }

    const wchar_t* subPaths[] = {L"\\Cookies", L"\\Network\\Cookies"};
    int foundCount = 0;
    do {
      if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) continue;

      for (int spi = 0; spi < 2; spi++) {
        wchar_t path[MAX_PATH];
        wcscpy_s(path, root);
        wcscat_s(path, L"\\");
        wcscat_s(path, ffd.cFileName);
        wcscat_s(path, subPaths[spi]);

        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        foundCount++;

        UINT32 n = ImportCookiesFromFile(mgr, path, b.table);
        if (n > 0 && n != UINT32_MAX) {
          total += n;
          if (!foundList.empty()) foundList += L", ";
          foundList += b.name;
          foundList += L" [" + std::wstring(ffd.cFileName) + L"]";
          foundList += L" (" + std::to_wstring(n) + L")";
          diag += b.name; diag += L" ["; diag += ffd.cFileName; diag += subPaths[spi];
          diag += L"]: "; diag += std::to_wstring(n); diag += L" cookies OK\r\n";
        } else {
          diag += b.name; diag += L" ["; diag += ffd.cFileName; diag += subPaths[spi];
          diag += L"]: ";
          diag += (n == 0) ? L"0 cookies\r\n" : L"FOUT bij lezen\r\n";
        }
      }
    } while (FindNextFileW(hFind, &ffd) != 0);
    FindClose(hFind);

    if (foundCount == 0) {
      diag += b.name; diag += L": geen Cookies bestand gevonden in profielen\r\n";
    }
  }

  mgr->Release();

  wchar_t msg[1024];
  if (total > 0) {
    swprintf_s(msg, L"%u cookies geimporteerd uit: %s\n\n%s", total, foundList.c_str(), diag.c_str());
  } else {
    swprintf_s(msg, L"Geen cookies gevonden.\n\n%s", diag.c_str());
  }
  MessageBoxW(nullptr, msg, L"Cookie Import", MB_OK);
}
