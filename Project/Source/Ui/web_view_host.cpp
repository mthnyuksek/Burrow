#include "Ui/web_view_host.h"

#include "Ui/embedded_web.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "WebView2.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
const wchar_t kAppUrl[] = L"https://app.burrow/";
const wchar_t kAppFilter[] = L"https://app.burrow/*";

// MinGW has no __uuidof for the WebView2 interfaces; these two GUIDs are copied from WebView2.h.
const GUID kIidController2 = {0xc979903e, 0xd4ca, 0x4228, {0x92, 0xeb, 0x47, 0xee, 0x3f, 0xa9, 0x6e, 0xab}};
const GUID kIidSettings3 = {0xfdb5ab74, 0xaf33, 0x4854, {0x84, 0xf0, 0x0a, 0x63, 0x1d, 0xeb, 0x5e, 0xba}};

std::wstring Widen(const std::string& s) {
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string Narrow(const wchar_t* s) {
    if (!s || !*s)
        return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &out[0], n, nullptr, nullptr);
    out.resize(static_cast<size_t>(n) - 1);  // drop the terminating zero
    return out;
}

// %LOCALAPPDATA%\burrow\<sub>
std::wstring AppDataDir(const wchar_t* sub) {
    wchar_t base[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return std::wstring();
    std::wstring dir = std::wstring(base) + L"\\burrow";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += std::wstring(L"\\") + sub;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// The loader DLL travels inside the program; it is copied out once so LoadLibrary can use it.
std::wstring ExtractLoader() {
    const unsigned char* data = nullptr;
    size_t size = 0;
    std::string mime;
    if (!FindEmbeddedFile("loader/WebView2Loader.dll", data, size, mime))
        return std::wstring();
    const std::wstring dir = AppDataDir(L"runtime");
    if (dir.empty())
        return std::wstring();
    const std::wstring path = dir + L"\\WebView2Loader.dll";
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeLow == size &&
        info.nFileSizeHigh == 0)
        return path;  // already there (and maybe loaded by another instance)
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return std::wstring();
    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(file);
    return ok && written == size ? path : std::wstring();
}

// A COM callback object that forwards Invoke to a lambda. Reference counted like any COM object.
template <class Iface, class... Args>
class Handler final : public Iface {
public:
    explicit Handler(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}

    // WebView2 only ever uses the pointer it was handed, so IUnknown is all that has to be answered.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown) {
            *out = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --refs_;
        if (left == 0)
            delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    std::function<HRESULT(Args...)> fn_;
    std::atomic<ULONG> refs_{1};
};

template <class Iface, class... Args, class Fn>
Iface* MakeHandler(Fn fn) {
    return new Handler<Iface, Args...>(std::function<HRESULT(Args...)>(std::move(fn)));
}

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void OpenInBrowser(const std::wstring& uri) {
    if (uri.rfind(L"http://", 0) == 0 || uri.rfind(L"https://", 0) == 0)
        ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
}  // namespace

struct WebViewHost::Impl {
    WebViewHost* owner = nullptr;
    HWND parent = nullptr;
    HMODULE loader = nullptr;
    ICoreWebView2Environment* env = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* view = nullptr;
    RECT bounds = {0, 0, 0, 0};
    bool ready = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);  // callbacks check it after the host is gone

    void OnControllerCreated(ICoreWebView2Controller* created);
    void ServeResource(ICoreWebView2WebResourceRequestedEventArgs* args);
};

void WebViewHost::Impl::ServeResource(ICoreWebView2WebResourceRequestedEventArgs* args) {
    ICoreWebView2WebResourceRequest* request = nullptr;
    if (FAILED(args->get_Request(&request)) || !request)
        return;
    LPWSTR uri = nullptr;
    request->get_Uri(&uri);
    std::string path = Narrow(uri);
    CoTaskMemFree(uri);
    SafeRelease(request);

    const std::string prefix = Narrow(kAppUrl);
    if (path.rfind(prefix, 0) != 0)
        return;
    path.erase(0, prefix.size());
    const size_t cut = path.find_first_of("?#");
    if (cut != std::string::npos)
        path.erase(cut);
    if (path.empty())
        path = "index.html";

    const unsigned char* data = nullptr;
    size_t size = 0;
    std::string mime;
    const bool found = FindEmbeddedFile("web/" + path, data, size, mime);

    IStream* stream = found ? SHCreateMemStream(data, static_cast<UINT>(size)) : nullptr;
    const std::wstring headers =
        found ? Widen("Content-Type: " + mime + "\r\nCache-Control: no-store") : std::wstring(L"Content-Type: text/plain");
    ICoreWebView2WebResourceResponse* response = nullptr;
    if (SUCCEEDED(env->CreateWebResourceResponse(stream, found ? 200 : 404, found ? L"OK" : L"Not Found",
                                                 headers.c_str(), &response)) &&
        response)
        args->put_Response(response);
    SafeRelease(response);
    SafeRelease(stream);
}

