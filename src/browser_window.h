#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "WebView2.h"

class BrowserWindow {
public:
  BrowserWindow();
  ~BrowserWindow();

  bool Initialize(HWND parent);

  void Navigate(const std::string& url);
  void Navigate(const std::wstring& url);
  void GoBack();
  void GoForward();
  void Reload();
  void Resize();
  void ExportCookies();
  void ImportCookies();
  void ToggleTheme();
  bool IsDark() const { return isDark_; }

  void SavePassword();
  void AutoFillPassword();
  void ViewPasswords();
  void ClearPasswords();

  void ImportChromeCookies();
  void AddBookmark();
  void ShowBookmarks();
  void ShowDownloads();
  void ToggleSiteDark();

  ICoreWebView2* WebView() const { return webview_; }

private:
  ICoreWebView2Environment* env_ = nullptr;
  ICoreWebView2Controller* controller_ = nullptr;
  ICoreWebView2* webview_ = nullptr;
  bool isDark_ = false;
  bool siteDark_ = false;

  friend struct EnvHandler;
  friend struct CtrlHandler;
  friend struct PmSaveHandler;
  friend struct PmAutofillHandler;
};
