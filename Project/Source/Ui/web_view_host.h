#pragma once

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

// Hosts one WebView2 control inside a window and serves the embedded Web files to it from
// https://app.burrow/. WebView2.h is huge, so it stays inside the .cpp (pimpl).
//
// Page -> program: window.chrome.webview.postMessage(json) arrives in on_message (UTF-8 JSON text).
// Program -> page: PostToPage(json) arrives in window.chrome.webview's "message" event.
class WebViewHost {
public:
    WebViewHost();
    ~WebViewHost();

    // False with a readable `error` when the WebView2 runtime or the loader is missing.
    // The control appears asynchronously; on_ready is called once the page is loading.
    bool Create(HWND parent, std::wstring& error);
    void Resize(int width, int height);
    void Focus();
    void PostToPage(const std::string& json_utf8);  // dropped while the control is not ready
    void OpenDevTools();

    std::function<void(const std::string&)> on_message;
    std::function<void()> on_ready;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
