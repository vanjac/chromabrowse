#include "TrayWindow.h"

namespace chromabrowse {

const wchar_t *TRAY_WINDOW_CLASS = L"Tray Window";

void TrayWindow::init() {
    WNDCLASS wndClass = createWindowClass(TRAY_WINDOW_CLASS);
    RegisterClass(&wndClass);
}

TrayWindow::TrayWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : FolderWindow(parent, item)
{}

const wchar_t * TrayWindow::className() {
    return TRAY_WINDOW_CLASS;
}

int TrayWindow::captionHeight() {
    return 16;
}

bool TrayWindow::initBrowser() {
    RECT browserRect = windowBody();
    browserRect.bottom += browserRect.top; // initial rect is wrong

    FOLDERSETTINGS folderSettings = {};
    folderSettings.ViewMode = FVM_LIST;
    folderSettings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOCOLUMNHEADER | FWF_NOGROUPING
        | FWF_NOBROWSERVIEWSTATE;
    if (FAILED(browser.CoCreateInstance(__uuidof(ExplorerBrowser)))
            || FAILED(browser->Initialize(hwnd, &browserRect, &folderSettings)))
        return false;
    browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOPERSISTVIEWSTATE);
    return true;
}

} // namespace
