#pragma once
#include <common.h>

#include "FolderWindow.h"

namespace chromabrowse {

// about the name: https://devblogs.microsoft.com/oldnewthing/20030910-00/?p=42583
class TrayWindow : public FolderWindow {
public:
    static void init();

    TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

protected:
    int captionHeight() override;

private:
    const wchar_t * className() override;

    bool initBrowser() override;
};

} // namespace
