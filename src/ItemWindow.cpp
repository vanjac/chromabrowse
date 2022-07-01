#include "ItemWindow.h"
#include "ItemWindowFactory.h"
#include "RectUtil.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <vssym32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <VersionHelpers.h>

namespace chromabrowse {

const wchar_t *CHAIN_OWNER_CLASS = L"Chain";
const wchar_t *WINDOW_THEME = L"CompositedWindow::Window";

// dimensions
const int RESIZE_MARGIN = 8; // TODO use some system metric?
const int WINDOW_ICON_PADDING = 4;
const int RENAME_BOX_PADDING = 2; // with border
const int SNAP_DISTANCE = 32;
const SIZE DEFAULT_SIZE = {450, 450};
// colors
// this is the color used in every high-contrast theme
// regular light mode theme uses #999999
const COLORREF WIN10_INACTIVE_CAPTION_COLOR = 0x636363;

static HANDLE symbolFontHandle = 0;
static HFONT captionFont = 0, symbolFont = 0;

long numOpenWindows;
CComPtr<ItemWindow> activeWindow;

int ItemWindow::CAPTION_HEIGHT = 0;
HACCEL ItemWindow::accelTable;

void ItemWindow::init() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASS chainClass = {};
    chainClass.lpszClassName = CHAIN_OWNER_CLASS;
    chainClass.lpfnWndProc = DefWindowProc;
    chainClass.hInstance = hInstance;
    RegisterClass(&chainClass);

    RECT adjustedRect = {};
    AdjustWindowRectEx(&adjustedRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    CAPTION_HEIGHT = -adjustedRect.top; // = 31

    HTHEME theme = OpenThemeData(nullptr, WINDOW_THEME);
    if (theme) {
        LOGFONT logFont;
        if (SUCCEEDED(GetThemeSysFont(theme, TMT_CAPTIONFONT, &logFont)))
            captionFont = CreateFontIndirect(&logFont);
        CloseThemeData(theme);
    }

    HRSRC symbolFontResource = FindResource(hInstance, MAKEINTRESOURCE(IDR_ICON_FONT), RT_FONT);
    HGLOBAL symbolFontAddr = LoadResource(hInstance, symbolFontResource);
    DWORD count = 1;
    symbolFontHandle = (HFONT)AddFontMemResourceEx(symbolFontAddr,
        SizeofResource(hInstance, symbolFontResource), 0, &count);
    symbolFont = CreateFont(12, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");

    accelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ITEM_ACCEL));
}

void ItemWindow::uninit() {
    if (captionFont)
        DeleteFont(captionFont);
    if (symbolFont)
        DeleteFont(symbolFont);
    if (symbolFontHandle)
        RemoveFontMemResourceEx(symbolFontHandle);
}

WNDCLASS ItemWindow::createWindowClass(const wchar_t *name) {
    WNDCLASS wndClass = {};
    wndClass.lpfnWndProc = ItemWindow::windowProc;
    wndClass.hInstance = GetModuleHandle(nullptr);
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
        POINT testPoint = {rect.left, rect.top + CAPTION_HEIGHT};
        HMONITOR nearestMonitor = MonitorFromPoint(testPoint, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfo(nearestMonitor, &monitorInfo);
        if (testPoint.x < monitorInfo.rcWork.left)
            OffsetRect(&rect, monitorInfo.rcWork.left - testPoint.x, 0);
        if (testPoint.y > monitorInfo.rcWork.bottom)
            OffsetRect(&rect, 0, monitorInfo.rcWork.bottom - testPoint.y);
    }

    HWND owner;
    if (parent)
        owner = GetWindowOwner(parent->hwnd);
    else if (child)
        owner = GetWindowOwner(child->hwnd);
    else
        owner = createChainOwner();

    HWND createHwnd = CreateWindow(
        className(), title,
        // WS_CLIPCHILDREN fixes drawing glitches with the scrollbars
        (WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN,
        rect.left, rect.top, rectWidth(rect), rectHeight(rect),
        owner, nullptr, GetModuleHandle(nullptr), this);
    if (!createHwnd) {
        debugPrintf(L"Couldn't create window\n");
        return false;
    }
    SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) + 1);

    ShowWindow(createHwnd, showCommand);

    AddRef(); // keep window alive while open
    InterlockedIncrement(&numOpenWindows);
    return true;
}

