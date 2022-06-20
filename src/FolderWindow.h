#pragma once
#include <common.h>

#include "ItemWindow.h"

namespace chromabrowse {

class FolderWindow : public ItemWindow, public IServiceProvider, public ICommDlgBrowser {
public:
    static void init();

    FolderWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);

    bool preserveSize() override;
    SIZE requestedSize() override;

    bool handleTopLevelMessage(MSG *msg) override;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IServiceProvider
    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void **ppv) override;
    // ICommDlgBrowser
    STDMETHODIMP OnDefaultCommand(IShellView *view) override;
    STDMETHODIMP OnStateChange(IShellView *view, ULONG change) override;
    STDMETHODIMP IncludeObject(IShellView *view, PCUITEMID_CHILD pidl) override;

protected:
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

    void onCreate() override;
    void onDestroy() override;
    void onActivate(WORD state, HWND prevWindow) override;
    void onSize(int width, int height) override;

    void onChildDetached() override;

    void refresh() override;

    CComPtr<IExplorerBrowser> browser;

private:
    const wchar_t * className() override;

    virtual bool initBrowser();
    void selectionChanged();
    void resultsFolderFallback();

    CComPtr<IShellView> shellView;
    CComPtr<IPropertyBag> propBag;

    SIZE lastSize = {-1, -1};
    bool sizeChanged = false;

    // jank flags
    bool ignoreNextSelection = false;
    bool activateOnShiftRelease = false;
};

} // namespace
