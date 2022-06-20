#include "ItemWindow.h"
#include "ItemWindowFactory.h"
#include "RectUtil.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>

namespace chromabrowse {

// dimensions
const int RESIZE_MARGIN = 8; // TODO use some system metric?
const int CAPTION_PADDING = 8;
const int WINDOW_ICON_PADDING = 4;
const int SNAP_DISTANCE = 32;
const SIZE DEFAULT_SIZE = {450, 450};
// colors
// this is the color used in every high-contrast theme
// regular light mode theme uses #999999
const COLORREF INACTIVE_CAPTION_COLOR = 0x636363;

static HFONT symbolFont;

long numOpenWindows;
CComPtr<ItemWindow> activeWindow;

LRESULT CALLBACK captionButtonProc(HWND hwnd, UINT message,
    WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

HACCEL ItemWindow::accelTable;

void ItemWindow::init() {
    // TODO only supported on windows 10!
    symbolFont = CreateFont(12, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    accelTable = LoadAccelerators(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_ITEM_ACCEL));
}

void ItemWindow::uninit() {
    DeleteObject(symbolFont);
    DestroyAcceleratorTable(accelTable);
}

WNDCLASS ItemWindow::createWindowClass(const wchar_t *name) {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = ItemWindow::windowProc;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.lpszClassName = name;
    wndClass.style = CS_HREDRAW; // ensure caption gets redrawn if width changes
    return wndClass;
}

LRESULT CALLBACK ItemWindow::windowProc(
        HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // DWM custom frame
    // https://docs.microsoft.com/en-us/windows/win32/dwm/customframe
    LRESULT dwmResult = 0;
    if (DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult))
        return dwmResult;

    ItemWindow *self = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT *create = (CREATESTRUCT*)lParam;
        self = (ItemWindow*)create->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = hwnd;
    } else {
        self = (ItemWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

ItemWindow::ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : parent(parent)
    , item(item)
    , storedChildSize(DEFAULT_SIZE)
{}

ItemWindow::~ItemWindow() {
    if (iconLarge)
        DestroyIcon(iconLarge);
    if (iconSmall)
        DestroyIcon(iconSmall);
}

bool ItemWindow::preserveSize() {
    return true;
}

SIZE ItemWindow::requestedSize() {
    return DEFAULT_SIZE;
}

int ItemWindow::captionHeight() {
    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    return -adjustedRect.top; // = 31
}

bool ItemWindow::create(RECT rect, int showCommand) {
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &title))) {
        debugPrintf(L"Unable to get item name\n");
        return false;
    }
    debugPrintf(L"Create %s\n", &*title);