HWND ItemWindow::createChainOwner() {
    HWND window = CreateWindow(CHAIN_OWNER_CLASS, nullptr, WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), 0); // user data stores num owned windows
    ShowWindow(window, SW_SHOWNORMAL); // show in taskbar
    return window;
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
    RECT clientRect, statusRect;
    GetClientRect(hwnd, &clientRect);
    GetClientRect(statusBar, &statusRect);
    return {0, CAPTION_HEIGHT + statusRect.bottom, clientRect.right, clientRect.bottom};
}

LRESULT ItemWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (contextMenu3) {
        LRESULT result;
        if (SUCCEEDED(contextMenu3->HandleMenuMsg2(message, wParam, lParam, &result)))
            return result;
    } else if (contextMenu2) {
        if (SUCCEEDED(contextMenu2->HandleMenuMsg(message, wParam, lParam)))
            return 0;
    }

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
            if (InterlockedDecrement(&numOpenWindows) == 0)
                PostQuitMessage(0);
            Release(); // allow window to be deleted
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
        case WM_NCLBUTTONDBLCLK: {
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientCursor = cursor;
            ScreenToClient(hwnd, &clientCursor);
            if (wParam == HTCAPTION && PtInRect(&proxyRect, clientCursor)) {
                if (GetKeyState(VK_MENU) < 0)
                    openProxyProperties();
                else
                    invokeProxyDefaultVerb(cursor);
                return 0;
            }
            break;
        }
        case WM_NCRBUTTONUP: { // WM_CONTEXTMENU doesn't seem to work in the caption
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT clientCursor = cursor;
            ScreenToClient(hwnd, &clientCursor);
            if (wParam == HTCAPTION && PtInRect(&proxyRect, clientCursor)) {
                openProxyContextMenu(cursor);
                return 0;
            }
            break; // pass to DefWindowProc
        }
        case WM_NOTIFY: {
            NMHDR *notif = (NMHDR *)lParam;
            if (notif->hwndFrom == tooltip && notif->code == TTN_SHOW) {
                // position tooltip on top of title
                RECT tooltipRect = titleRect;
                MapWindowRect(hwnd, nullptr, &tooltipRect);
                SendMessage(tooltip, TTM_ADJUSTRECT, TRUE, (LPARAM)&tooltipRect);
                SetWindowPos(tooltip, nullptr, tooltipRect.left, tooltipRect.top, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            if ((HWND)lParam == parentButton && HIWORD(wParam) == BN_CLICKED) {
                openParent();
                return 0;
            } else if ((HWND)lParam == renameBox && HIWORD(wParam) == EN_KILLFOCUS) {
                if (IsWindowVisible(renameBox))
                    completeRename();
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
                case ID_PROXY_MENU: {
                    POINT menuPos = {proxyRect.right, proxyRect.top};
                    ClientToScreen(hwnd, &menuPos);
                    openProxyContextMenu(menuPos);
                    return 0;
                }
                case ID_HELP:
                    ShellExecute(nullptr, L"open", L"https://github.com/vanjac/chromabrowse/wiki",
                        nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
            }
            break;
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

    HMODULE instance = GetWindowInstance(hwnd);

    tooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, instance, nullptr);
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (captionFont)
        SendMessage(tooltip, WM_SETFONT, (WPARAM)captionFont, FALSE);
    TOOLINFO toolInfo = {};
    toolInfo.cbSize = sizeof(toolInfo);
    toolInfo.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    toolInfo.hwnd = hwnd;
    toolInfo.hinst = instance;
    toolInfo.lpszText = title;
    SendMessage(tooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

    CComPtr<IShellItem> parentItem;
    bool showParentButton = !parent && SUCCEEDED(item->GetParent(&parentItem));
    parentButton = CreateWindow(L"BUTTON", L"\uE96F", // ChevronLeftSmall
        (showParentButton ? WS_VISIBLE : 0) | WS_CHILD | BS_PUSHBUTTON,
        0, 0, GetSystemMetrics(SM_CXSIZE), CAPTION_HEIGHT,
        hwnd, nullptr, instance, nullptr);
    SetWindowSubclass(parentButton, captionButtonProc, 0, 0);

    // will be positioned in beginRename
    renameBox = CreateWindow(L"EDIT", nullptr,
        WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwnd, nullptr, instance, nullptr);
    SetWindowSubclass(renameBox, renameBoxProc, 0, (DWORD_PTR)this);
    if (captionFont)
        SendMessage(renameBox, WM_SETFONT, (WPARAM)captionFont, FALSE);

    statusBar = CreateWindow(STATUSCLASSNAME, nullptr,
        CCS_TOP | CCS_NOPARENTALIGN | SBARS_TOOLTIPS | WS_CHILD | WS_VISIBLE,
        0, CAPTION_HEIGHT, 0, 0,
        hwnd, nullptr, instance, nullptr);
    int initParts[] = {0, 0, -1};
    SendMessage(statusBar, SB_SETPARTS, 3, (LPARAM)initParts);
    CreateThread(nullptr, 0, setStatusBarThread, this, 0, nullptr);
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
    HWND owner = GetWindowOwner(hwnd);
    if (SetWindowLongPtr(owner, GWLP_USERDATA, GetWindowLongPtr(owner, GWLP_USERDATA) - 1) == 1)
        PostMessage(owner, WM_CLOSE, 0, 0); // last window in group
}

void ItemWindow::onActivate(WORD state, HWND) {
    // for DWM custom frame
    // make sure frame is correct if window is maximized
    extendWindowFrame();

    RECT captionRect;
    GetClientRect(hwnd, &captionRect);
    captionRect.bottom = CAPTION_HEIGHT;
    InvalidateRect(hwnd, &captionRect, FALSE); // make sure to update caption text color

    if (state != WA_INACTIVE) {
        activeWindow = this;
        HWND owner = GetWindowOwner(hwnd);
        SetWindowText(owner, title); // update taskbar / alt-tab
        PostMessage(owner, WM_SETICON, ICON_BIG, (LPARAM)iconLarge);
        PostMessage(owner, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
    }
}

void ItemWindow::onSize(int width, int) {
    windowRectChanged();
    if (parent && preserveSize()) {
        RECT rect;
        GetWindowRect(hwnd, &rect);
        parent->storedChildSize = rectSize(rect);
    }
    SetWindowPos(statusBar, nullptr, 0, 0, width, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    int parts[] = {width / 3, width * 2 / 3, -1};
    SendMessage(statusBar, SB_SETPARTS, 3, (LPARAM)parts);
}

void ItemWindow::windowRectChanged() {
    if (child) {
        child->setPos(childPos());
    }
}

void ItemWindow::extendWindowFrame() {
    MARGINS margins;
    margins.cxLeftWidth = 0;
    margins.cxRightWidth = 0;
    margins.cyTopHeight = CAPTION_HEIGHT;
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
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    HDC hdcPaint = CreateCompatibleDC(paint.hdc);
    if (!hdcPaint)
        return;

    int width = rectWidth(clientRect);
    int height = CAPTION_HEIGHT;

    // bitmap buffer for drawing caption
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize         = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth        = width;
    bitmapInfo.bmiHeader.biHeight       = -height; // top-to-bottom order for DrawThemeTextEx()
    bitmapInfo.bmiHeader.biPlanes       = 1;
    bitmapInfo.bmiHeader.biBitCount     = 32;
    bitmapInfo.bmiHeader.biCompression  = BI_RGB;
    HBITMAP bitmap = CreateDIBSection(paint.hdc, &bitmapInfo, DIB_RGB_COLORS,
                                      nullptr, nullptr, 0);
    if (!bitmap) {
        DeleteDC(hdcPaint);
        return;
    }
    HBITMAP oldBitmap = SelectBitmap(hdcPaint, bitmap);

    int iconSize = GetSystemMetrics(SM_CXSMICON);
    int buttonWidth = GetSystemMetrics(SM_CXSIZE); // TODO use DWMWA_CAPTION_BUTTON_BOUNDS

    HFONT oldFont = nullptr;
    if (captionFont)
        oldFont = SelectFont(hdcPaint, captionFont); // must be set here for GetTextExtentPoint32

    SIZE titleSize = {};
    GetTextExtentPoint32(hdcPaint, title, (int)lstrlen(title), &titleSize);
    // include padding on the right side of the text; makes it look more centered
    int headerWidth = iconSize + WINDOW_ICON_PADDING * 2 + titleSize.cx;
    int headerLeft = (width - headerWidth) / 2;
    bool truncateTitle = headerLeft < buttonWidth + WINDOW_ICON_PADDING;
    if (truncateTitle) {
        headerLeft = buttonWidth + WINDOW_ICON_PADDING;
        headerWidth = width - buttonWidth - WINDOW_ICON_PADDING - headerLeft;
    }
    // store for hit testing proxy icon/text
    proxyRect = {headerLeft, 0, headerLeft + headerWidth, CAPTION_HEIGHT};

    if (truncateTitle) {
        TOOLINFO toolInfo = {};
        toolInfo.cbSize = sizeof(toolInfo);
        toolInfo.hwnd = hwnd;
        toolInfo.rect = proxyRect;
        SendMessage(tooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&toolInfo); // rect to trigger tooltip
        SendMessage(tooltip, TTM_ACTIVATE, TRUE, 0);
    } else {
        SendMessage(tooltip, TTM_ACTIVATE, FALSE, 0);
    }

    DrawIconEx(hdcPaint, headerLeft, (CAPTION_HEIGHT - iconSize) / 2, iconSmall,
               iconSize, iconSize, 0, nullptr, DI_NORMAL);

    // the colors won't be right in many cases and it seems like there's no easy way to fix that
    // https://github.com/res2k/Windows10Colors
    HTHEME windowTheme = OpenThemeData(hwnd, WINDOW_THEME);
    if (windowTheme) {
        DTTOPTS textOpts = {sizeof(DTTOPTS)};
        bool isActive = GetActiveWindow() == hwnd;
        if (IsWindows10OrGreater()) {
            // COLOR_INACTIVECAPTIONTEXT doesn't work in Windows 10
            // the documentation says COLOR_CAPTIONTEXT isn't supported either but it seems to work
            textOpts.crText = isActive ? GetSysColor(COLOR_CAPTIONTEXT)
                : WIN10_INACTIVE_CAPTION_COLOR;
        } else {
            textOpts.crText = GetSysColor(COLOR_CAPTIONTEXT);
        }
        textOpts.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;

        titleRect = clientRect;
        titleRect.top = (CAPTION_HEIGHT - titleSize.cy) / 2;
        titleRect.right -= buttonWidth; // close button width
        titleRect.left = headerLeft + iconSize + WINDOW_ICON_PADDING;
        titleRect.bottom = titleRect.top + titleSize.cy;
        DrawThemeTextEx(windowTheme, hdcPaint, 0, 0, title, -1,
                        DT_LEFT | DT_WORD_ELLIPSIS, &titleRect, &textOpts);

        CloseThemeData(windowTheme);
    }

    BitBlt(paint.hdc, 0, 0, width, height, hdcPaint, 0, 0, SRCCOPY);

    if (oldFont)
        SelectFont(hdcPaint, oldFont);
    SelectBitmap(hdcPaint, oldBitmap);
    DeleteBitmap(bitmap);
    DeleteDC(hdcPaint);
}

void ItemWindow::openChild(CComPtr<IShellItem> childItem) {
    childItem = resolveLink(hwnd, childItem);
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
        RECT windowRect;
        GetWindowRect(hwnd, &windowRect);
        parent->storedChildSize = rectSize(windowRect);

        SIZE size = parent->requestedSize();
        POINT pos = parentPos();
        parent->create({pos.x - size.cx, pos.y, pos.x, pos.y + size.cy}, SW_SHOWNORMAL);
        ShowWindow(parentButton, SW_HIDE);
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
    SetActiveWindow(parent->hwnd); // focus parent in chain
    clearParent();
    ShowWindow(parentButton, SW_SHOW);
    HWND prevOwner = GetWindowOwner(hwnd);
    HWND owner = createChainOwner();
    int numChildren = 0;
    for (ItemWindow *next = this; next != nullptr; next = next->child) {
        SetWindowLongPtr(next->hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner);
        numChildren++;
    }
    SetWindowLongPtr(owner, GWLP_USERDATA, (LONG_PTR)numChildren);
    SetWindowLongPtr(prevOwner, GWLP_USERDATA,
        GetWindowLongPtr(prevOwner, GWLP_USERDATA) - numChildren);
    SetActiveWindow(hwnd); // bring this chain to front
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

void ItemWindow::refresh() {}

bool ItemWindow::statusBarText(HWND bar) {
    CComPtr<IQueryInfo> queryInfo;
    HRESULT result = item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&queryInfo));
    if (FAILED(result)) {
        debugPrintf(L"bind to handler failed %d\n", result);
        return false;
    }
    CComHeapPtr<wchar_t> text;
    if (FAILED(queryInfo->GetInfoTip(QITIPF_USESLOWTIP, &text)))
        return false;
    int part = 0;
    wchar_t *partStart = text;
    for (wchar_t *c = text; *c; c++) {
        if (*c == '\n') {
            *c = 0;
            SendMessage(bar, SB_SETTEXT, part++, (LPARAM)partStart);
            partStart = c + 1;
        }
    }
    SendMessage(bar, SB_SETTEXT, part, (LPARAM)partStart);
    return true;
}

DWORD WINAPI ItemWindow::setStatusBarThread(void *param) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 0;
    ItemWindow *itemWindow = (ItemWindow *)param;
    if (itemWindow->statusBarText(itemWindow->statusBar))
        debugPrintf(L"successfully set status bar text\n");
    else
        debugPrintf(L"failed to set status bar text\n");
    CoUninitialize();
    return 0;
}

void ItemWindow::invokeProxyDefaultVerb(POINT point) {
    // https://devblogs.microsoft.com/oldnewthing/20040930-00/?p=37693
    CComPtr<IContextMenu> contextMenu;
    if (FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    if (SUCCEEDED(contextMenu->QueryContextMenu(popupMenu, 0, 1, 0x7FFF, CMF_DEFAULTONLY))) {
        UINT id = GetMenuDefaultItem(popupMenu, FALSE, 0);
        if (id != (UINT)-1) {
            CMINVOKECOMMANDINFOEX info = {};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE
                | CMIC_MASK_ASYNCOK | CMIC_MASK_FLAG_LOG_USAGE;
            info.hwnd = hwnd;
            info.lpVerb = MAKEINTRESOURCEA(id - 1);
            info.lpVerbW = MAKEINTRESOURCEW(id - 1);
            info.nShow = SW_SHOWNORMAL;
            info.ptInvoke = point;
            contextMenu->InvokeCommand((CMINVOKECOMMANDINFO*)&info);
        }
    }
    DestroyMenu(popupMenu);
}

void ItemWindow::openProxyProperties() {
    CComHeapPtr<ITEMIDLIST> idList;
    if (SUCCEEDED(SHGetIDListFromObject(item, &idList))) {
        SHELLEXECUTEINFO info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_INVOKEIDLIST;
        info.lpVerb = L"properties";
        info.lpIDList = idList;
        info.hwnd = hwnd;
        ShellExecuteEx(&info);
    }
}

void ItemWindow::openProxyContextMenu(POINT point) {
    // https://devblogs.microsoft.com/oldnewthing/20040920-00/?p=37823 and onward
    CComPtr<IContextMenu> contextMenu;
    if (FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&contextMenu))))
        return;
    HMENU popupMenu = CreatePopupMenu();
    if (!popupMenu)
        return;
    UINT contextFlags = CMF_ITEMMENU | CMF_CANRENAME;
    if (GetKeyState(VK_SHIFT) < 0)
        contextFlags |= CMF_EXTENDEDVERBS;
    if (FAILED(contextMenu->QueryContextMenu(popupMenu, 0, 1, 0x7FFF, contextFlags))) {
        DestroyMenu(popupMenu);
        return;
    }
    contextMenu2 = contextMenu;
    contextMenu3 = contextMenu;
    int cmd = TrackPopupMenuEx(popupMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, hwnd, nullptr);
    contextMenu2 = nullptr;
    contextMenu3 = nullptr;
    if (cmd > 0) {
        cmd -= 1; // idCmdFirst

        // https://groups.google.com/g/microsoft.public.win32.programmer.ui/c/PhXQcfhYPHQ
        wchar_t verb[64];
        verb[0] = 0; // some handlers may return S_OK without touching the buffer
        if (SUCCEEDED(contextMenu->GetCommandString(cmd, GCS_VERBW, nullptr, (char*)verb, 64))
                && lstrcmpi(verb, L"rename") == 0) {
            beginRename();
        } else {
            CMINVOKECOMMANDINFOEX info = {};
            info.cbSize = sizeof(info);
            // TODO must set a thread reference
            // see https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand
            info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE
                | CMIC_MASK_ASYNCOK | CMIC_MASK_FLAG_LOG_USAGE;
            if (GetKeyState(VK_CONTROL) < 0)
                info.fMask |= CMIC_MASK_CONTROL_DOWN;
            if (GetKeyState(VK_SHIFT) < 0)
                info.fMask |= CMIC_MASK_SHIFT_DOWN;
            info.hwnd = hwnd;
            info.lpVerb = MAKEINTRESOURCEA(cmd);
            info.lpVerbW = MAKEINTRESOURCEW(cmd);
            info.nShow = SW_SHOWNORMAL;
            info.ptInvoke = point;
            contextMenu->InvokeCommand((CMINVOKECOMMANDINFO*)&info);
        }
    }
    DestroyMenu(popupMenu);
}

void ItemWindow::beginRename() {
    // update rename box rect
    int leftMargin = LOWORD(SendMessage(renameBox, EM_GETMARGINS, 0, 0));
    int renameHeight = rectHeight(titleRect) + RENAME_BOX_PADDING * 2;
    POINT renamePos = {titleRect.left - leftMargin - RENAME_BOX_PADDING,
                       (CAPTION_HEIGHT - renameHeight) / 2};
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int renameWidth = clientRect.right - GetSystemMetrics(SM_CXSIZE) - renamePos.x;
    ClientToScreen(hwnd, &renamePos);
    SetWindowPos(renameBox, nullptr, renamePos.x, renamePos.y, renameWidth, renameHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SendMessage(renameBox, WM_SETTEXT, 0, (LPARAM)&*title);
    wchar_t *ext = PathFindExtension(title);
    if (ext == title) { // files that start with a dot
        SendMessage(renameBox, EM_SETSEL, 0, -1);
    } else {
        SendMessage(renameBox, EM_SETSEL, 0, ext - title);
    }
    ShowWindow(renameBox, SW_SHOW);
}

void ItemWindow::completeRename() {
    wchar_t newName[MAX_PATH];
    SendMessage(renameBox, WM_GETTEXT, MAX_PATH, (LPARAM)newName);
    cancelRename();

    if (lstrcmp(newName, title) == 0)
        return; // names are identical, which would cause an unnecessary error message

    CComHeapPtr<wchar_t> path;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        wchar_t *fileName = PathFindFileName(path);
        int fileNameLen = lstrlen(fileName);
        int titleLen = lstrlen(title);
        if (fileNameLen > titleLen) { // if extensions are hidden in File Explorer Options
            debugPrintf(L"Appending extension %s\n", fileName + titleLen);
            StringCchCat(newName, MAX_PATH, fileName + titleLen);
        }
    }

    CComPtr<IFileOperation> operation;
    if (FAILED(operation.CoCreateInstance(__uuidof(FileOperation))))
        return;
    operation->SetOperationFlags(FOFX_ADDUNDORECORD);
    if (FAILED(operation->RenameItem(item, newName, nullptr)))
        return;
    operation->PerformOperations();
}

void ItemWindow::cancelRename() {
    ShowWindow(renameBox, SW_HIDE);
}

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
        delete this;
    }
    return r;
}

