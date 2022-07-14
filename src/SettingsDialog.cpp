#include "SettingsDialog.h"
#include "Settings.h"
#include "resource.h"
#include <atlbase.h>
#include <prsht.h>
#include <shellapi.h>

namespace chromabrowse {

const wchar_t *SPECIAL_PATHS[] = {
    L"shell:Desktop",
    L"shell:MyComputerFolder",
    L"shell:Links",
    L"shell:Recent",
    L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}" // Quick access
};

INT_PTR generalProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            for (int i = 0; i < _countof(SPECIAL_PATHS); i++) {
                SendDlgItemMessage(hwnd, IDC_START_FOLDER_PATH, CB_ADDSTRING, 0,
                    (LPARAM)SPECIAL_PATHS[i]);
            }
            CComHeapPtr<wchar_t> startingFolder;
            settings::getStartingFolder(startingFolder);
            SetDlgItemText(hwnd, IDC_START_FOLDER_PATH, startingFolder);
            SIZE folderWindowSize = settings::getFolderWindowSize();
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_WIDTH, folderWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_HEIGHT, folderWindowSize.cy, TRUE);
            SIZE itemWindowSize = settings::getItemWindowSize();
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_WIDTH, itemWindowSize.cx, TRUE);
            SetDlgItemInt(hwnd, IDC_ITEM_WINDOW_HEIGHT, itemWindowSize.cy, TRUE);
            CheckDlgButton(hwnd, IDC_PREVIEWS_ENABLED,
                settings::getPreviewsEnabled() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_TEXT_EDITOR_ENABLED,
                settings::getTextEditorEnabled() ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->code == PSN_KILLACTIVE) {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, FALSE);
                return TRUE;
            } else if (notif->code == PSN_APPLY) {
                wchar_t startingFolder[MAX_PATH];
                if (GetDlgItemText(hwnd, IDC_START_FOLDER_PATH, startingFolder, MAX_PATH))
                    settings::setStartingFolder(startingFolder);
                BOOL success;
                SIZE size;
                size.cx = GetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_WIDTH, &success, TRUE);
                if (success) {
                    size.cy = GetDlgItemInt(hwnd, IDC_FOLDER_WINDOW_HEIGHT, &success, TRUE);
                    if (success)
                        settings::setFolderWindowSize(size);
                }
                size.cx = GetDlgItemInt(hwnd, IDC_ITEM_WINDOW_WIDTH, &success, TRUE);
                if (success) {
                    size.cy = GetDlgItemInt(hwnd, IDC_ITEM_WINDOW_HEIGHT, &success, TRUE);
                    if (success)
                        settings::setItemWindowSize(size);
                }
                settings::setPreviewsEnabled(!!IsDlgButtonChecked(hwnd, IDC_PREVIEWS_ENABLED));
                settings::setTextEditorEnabled(!!IsDlgButtonChecked(hwnd, IDC_TEXT_EDITOR_ENABLED));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EXPLORER_SETTINGS && HIWORD(wParam) == BN_CLICKED) {
                // https://docs.microsoft.com/en-us/windows/win32/shell/executing-control-panel-items#folder-options
                ShellExecute(nullptr, L"open",
                    L"rundll32.exe", L"shell32.dll,Options_RunDLL 7", nullptr, SW_SHOWNORMAL);
                return TRUE;
            } else if (HIWORD(wParam) == EN_CHANGE
                    || LOWORD(wParam) == IDC_START_FOLDER_PATH && HIWORD(wParam) == CBN_EDITCHANGE
                    || LOWORD(wParam) == IDC_START_FOLDER_PATH && HIWORD(wParam) == CBN_SELCHANGE
                    || LOWORD(wParam) == IDC_PREVIEWS_ENABLED && HIWORD(wParam) == BN_CLICKED
                    || LOWORD(wParam) == IDC_TEXT_EDITOR_ENABLED && HIWORD(wParam) == BN_CLICKED) {
                PropSheet_Changed(GetParent(hwnd), hwnd);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

void openSettingsDialog(HWND owner) {
    PROPSHEETPAGE generalPage = {sizeof(generalPage)};
    generalPage.hInstance = GetModuleHandle(nullptr);
    generalPage.pszTemplate = MAKEINTRESOURCE(IDD_SETTINGS_GENERAL);
    generalPage.pfnDlgProc = generalProc;

    PROPSHEETHEADER sheet = {sizeof(sheet)};
    sheet.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_NOCONTEXTHELP;
    sheet.hwndParent = owner;
    sheet.hInstance = GetModuleHandle(nullptr);
    sheet.pszIcon = MAKEINTRESOURCE(IDR_APP_ICON);
    sheet.pszCaption = MAKEINTRESOURCE(IDS_SETTINGS_CAPTION);
    sheet.nPages = 1;
    sheet.ppsp = &generalPage;
    PropertySheet(&sheet);
}

} // namespace