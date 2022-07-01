#include "TextWindow.h"
#include "RectUtils.h"
#include "resource.h"
#include <windowsx.h>
#include <shlobj.h>
#include <Richedit.h>
#include <VersionHelpers.h>

namespace chromabrowse {

const wchar_t TEXT_WINDOW_CLASS[] = L"Text Window";

const wchar_t TEXT_FONT_FACE[] = L"Consolas";
const int TEXT_FONT_HEIGHT = 16;

const uint8_t BOM_UTF8BOM[] = {0xEF, 0xBB, 0xBF};
const uint8_t BOM_UTF16LE[] = {0xFF, 0xFE};
const uint8_t BOM_UTF16BE[] = {0xFE, 0xFF};
#define CHECK_BOM(buffer, size, bom) \
    ((size) >= sizeof(bom) && memcmp((buffer), (bom), sizeof(bom)) == 0)

static HACCEL textAccelTable;
static HFONT monoFont = 0;

void TextWindow::init() {
    WNDCLASS wndClass = createWindowClass(TEXT_WINDOW_CLASS);
    RegisterClass(&wndClass);
    // http://www.jose.it-berater.org/richedit/rich_edit_control.htm
    LoadLibrary(L"Msftedit.dll");

    monoFont = CreateFont(TEXT_FONT_HEIGHT, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
        DEFAULT_PITCH | FF_DONTCARE, TEXT_FONT_FACE);

    textAccelTable = LoadAccelerators(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_TEXT_ACCEL));
}

void TextWindow::uninit() {
    if (monoFont)
        DeleteFont(monoFont);
}

TextWindow::TextWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item)
    : ItemWindow(parent, item) {}

const wchar_t * TextWindow::className() {
    return TEXT_WINDOW_CLASS;
}

void TextWindow::onCreate() {
    ItemWindow::onCreate();
    edit = CreateWindow(MSFTEDIT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL
        | ES_NOHIDESEL | ES_SAVESEL | ES_SELECTIONBAR,
        0, 0, 0, 0,
        hwnd, nullptr, GetWindowInstance(hwnd), nullptr);
    SetWindowSubclass(edit, richEditProc, 0, (DWORD_PTR)this);
    if (monoFont)
        SendMessage(edit, WM_SETFONT, (WPARAM)monoFont, FALSE);
    SendMessage(edit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    if (IsWindows8OrGreater())
        SendMessage(edit, EM_SETEDITSTYLE, SES_MULTISELECT, SES_MULTISELECT);

    if (!loadText())
        SendMessage(edit, EM_SETOPTIONS, ECOOP_OR, ECO_READONLY);
    SendMessage(edit, EM_SETMODIFY, FALSE, 0);
}

void TextWindow::onActivate(WORD state, HWND prevWindow) {
    ItemWindow::onActivate(state, prevWindow);
    if (state != WA_INACTIVE) {
        SetFocus(edit);
    }
}

void TextWindow::onSize(int width, int height) {
    ItemWindow::onSize(width, height);
    RECT body = windowBody();
    MoveWindow(edit, body.left, body.top, rectWidth(body), rectHeight(body), TRUE);
}

bool TextWindow::handleTopLevelMessage(MSG *msg) {
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_TAB)
        return false;
    if (TranslateAccelerator(hwnd, textAccelTable, msg))
        return true;
    return ItemWindow::handleTopLevelMessage(msg);
}

LRESULT TextWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SAVE:
                    if (saveText())
                        SendMessage(edit, EM_SETMODIFY, FALSE, 0);
                    return 0;
            }
    }

    return ItemWindow::handleMessage(message, wParam, lParam);
}

