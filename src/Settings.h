#pragma once
#include <common.h>

#include <windows.h>
#include <atlbase.h>

namespace chromabrowse::settings {

void getStartingFolder(CComHeapPtr<wchar_t> &value);
void setStartingFolder(wchar_t *value);

SIZE getItemWindowSize();
void setItemWindowSize(SIZE value);

SIZE getFolderWindowSize();
void setFolderWindowSize(SIZE value);

bool getPreviewsEnabled();
void setPreviewsEnabled(bool value);

bool getTextEditorEnabled();
void setTextEditorEnabled(bool value);

bool getTrayOpenOnStartup();
void setTrayOpenOnStartup(bool value);
void getTrayFolder(CComHeapPtr<wchar_t> &value);
void setTrayFolder(wchar_t *value);
POINT getTrayPosition();
void setTrayPosition(POINT value);
SIZE getTraySize();
void setTraySize(SIZE value);

enum TrayDirection : DWORD { TRAY_UP, TRAY_DOWN, TRAY_RIGHT };
TrayDirection getTrayDirection();
void setTrayDirection(TrayDirection value);

} // namespace
