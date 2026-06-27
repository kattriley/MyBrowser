#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "WebView2.h"

struct TabInfo {
  ICoreWebView2Controller* controller = nullptr;
  ICoreWebView2* webview = nullptr;
  std::wstring url;
  std::wstring title;
  bool isLoading = false;
};

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
  void SearchInPage();

  void NewTab(const std::wstring& url = L"https://www.google.com");
  void CloseTab(int idx);
  void SwitchTab(int idx);
  int ActiveTab() const { return activeTab_; }
  int TabCount() const { return (int)tabs_.size(); }
  const std::wstring& TabTitle(int idx) const { return tabs_[idx].title; }

  ICoreWebView2* WebView() const;
  HWND GetParent() const { return parent_; }
  HWND addrEdit_ = nullptr;

private:
  HWND parent_ = nullptr;
  ICoreWebView2Environment* env_ = nullptr;
  std::vector<TabInfo> tabs_;
  int activeTab_ = -1;
  int nextNewTabId_ = 0;
  bool isDark_ = false;
  bool siteDark_ = false;

  HWND goBtn_ = nullptr;
  std::wstring addressUrl_;

  void SetupNewTab(int idx, const std::wstring& url);

  TabInfo& ActiveTabRef();
  RECT CalcWebViewRect();
  void NavigateTab(int idx, const std::wstring& url);

  friend struct EnvHandler;
  friend struct CtrlHandler;
  friend struct TabCtrlHandler;
  friend struct NavCompletedHandler;
  friend struct SrcChangedHandler;
  friend struct PmSaveHandler;
  friend struct PmAutofillHandler;
};