void MakeBitmapOpaque(HDC hdc, const RECT &rect) {
    // https://devblogs.microsoft.com/oldnewthing/20210915-00/?p=105687
    // thank you Raymond Chen :)
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = 1;
    bi.bmiHeader.biHeight      = 1;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    RGBQUAD bitmapBits = { 0x00, 0x00, 0x00, 0xFF };
    StretchDIBits(hdc, rect.left, rect.top, rectWidth(rect), rectHeight(rect),
                  0, 0, 1, 1, &bitmapBits, &bi,
                  DIB_RGB_COLORS, SRCPAINT);
}

LRESULT CALLBACK ItemWindow::captionButtonProc(HWND hwnd, UINT message,
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
            InflateRect(&buttonRect, 1, 1);
            if (themeState == PBS_NORMAL) {
                FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
                MakeBitmapOpaque(hdc, ps.rcPaint);
            } else {
                DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, themeState, &buttonRect,
                                    &ps.rcPaint);
            }

            RECT contentRect;
            if (SUCCEEDED(GetThemeBackgroundContentRect(theme, hdc, BP_PUSHBUTTON, 
                    themeState, &buttonRect, &contentRect))) {
                wchar_t buttonText[32];
                GetWindowText(hwnd, buttonText, 32);
                HFONT oldFont = SelectFont(hdc, symbolFont);
                SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, buttonText, -1, &contentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectFont(hdc, oldFont);
                MakeBitmapOpaque(hdc, ps.rcPaint);
            }

            CloseThemeData(theme);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ItemWindow::renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_CHAR && wParam == VK_RETURN) {
        ((ItemWindow *)refData)->completeRename();
        return 0;
    } else if (message == WM_CHAR && wParam == VK_ESCAPE) {
        ((ItemWindow *)refData)->cancelRename();
        return 0;
    } else if (message == WM_CLOSE) {
        // prevent user closing rename box (it will still be destroyed when owner is closed)
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