void WebViewHost::Impl::OnControllerCreated(ICoreWebView2Controller* created) {
    controller = created;
    controller->AddRef();
    controller->get_CoreWebView2(&view);
    if (!view)
        return;

    ICoreWebView2Controller2* controller2 = nullptr;
    if (SUCCEEDED(controller->QueryInterface(kIidController2, reinterpret_cast<void**>(&controller2))) && controller2) {
        COREWEBVIEW2_COLOR dark = {255, 15, 17, 23};  // same as --bg in app.css: no white flash while loading
        controller2->put_DefaultBackgroundColor(dark);
        controller2->Release();
    }

    ICoreWebView2Settings* settings = nullptr;
    if (SUCCEEDED(view->get_Settings(&settings)) && settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        // Browser shortcuts (F5, Ctrl+P ...) are off: it is an app. Developers keep them (reload, F12)
        // while BURROW_WEB_DIR is set, see embedded_web.cpp.
        ICoreWebView2Settings3* settings3 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(kIidSettings3, reinterpret_cast<void**>(&settings3))) && settings3) {
            settings3->put_AreBrowserAcceleratorKeysEnabled(std::getenv("BURROW_WEB_DIR") ? TRUE : FALSE);
            settings3->Release();
        }
        settings->Release();
    }

    const std::shared_ptr<bool> life = alive;
    EventRegistrationToken token;

    view->AddWebResourceRequestedFilter(kAppFilter, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    view->add_WebResourceRequested(
        MakeHandler<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*,
                    ICoreWebView2WebResourceRequestedEventArgs*>(
            [this, life](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
                if (*life)
                    ServeResource(args);
                return S_OK;
            }),
        &token);

    view->add_WebMessageReceived(
        MakeHandler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*,
                    ICoreWebView2WebMessageReceivedEventArgs*>(
            [this, life](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
                LPWSTR json = nullptr;
                if (*life && SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                    if (owner->on_message)
                        owner->on_message(Narrow(json));
                    CoTaskMemFree(json);
                }
                return S_OK;
            }),
        &token);

    // Links must never replace the app page; they open in the normal browser instead.
    view->add_NavigationStarting(
        MakeHandler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*,
                    ICoreWebView2NavigationStartingEventArgs*>(
            [life](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
                LPWSTR uri = nullptr;
                if (*life && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    const std::wstring target = uri;
                    CoTaskMemFree(uri);
                    if (target.rfind(kAppUrl, 0) != 0 && target.rfind(L"about:", 0) != 0) {
                        args->put_Cancel(TRUE);
                        OpenInBrowser(target);
                    }
                }
                return S_OK;
            }),
        &token);
    view->add_NewWindowRequested(
        MakeHandler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*,
                    ICoreWebView2NewWindowRequestedEventArgs*>(
            [life](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr;
                if (*life && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    OpenInBrowser(uri);
                    CoTaskMemFree(uri);
                }
                return S_OK;
            }),
        &token);

    controller->put_Bounds(bounds);
    controller->put_IsVisible(TRUE);
    ready = true;
    view->Navigate((std::wstring(kAppUrl) + L"index.html").c_str());
    if (owner->on_ready)
        owner->on_ready();
}

WebViewHost::WebViewHost() : impl_(new Impl) { impl_->owner = this; }

WebViewHost::~WebViewHost() {
    *impl_->alive = false;
    if (impl_->controller)
        impl_->controller->Close();
    SafeRelease(impl_->view);
    SafeRelease(impl_->controller);
    SafeRelease(impl_->env);
}

bool WebViewHost::Create(HWND parent, std::wstring& error) {
    impl_->parent = parent;
    GetClientRect(parent, &impl_->bounds);

    const std::wstring loader_path = ExtractLoader();
    if (loader_path.empty()) {
        error = L"WebView2 yükleyicisi (WebView2Loader.dll) çıkarılamadı.";
        return false;
    }
    impl_->loader = LoadLibraryW(loader_path.c_str());
    typedef HRESULT(STDAPICALLTYPE * CreateEnvironmentFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                         ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create_env = impl_->loader ? reinterpret_cast<CreateEnvironmentFn>(
                                          GetProcAddress(impl_->loader, "CreateCoreWebView2EnvironmentWithOptions"))
                                    : nullptr;
    if (!create_env) {
        error = L"WebView2Loader.dll yüklenemedi.";
        return false;
    }

    Impl* impl = impl_.get();
    const std::shared_ptr<bool> life = impl->alive;
    const std::wstring data_dir = AppDataDir(L"webview_data");
    auto* env_done = MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                                 ICoreWebView2Environment*>(
        [impl, life](HRESULT result, ICoreWebView2Environment* created) {
            if (!*life || FAILED(result) || !created)
                return S_OK;
            impl->env = created;
            created->AddRef();
            created->CreateCoreWebView2Controller(
                impl->parent, MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                          ICoreWebView2Controller*>(
                                  [impl, life](HRESULT r, ICoreWebView2Controller* controller) {
                                      if (*life && SUCCEEDED(r) && controller)
                                          impl->OnControllerCreated(controller);
                                      return S_OK;
                                  }));
            return S_OK;
        });
    const HRESULT hr = create_env(nullptr, data_dir.empty() ? nullptr : data_dir.c_str(), nullptr, env_done);
    env_done->Release();
    if (FAILED(hr)) {
        error = L"WebView2 çalışma zamanı bulunamadı. Microsoft Edge WebView2 Runtime kurulu olmalı "
                L"(Windows 11'de hazır gelir).";
        return false;
    }
    return true;
}

void WebViewHost::Resize(int width, int height) {
    impl_->bounds = {0, 0, width, height};
    if (impl_->controller)
        impl_->controller->put_Bounds(impl_->bounds);
}

void WebViewHost::Focus() {
    if (impl_->controller)
        impl_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void WebViewHost::PostToPage(const std::string& json_utf8) {
    if (impl_->ready && impl_->view)
        impl_->view->PostWebMessageAsJson(Widen(json_utf8).c_str());
}

void WebViewHost::OpenDevTools() {
    if (impl_->view)
        impl_->view->OpenDevToolsWindow();
}
