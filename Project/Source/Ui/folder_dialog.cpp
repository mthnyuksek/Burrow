#include "Ui/folder_dialog.h"

#include "Core/platform.h"

#include <shobjidl.h>

bool PickFolder(HWND owner, const std::string& title, std::string& path) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dialog))) ||
        !dialog)
        return false;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (!title.empty())
        dialog->SetTitle(winstr::Widen(title).c_str());

    bool picked = false;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR wide = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide) {
                path = winstr::Narrow(wide);
                picked = true;
                CoTaskMemFree(wide);
            }
            item->Release();
        }
    }
    dialog->Release();
    return picked;
}
