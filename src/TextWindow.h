#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class TextWindow : public ItemWindow {
public:
    static void init();
    static void uninit();

    TextWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    bool handleTopLevelMessage(MSG *msg) override;

protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onActivate(WORD state, HWND prevWindow);
    void onSize(int width, int height);

private:
    const wchar_t * className() override;

    bool loadText();
    bool saveText();

    static LRESULT CALLBACK richEditProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    enum Encoding {
        FAIL, UTF8, UTF8BOM, UTF16LE, UTF16BE
    };

    HWND edit;
    Encoding encoding = FAIL;
    int scrollAccum = 0; // for high resolution scrolling
};

} // namespace
