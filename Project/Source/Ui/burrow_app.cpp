#include "Core/platform.h"
#include "Core/vault.h"
#include "Core/vault_registry.h"
#include "Ui/web_frame.h"

#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>

#include <string>

namespace {
struct Options {
    std::string vault;  // --vault <folder>: open that vault without touching the saved vault list
    std::string page;   // --page <name>: start on this page (overview, agents, chats, ...)
};

Options ParseCommandLine() {
    Options options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = winstr::Narrow(argv[i]);
        auto value = [&](const char* name, std::string& out) {
            const std::string flag = std::string("--") + name;
            if (arg == flag && i + 1 < argc) {
                out = winstr::Narrow(argv[++i]);
                return true;
            }
            if (arg.rfind(flag + "=", 0) == 0) {
                out = arg.substr(flag.size() + 1);
                return true;
            }
            return false;
        };
        if (!value("vault", options.vault))
            value("page", options.page);
    }
    LocalFree(argv);
    return options;
}

void ShowError(const std::string& message) {
    MessageBoxW(nullptr, winstr::Widen(message).c_str(), L"Burrow", MB_OK | MB_ICONERROR);
}
}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (FAILED(OleInitialize(nullptr)))  // WebView2 and the folder picker need a single-threaded COM apartment
        return 1;
    int exit_code = 0;
    {
        const Options options = ParseCommandLine();
        VaultRegistry registry;
        Vault vault;
        bool have_vault = false;
        std::string error;

        if (!options.vault.empty()) {
            if (!Vault::Open(options.vault, vault, error)) {
                ShowError(error);
                OleUninitialize();
                return 1;
            }
            have_vault = true;
        } else {
            // Reopen the last vault if it is still there, otherwise the page asks for one.
            const std::string last = registry.LastOpenedPath();
            if (!last.empty() && Vault::Open(last, vault, error)) {
                registry.SetLastOpened(vault.Root());
                have_vault = true;
            }
        }

        WebFrame frame(registry, have_vault ? &vault : nullptr, options.page);
        if (!frame.Create()) {
            ShowError("Pencere oluşturulamadı.");
            exit_code = 1;
        } else {
            frame.Show();
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            exit_code = static_cast<int>(msg.wParam);
        }
    }
    OleUninitialize();
    return exit_code;
}