bool TextWindow::loadText() {
    encoding = FAIL;
    CComHeapPtr<uint8_t> buffer; // null terminated!
    ULONG size;
    {
        CComPtr<IBindCtx> context;
        if (checkHR(CreateBindCtx(0, &context))) {
            BIND_OPTS options = {sizeof(BIND_OPTS), 0, STGM_READ | STGM_SHARE_DENY_NONE, 0};
            checkHR(context->SetBindOptions(&options));
        }
        CComPtr<IStream> stream;
        if (!checkHR(item->BindToHandler(context, BHID_Stream, IID_PPV_ARGS(&stream))))
            return false;
        ULARGE_INTEGER largeSize;
        if (!checkHR(IStream_Size(stream, &largeSize)))
            return false;
        if (largeSize.QuadPart > (ULONGLONG)(ULONG)-1) {
            debugPrintf(L"Too large!\n");
            return false;
        }
        size = (ULONG)largeSize.QuadPart;
        buffer.AllocateBytes(size + 2);
        if (!checkHR(IStream_Read(stream, buffer, (ULONG)size)))
            return false;
        buffer[size] = buffer[size + 1] = 0;
    }

    // https://docs.microsoft.com/en-us/windows/win32/intl/using-byte-order-marks
    if (CHECK_BOM(buffer, size, BOM_UTF16LE)) {
        encoding = UTF16LE;
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(uint8_t *)(buffer + 2));
    } else if (CHECK_BOM(buffer, size, BOM_UTF16BE)) {
        encoding = UTF16BE;
        wchar_t *wcBuffer = (wchar_t *)(void *)buffer;
        ULONG wcSize = size / 2;
        for (ULONG i = 1; i < wcSize; i++)
            wcBuffer[i] = _byteswap_ushort(wcBuffer[i]);
        SendMessage(edit, WM_SETTEXT, 0, (LPARAM)(wcBuffer + 1));
    } else { // assume UTF-8
        uint8_t *utf8String = buffer;
        if (CHECK_BOM(buffer, size, BOM_UTF8BOM)) {
            encoding = UTF8BOM;
            utf8String += 3;
        } else {
            encoding = UTF8;
        }
        SETTEXTEX setText = {};
        setText.codepage = CP_UTF8;
        SendMessage(edit, EM_SETTEXTEX, (WPARAM)&setText, (LPARAM)utf8String);
    }
    debugPrintf(L"Encoding %d\n", encoding);
    return true;
}

bool TextWindow::saveText() {
    debugPrintf(L"Saving!\n");

    CComHeapPtr<uint8_t> buffer; // not null terminated!
    ULONG bufferSize;
    if (encoding == UTF8 || encoding == UTF8BOM) {
        GETTEXTLENGTHEX getLength = {};
        getLength.flags = GTL_USECRLF | GTL_NUMBYTES; // TODO
        getLength.codepage = CP_UTF8;
        ULONG utf8Size = SendMessage(edit, EM_GETTEXTLENGTHEX, (WPARAM)&getLength, 0);
        uint8_t *utf8String;
        if (encoding == UTF8BOM) {
            bufferSize = utf8Size + 3;
            buffer.AllocateBytes(bufferSize);
            utf8String = buffer + 3;
            CopyMemory(buffer, BOM_UTF8BOM, sizeof(BOM_UTF8BOM));
        } else {
            bufferSize = utf8Size;
            buffer.AllocateBytes(bufferSize);
            utf8String = buffer;
        }
        GETTEXTEX getText = {};
        getText.cb = utf8Size;
        getText.flags = GT_USECRLF; // TODO
        getText.codepage = CP_UTF8;
        SendMessage(edit, EM_GETTEXTEX, (WPARAM)&getText, (LPARAM)utf8String);
    } else {
        debugPrintf(L"Unrecognized encoding\n");
        return false;
    }
    return true;
}

LRESULT CALLBACK TextWindow::richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData) {
    if (message == WM_MOUSEWHEEL) {
        // override smooth scrolling
        TextWindow *window = (TextWindow *)refData;
        window->scrollAccum += GET_WHEEL_DELTA_WPARAM(wParam);

        UINT linesPerClick = 3;
        SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerClick, 0);
        float lineDelta = (float)WHEEL_DELTA / linesPerClick;
        int lines = (int)floor(window->scrollAccum / lineDelta);
        window->scrollAccum -= (int)(lines * lineDelta);
        SendMessage(hwnd, EM_LINESCROLL, 0, -lines);
        return 0;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

} // namespace