    CComPtr<IExtractIcon> extractIcon;
    if (SUCCEEDED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&extractIcon)))) {
        wchar_t iconFile[MAX_PATH];
        int index;
        UINT flags;
        if (extractIcon->GetIconLocation(0, iconFile, MAX_PATH, &index, &flags) == S_OK) {
            UINT iconSizes = (GetSystemMetrics(SM_CXSMICON) << 16) + GetSystemMetrics(SM_CXICON);
            if (extractIcon->Extract(iconFile, index, &iconLarge, &iconSmall, iconSizes) != S_OK) {
                debugPrintf(L"IExtractIcon failed\n");
                // https://devblogs.microsoft.com/oldnewthing/20140501-00/?p=1103
                SHDefExtractIcon(iconFile, index, flags, &iconLarge, &iconSmall, iconSizes);
            }
        }
    }

    // keep window on screen
    if (rect.left != CW_USEDEFAULT && rect.top != CW_USEDEFAULT) {
        POINT testPoint = {rect.left, rect.top + captionHeight()};
        HMONITOR nearestMonitor = MonitorFromPoint(testPoint, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfo(nearestMonitor, &monitorInfo);
        if (testPoint.x < monitorInfo.rcWork.left)
            OffsetRect(&rect, monitorInfo.rcWork.left - testPoint.x, 0);
        if (testPoint.y > monitorInfo.rcWork.bottom)
            OffsetRect(&rect, 0, monitorInfo.rcWork.bottom - testPoint.y);
    }

    HWND taskBar = FindWindow(L"Shell_TrayWnd", NULL);
    if (taskBar)
        debugPrintf(L"found the taskbar!\n");

    HWND createHwnd = CreateWindow(
        className(),
        title,
        // style
        // WS_CLIPCHILDREN fixes drawing glitches with the scrollbars
        (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN,

        // position/size
        rect.left, rect.top, rectWidth(rect), rectHeight(rect),

        taskBar,                // parent/owner window
        nullptr,                // menu
        GetModuleHandle(NULL),  // instance handle
        this);                  // application data
    if (!createHwnd) {
        debugPrintf(L"Couldn't create window\n");
        return false;
    }

    ShowWindow(createHwnd, showCommand);

    AddRef(); // keep window alive while open
    InterlockedIncrement(&numOpenWindows);
    return true;
}

void ItemWindow::close() {
    PostMessage(hwnd, WM_CLOSE, 0, 0);
}

void ItemWindow::activate() {
    SetActiveWindow(hwnd);
}

void ItemWindow::setPos(POINT pos) {
    // TODO SWP_ASYNCWINDOWPOS?
    SetWindowPos(hwnd, nullptr, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ItemWindow::move(int x, int y) {
    RECT rect;
    GetWindowRect(hwnd, &rect);
    setPos({rect.left + x, rect.top + y});
}

RECT ItemWindow::windowBody() {
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    return {0, captionHeight(), clientRect.right, clientRect.bottom};
}

LRESULT ItemWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            onCreate();
            // ensure WM_NCCALCSIZE gets called
            // for DWM custom frame
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        case WM_DESTROY:
            onDestroy();
            return 0;
        case WM_NCDESTROY:
            hwnd = nullptr;
            Release(); // allow window to be deleted
            if (InterlockedDecrement(&numOpenWindows) == 0)
                PostQuitMessage(0);
            return 0;
        case WM_ACTIVATE: {
            onActivate(LOWORD(wParam), (HWND)lParam);
            return 0;
        }
        case WM_NCCALCSIZE:
            if (wParam == TRUE) {
                // allow resizing past the edge of the window by reducing client rect
                NCCALCSIZE_PARAMS *params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                params->rgrc[0].left = params->rgrc[0].left + RESIZE_MARGIN;
                params->rgrc[0].top = params->rgrc[0].top;
                params->rgrc[0].right = params->rgrc[0].right - RESIZE_MARGIN;
                params->rgrc[0].bottom = params->rgrc[0].bottom - RESIZE_MARGIN;
                // for DWM custom frame
                return 0;
            }
        case WM_NCHITTEST: {
            // for DWM custom frame
            LRESULT defHitTest = DefWindowProc(hwnd, message, wParam, lParam);
            if (defHitTest != HTCLIENT)
                return defHitTest;
            return hitTestNCA({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        }
        case WM_PAINT: {
            // for DWM custom frame
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            onPaint(paint);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            moveAccum = {0, 0};
            return 0;
        case WM_MOVING:
            // https://www.drdobbs.com/make-it-snappy/184416407
            if (parent) {
                RECT *desiredRect = (RECT *)lParam;
                RECT curRect;
                GetWindowRect(hwnd, &curRect);
                moveAccum.x += desiredRect->left - curRect.left;
                moveAccum.y += desiredRect->top - curRect.top;
                // windows max macro prevents me from using std::max :')
                int moveAmount = max(abs(moveAccum.x), abs(moveAccum.y));
                if (moveAmount > SNAP_DISTANCE) {
                    detachFromParent();
                    bringGroupToFront();
                    OffsetRect(desiredRect, moveAccum.x, moveAccum.y);
                } else {
                    *desiredRect = curRect;
                }
            }
            // required for WM_ENTERSIZEMOVE to behave correctly
            return TRUE;
        case WM_MOVE:
            windowRectChanged();
            return 0;
        case WM_SIZING:
            if (parent) {
                RECT *desiredRect = (RECT *)lParam;
                RECT curRect;
                GetWindowRect(hwnd, &curRect);
                // constrain top-left corner
                int moveX = 0, moveY = 0;
                if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
                    moveX = desiredRect->left - curRect.left;
                if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                    moveY = desiredRect->top - curRect.top;
                if (moveX != 0 || moveY != 0) {
                    auto topParent = parent;
                    while (topParent->parent)
                        topParent = topParent->parent;
                    topParent->move(moveX, moveY);
                }
            }
            return TRUE;
        case WM_SIZE: {
            onSize(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_COMMAND: {
            if ((HWND)lParam == parentButton && HIWORD(wParam) == BN_CLICKED) {
                openParent();
                return 0;
            }
            switch (LOWORD(wParam)) {
                case ID_NEXT_WINDOW:
                    if (child)
                        child->activate();
                    return 0;
                case ID_PREV_WINDOW:
                    if (parent)
                        parent->activate();
                    else
                        openParent();
                    return 0;
                case ID_CLOSE_WINDOW:
                    close();
                    return 0;
                case ID_REFRESH:
                    refresh();
                    return 0;
                case ID_HELP:
                    ShellExecute(NULL, L"open", L"https://github.com/vanjac/chromabrowse/wiki",
                        NULL, NULL, SW_SHOWNORMAL);
                    return 0;
            }
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

bool ItemWindow::handleTopLevelMessage(MSG *msg) {
    if (TranslateAccelerator(hwnd, accelTable, msg))
        return true;
    return false;
}

void ItemWindow::onCreate() {
    BOOL disableAnimations = true;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableAnimations, sizeof(disableAnimations));
    extendWindowFrame();

    if (iconLarge)
        PostMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
    if (iconSmall)
        PostMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);

    if (parent)
        hideInTaskbar();

    CComPtr<IShellItem> parentItem;
    bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
    parentButton = CreateWindow(L"BUTTON", L"\uE96F", // ChevronLeftSmall
        (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
        0, 0, GetSystemMetrics(SM_CXSIZE), captionHeight(),
        hwnd, nullptr, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), nullptr);
    SetWindowSubclass(parentButton, captionButtonProc, 0, 0);
}

void ItemWindow::onDestroy() {
    debugPrintf(L"Cleanup %s\n", &*title);
    if (child) {
        child->parent = nullptr;
        child->close(); // recursive
    }
    child = nullptr;
    clearParent();
    if (activeWindow == this)
        activeWindow = nullptr;
    hideInTaskbar();
}

void ItemWindow::onActivate(WORD state, HWND prevWindow) {
    // for DWM custom frame
    // make sure frame is correct if window is maximized
    extendWindowFrame();

    RECT captionRect;
    GetClientRect(hwnd, &captionRect);
    captionRect.bottom = captionHeight();
    InvalidateRect(hwnd, &captionRect, FALSE); // make sure to update caption text color

    if (state != WA_INACTIVE) {
        activeWindow = this;

        for (ItemWindow *next = child; next; next = next->child) {
            if (next->hwnd == prevWindow)
                return; // group was already front
        }
        for (ItemWindow *next = parent; next; next = next->parent) {
            if (next->hwnd == prevWindow)
                return;
        }
        bringGroupToFront();
    }
}

void ItemWindow::onSize(int, int) {
    windowRectChanged();
    if (parent && preserveSize()) {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        parent->storedChildSize = rectSize(rect);
    }
}

void ItemWindow::windowRectChanged() {
    if (child) {
        child->setPos(childPos());
    }
}

void ItemWindow::bringGroupToFront() {
    ItemWindow *rootParent = this;
    while (rootParent->parent)
        rootParent = rootParent->parent;
    for (ItemWindow *next = rootParent; next; next = next->child) {
        if (next != this)
            SetWindowPos(next->hwnd, hwnd, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void ItemWindow::extendWindowFrame() {
    MARGINS margins;
    margins.cxLeftWidth = 0;
    margins.cxRightWidth = 0;
    margins.cyTopHeight = captionHeight();
    margins.cyBottomHeight = 0;
    if (FAILED(DwmExtendFrameIntoClientArea(hwnd, &margins))) {
        debugPrintf(L"Unable to create custom frame!\n");
    }
}

LRESULT ItemWindow::hitTestNCA(POINT cursor) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-c-hittestnca-function
    // the default window proc handles the left, right, and bottom edges
    // so only need to check top edge and caption
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect); // TODO what about the shadow??

    if (cursor.x < windowRect.left || cursor.x >= windowRect.right)
        return HTNOWHERE;

    if (cursor.y < windowRect.top || cursor.y >= windowRect.bottom) {
        return HTNOWHERE;
    } else if (cursor.y < windowRect.top + RESIZE_MARGIN) {
        if (cursor.x < windowRect.left + RESIZE_MARGIN * 2)
            return HTTOPLEFT;
        else if (cursor.x >= windowRect.right - RESIZE_MARGIN * 2)
            return HTTOPRIGHT;
        else
            return HTTOP;
    } else {
        return HTCAPTION; // can drag anywhere else in window to move!
    }
}

void ItemWindow::onPaint(PAINTSTRUCT paint) {
    // from https://docs.microsoft.com/en-us/windows/win32/dwm/customframe?redirectedfrom=MSDN#appendix-b-painting-the-caption-title
    // TODO clean this up
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    // the colors won't be right in many cases and it seems like there's no easy way to fix that
    // https://github.com/res2k/Windows10Colors
    HTHEME theme = OpenThemeData(hwnd, L"CompositedWindow::Window");
    if (!theme)
        return;

    HDC hdcPaint = CreateCompatibleDC(paint.hdc);
    if (hdcPaint) {
        int width = rectWidth(clientRect);
        int height = captionHeight();

        // Define the BITMAPINFO structure used to draw text.
        // Note that biHeight is negative. This is done because
        // DrawThemeTextEx() needs the bitmap to be in top-to-bottom
        // order.
        BITMAPINFO bitmapInfo = {};
        bitmapInfo.bmiHeader.biSize            = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth           = width;
        bitmapInfo.bmiHeader.biHeight          = -height;
        bitmapInfo.bmiHeader.biPlanes          = 1;
        bitmapInfo.bmiHeader.biBitCount        = 32;
        bitmapInfo.bmiHeader.biCompression     = BI_RGB;

        HBITMAP bitmap = CreateDIBSection(paint.hdc, &bitmapInfo, DIB_RGB_COLORS,
                                          nullptr, nullptr, 0);
        if (bitmap) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(hdcPaint, bitmap);

            // Setup the theme drawing options.
            DTTOPTS textOpts = {sizeof(DTTOPTS)};
            // COLOR_INACTIVECAPTIONTEXT doesn't work in Windows 10
            // the documentation says COLOR_CAPTIONTEXT isn't supported either but it seems to work
            textOpts.crText = GetActiveWindow() == hwnd ? GetSysColor(COLOR_CAPTIONTEXT)
                : INACTIVE_CAPTION_COLOR;
            textOpts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;

            // Select a font.
            LOGFONT logFont;
            HFONT font = nullptr, oldFont = nullptr;
            if (SUCCEEDED(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont))) {
                font = CreateFontIndirect(&logFont);
                oldFont = (HFONT)SelectObject(hdcPaint, font);
            }

            int iconSize = GetSystemMetrics(SM_CXSMICON);
            int buttonWidth = GetSystemMetrics(SM_CXSIZE);
            SIZE titleSize = {};
            GetTextExtentPoint32(hdcPaint, title, (int)lstrlen(title), &titleSize);
            // include padding on the right side of the text; makes it look more centered
            int headerWidth = iconSize + WINDOW_ICON_PADDING * 2 + titleSize.cx;
            int headerLeft = (width - headerWidth) / 2;
            if (headerLeft < buttonWidth + WINDOW_ICON_PADDING)
                headerLeft = buttonWidth + WINDOW_ICON_PADDING;

            DrawIconEx(hdcPaint, clientRect.left + headerLeft, clientRect.top + CAPTION_PADDING,
                iconSmall, iconSize, iconSize, 0, nullptr, DI_NORMAL);

            // Draw the title.
            RECT paintRect = clientRect;
            paintRect.top += CAPTION_PADDING;
            paintRect.right -= buttonWidth; // close button width
            paintRect.left += headerLeft + iconSize + WINDOW_ICON_PADDING;
            paintRect.bottom = captionHeight();
            DrawThemeTextEx(theme, hdcPaint, 0, 0, title, -1,
                            DT_LEFT | DT_WORD_ELLIPSIS, &paintRect, &textOpts);

            // Blit text to the frame.
            BitBlt(paint.hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY);

            SelectObject(hdcPaint, oldBitmap);
            if (oldFont)
                SelectObject(hdcPaint, oldFont);
            if (font)
                DeleteObject(font);
            DeleteObject(bitmap);
        }
        DeleteDC(hdcPaint);
    }
    CloseThemeData(theme);
}

void ItemWindow::hideInTaskbar() {
    CComPtr<ITaskbarList> taskbar;
    if (SUCCEEDED(taskbar.CoCreateInstance(__uuidof(TaskbarList)))) {
        taskbar->DeleteTab(hwnd);
    }
}

void ItemWindow::showInTaskbar() {
    CComPtr<ITaskbarList> taskbar;
    if (SUCCEEDED(taskbar.CoCreateInstance(__uuidof(TaskbarList)))) {
        taskbar->AddTab(hwnd);
    }
}

void ItemWindow::openChild(CComPtr<IShellItem> childItem) {
    childItem = resolveLink(childItem);
    if (child) {
        int compare;
        if (SUCCEEDED(child->item->Compare(childItem, SICHINT_CANONICAL, &compare))
                && compare == 0) {
            return; // already open
        }
        closeChild();
    }
    child = createItemWindow(this, childItem);
    SIZE size = child->preserveSize() ? storedChildSize : child->requestedSize();
    POINT pos = childPos();
    // will flush message queue
    child->create({pos.x, pos.y, pos.x + size.cx, pos.y + size.cy}, SW_SHOWNOACTIVATE);
}

void ItemWindow::closeChild() {
    if (child) {
        child->parent = nullptr;
        child->close();
        child = nullptr;
    }
}

void ItemWindow::openParent() {
    CComPtr<IShellItem> parentItem;
    if (SUCCEEDED(item->GetParent(&parentItem))) {
        parent = createItemWindow(nullptr, parentItem);
        parent->child = this;
        SIZE size = parent->requestedSize();
        POINT pos = parentPos();
        parent->create({pos.x - size.cx, pos.y, pos.x, pos.y + size.cy}, SW_SHOWNORMAL);
        ShowWindow(parentButton, SW_HIDE);
        hideInTaskbar();
    }
}

void ItemWindow::clearParent() {
    if (parent && parent->child == this) {
        parent->child = nullptr;
        parent->onChildDetached();
    }
    parent = nullptr;
}

void ItemWindow::detachFromParent() {
    clearParent();
    ShowWindow(parentButton, SW_SHOW);
    showInTaskbar();
}

void ItemWindow::onChildDetached() {}

POINT ItemWindow::childPos() {
    RECT windowRect = {}, clientRect = {};
    // https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
    // GetWindowRect includes the drop shadow! (why??)
    GetWindowRect(hwnd, &windowRect);
    GetClientRect(hwnd, &clientRect);
    return {windowRect.left + clientRect.right, windowRect.top};
}

POINT ItemWindow::parentPos() {
    RECT windowRect = {};
    GetWindowRect(hwnd, &windowRect);
    POINT shadow = {windowRect.left, windowRect.top};
    ScreenToClient(hwnd, &shadow); // determine size of drop shadow
    return {windowRect.left - shadow.x * 2, windowRect.top};
}

CComPtr<IShellItem> ItemWindow::resolveLink(CComPtr<IShellItem> linkItem) {
    // https://stackoverflow.com/a/46064112
    CComPtr<IShellLink> link;
    if (SUCCEEDED(linkItem->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) {
        if (SUCCEEDED(link->Resolve(hwnd, SLR_UPDATE))) {
            CComHeapPtr<ITEMIDLIST> targetPIDL;
            if (SUCCEEDED(link->GetIDList(&targetPIDL))) {
                CComPtr<IShellItem> targetItem;
                if (SUCCEEDED(SHCreateShellItem(nullptr, nullptr, targetPIDL, &targetItem))) {
                    // don't need to recurse, shortcuts to shortcuts are not allowed
                    return targetItem;
                }
            }
        } else {
            debugPrintf(L"Could not resolve link\n");
        }
    }
    return linkItem;
}

void ItemWindow::refresh() {}

/* IUnknown */

STDMETHODIMP ItemWindow::QueryInterface(REFIID id, void **obj) {
    static const QITAB interfaces[] = {
        {},
    };
    return QISearch(this, interfaces, id, obj);
}

STDMETHODIMP_(ULONG) ItemWindow::AddRef() {
    return InterlockedIncrement(&refCount);
}

STDMETHODIMP_(ULONG) ItemWindow::Release() {
    long r = InterlockedDecrement(&refCount);
    if (r == 0) {
        debugPrintf(L"Delete %s\n", &*title);
        delete this; // TODO ???
    }
    return r;
}


LRESULT CALLBACK captionButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) {
        // TODO is there a better way to do this?
        int buttonState = Button_GetState(hwnd);
        int themeState;
        if (buttonState & BST_PUSHED)
            themeState = PBS_PRESSED;
        else if (buttonState & BST_HOT)
            themeState = PBS_HOT;
        else
            themeState = PBS_NORMAL;

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HTHEME theme = OpenThemeData(hwnd, L"Button");
        if (theme) {
            RECT buttonRect;
            GetClientRect(hwnd, &buttonRect);
            buttonRect = {buttonRect.left - 1, buttonRect.top,
                          buttonRect.right + 1, buttonRect.bottom + 1};
            if (themeState == PBS_NORMAL) {
                // hacky way to hide button while still rendering text properly
                InflateRect(&buttonRect, 8, 8);
            }
            DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, themeState, &buttonRect, &ps.rcPaint);

            RECT contentRect;
            if (SUCCEEDED(GetThemeBackgroundContentRect(theme, hdc, BP_PUSHBUTTON, 
                    themeState, &buttonRect, &contentRect))) {
                wchar_t buttonText[32];
                GetWindowText(hwnd, buttonText, 32);
                HFONT oldFont = (HFONT) SelectObject(hdc, symbolFont);
                SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, buttonText, -1, &contentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }

            CloseThemeData(theme);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
