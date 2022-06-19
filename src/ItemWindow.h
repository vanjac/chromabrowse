#pragma once
#include <common.h>

#include "COMUtil.h"
#include <windows.h>
#include <shobjidl.h>
#include <atlbase.h>

namespace chromabrowse {

class ItemWindow;

extern long numOpenWindows;
extern CComPtr<ItemWindow> activeWindow;

class ItemWindow : public IUnknownImpl, public IDropSource, public IDropTarget {
protected:
    // calculated in init()
    static int CAPTION_HEIGHT;
    static HACCEL accelTable;

public:
    static void init();
    static void uninit();
    static WNDCLASS createWindowClass(const wchar_t *name);

    ItemWindow(CComPtr<ItemWindow> parent, CComPtr<IShellItem> item);
    virtual ~ItemWindow();

    virtual bool preserveSize(); // if true, requested size will be ignored by parent
    virtual SIZE requestedSize();

    bool create(RECT rect, int showCommand);
    void close();
    void activate();
    void setPos(POINT pos);
    void move(int x, int y);

    virtual bool handleTopLevelMessage(MSG *msg);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID id, void **obj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IDropSource
    STDMETHODIMP QueryContinueDrag(BOOL escapePressed, DWORD keyState) override;
    STDMETHODIMP GiveFeedback(DWORD effect) override;
    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject *dataObject, DWORD keyState, POINTL point, DWORD *effect)
        override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL point, DWORD *effect) override;
    STDMETHODIMP Drop(IDataObject *dataObject, DWORD keyState, POINTL point, DWORD *effect)
        override;

    CComPtr<IShellItem> item;

protected:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    RECT windowBody();

    virtual void onCreate();
    virtual void onDestroy();
    virtual void onActivate(WORD state, HWND prevWindow);
    virtual void onSize(int width, int height);
    virtual void onPaint(PAINTSTRUCT paint);

    void openChild(CComPtr<IShellItem> childItem);
    void closeChild();
    virtual void onChildDetached();

    virtual void refresh();

    HWND hwnd;
    CComHeapPtr<wchar_t> title;

    CComPtr<ItemWindow> parent, child;

private:
    virtual const wchar_t * className() = 0;

    HWND createChainOwner();

    void windowRectChanged();
    void bringGroupToFront();
    // for DWM custom frame:
    void extendWindowFrame();
    LRESULT hitTestNCA(POINT cursor);

    void openParent();
    void clearParent();
    void detachFromParent(); // updates UI state
    POINT childPos(); // top left corner of child
    POINT parentPos(); // top right corner of parent

    void invokeProxyDefaultVerb(POINT point);
    void openProxyProperties();
    void openProxyContextMenu(POINT point);
    void beginProxyDrag(POINT offset); // specify offset from icon origin
    void beginRename();
    void completeRename();
    void cancelRename();
    bool dropAllowed(POINTL point);

    // window subclasses
    static LRESULT CALLBACK captionButtonProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);
    static LRESULT CALLBACK renameBoxProc(HWND hwnd, UINT message,
        WPARAM wParam, LPARAM lParam, UINT_PTR subclassID, DWORD_PTR refData);

    HICON iconLarge = nullptr, iconSmall = nullptr;

    HWND tooltip, parentButton, renameBox;
    RECT proxyRect, titleRect, iconRect;
    CComPtr<IDropTarget> itemDropTarget;
    // for handling delayed context menu messages while open (eg. for Open With menu)
    CComQIPtr<IContextMenu2> contextMenu2;
    CComQIPtr<IContextMenu3> contextMenu3;

    SIZE storedChildSize;
    POINT moveAccum;
};

} // namespace
