#include "FolderWindow.h"
#include "ThumbnailWindow.h"
#include "PreviewWindow.h"
#include "TrayWindow.h"
#include "ItemWindowFactory.h"
#include "resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <propkey.h>
#include <Propvarutil.h>
#include <strsafe.h>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "Comctl32.lib")

const wchar_t *APP_ID = L"chroma.browse";

#ifdef CHROMABROWSE_DEBUG
int main(int, char**) {
    wWinMain(nullptr, nullptr, nullptr, SW_SHOWNORMAL);
}
#endif

DWORD WINAPI updateJumpListThread(void *);
bool updateJumpList();

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    debugPrintf(L"omg hiiiii ^w^\n"); // DO NOT REMOVE!!
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLine(), &argc);

    if (FAILED(OleInitialize(0))) // needed for drag/drop
        return 0;

    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    chromabrowse::ItemWindow::init();
    chromabrowse::FolderWindow::init();
    chromabrowse::ThumbnailWindow::init();
    chromabrowse::PreviewWindow::init();
    chromabrowse::TrayWindow::init();

    // https://docs.microsoft.com/en-us/windows/win32/shell/appids
    SetCurrentProcessExplicitAppUserModelID(APP_ID);

    DWORD threadId;
    CreateThread(nullptr, 0, updateJumpListThread, nullptr, 0, &threadId);

    {
        CComPtr<chromabrowse::ItemWindow> initialWindow;
        CComPtr<IShellItem> startItem;
        if (argc > 1 && lstrcmpi(argv[1], L"/tray") == 0) {
            if (FAILED(SHGetKnownFolderItem(FOLDERID_Links, KF_FLAG_DEFAULT, nullptr,
                        IID_PPV_ARGS(&startItem)))) {
                debugPrintf(L"Couldn't get links folder!\n");
                return 0;
            }
            initialWindow.Attach(new chromabrowse::TrayWindow(nullptr, startItem));
        } else {
            if (argc > 1) {
                int pathLen = lstrlen(argv[1]);
                if (argv[1][pathLen - 1] == '"')
                    argv[1][pathLen - 1] = '\\'; // fix weird CommandLineToArgvW behavior with \"
                // parse name vs display name https://stackoverflow.com/q/42966489
                if (FAILED(SHCreateItemFromParsingName(argv[1], nullptr,
                        IID_PPV_ARGS(&startItem)))) {
                    debugPrintf(L"Unable to locate item at path %s\n", argv[1]);
                    return 0;
                }
            } else {
                if (FAILED(SHGetKnownFolderItem(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr,
                        IID_PPV_ARGS(&startItem)))) {
                    debugPrintf(L"Couldn't get desktop!\n");
                    return 0;
                }
            }
            initialWindow = chromabrowse::createItemWindow(nullptr, startItem);
        }

        SIZE size = initialWindow->requestedSize();
        RECT windowRect;
        if (argc > 1) {
            windowRect = {CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT + size.cx, CW_USEDEFAULT + size.cy};
        } else {
            RECT workArea;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
            windowRect = {workArea.left, workArea.bottom - size.cy,
                          workArea.left + size.cx, workArea.bottom};
        }
        initialWindow->create(windowRect, showCommand);
    }
    LocalFree(argv);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == 0x04CC)
            debugPrintf(L"User message %x %llx %llx\n", msg.message, msg.wParam, msg.lParam);
        if (chromabrowse::activeWindow && chromabrowse::activeWindow->handleTopLevelMessage(&msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    updateJumpList();

    chromabrowse::ItemWindow::uninit();
    OleUninitialize();
    return 0;
}

DWORD WINAPI updateJumpListThread(void *) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 0;
    if (!updateJumpList()) {
        debugPrintf(L"Failed to update jump list\n");
    }
    CoUninitialize();
    return 0;
}

bool updateJumpList() {
    CComPtr<ICustomDestinationList> jumpList;
    if (FAILED(jumpList.CoCreateInstance(__uuidof(DestinationList))))
        return false;
    jumpList->SetAppID(APP_ID);
    UINT minSlots;
    CComPtr<IObjectArray> removedDestinations;
    jumpList->BeginList(&minSlots, IID_PPV_ARGS(&removedDestinations));

    CComPtr<IObjectCollection> tasks;
    tasks.CoCreateInstance(__uuidof(EnumerableObjectCollection));

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(GetModuleHandle(NULL), exePath, MAX_PATH);

    CComPtr<IShellItem> quickAccessItem;
    SHCreateItemFromParsingName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}",
        nullptr, IID_PPV_ARGS(&quickAccessItem));
    CComPtr<IEnumShellItems> enumItems;
    if (quickAccessItem && SUCCEEDED(quickAccessItem->BindToHandler(
            nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumItems)))) {
        CComPtr<IShellItem> childItem;
        for (int i = 0; i < 20; i++)
            enumItems->Next(1, &childItem, nullptr); // TODO: hack!! the first 20 items are recents
        while (enumItems->Next(1, &childItem, nullptr) == S_OK) {
            CComHeapPtr<wchar_t> displayName, parsingName;
            childItem->GetDisplayName(SIGDN_NORMALDISPLAY, &displayName);
            childItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingName);
            wchar_t args[MAX_PATH];
            args[0] = L'"';
            StringCchCopy(args + 1, MAX_PATH - 2, parsingName);
            StringCchCat(args, MAX_PATH, L"\"");

            CComPtr<IShellLink> link;
            link.CoCreateInstance(__uuidof(ShellLink));
            link->SetPath(exePath);
            link->SetArguments(args);
            link->SetIconLocation(exePath, IDR_APP_ICON);
            CComQIPtr<IPropertyStore> linkProps(link);
            PROPVARIANT propVar;
            InitPropVariantFromString(displayName, &propVar);
            linkProps->SetValue(PKEY_Title, propVar);
            tasks->AddObject(link);

            CComPtr<IExtractIcon> extractIcon;
            if (SUCCEEDED(childItem->BindToHandler(nullptr, BHID_SFUIObject,
                    IID_PPV_ARGS(&extractIcon)))) {
                wchar_t iconFile[MAX_PATH];
                int index;
                UINT flags;
                if (extractIcon->GetIconLocation(0, iconFile, MAX_PATH, &index, &flags) == S_OK) {
                    if (!(flags & GIL_NOTFILENAME))
                        link->SetIconLocation(iconFile, index);
                }
            }
        }
    }

    if (FAILED(jumpList->AddUserTasks(tasks))) {
        debugPrintf(L"Failed to add user tasks\n");
        return false;
    }
    jumpList->CommitList();

    debugPrintf(L"Successfully updated jump list\n");
    return true;
}