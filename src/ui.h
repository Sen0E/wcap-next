#pragma once

#include "wcap.h"

//
// UI module — tray icon, notifications, region selection helpers
//

#define WCAP_UI_FONT      L"Microsoft YaHei UI"
#define WCAP_UI_FONT_SIZE 16

#define WCAP_RECT_BORDER 2

#define WCAP_RESIZE_NONE 0
#define WCAP_RESIZE_TL   1
#define WCAP_RESIZE_T    2
#define WCAP_RESIZE_TR   3
#define WCAP_RESIZE_L    4
#define WCAP_RESIZE_M    5
#define WCAP_RESIZE_R    6
#define WCAP_RESIZE_BL   7
#define WCAP_RESIZE_B    8
#define WCAP_RESIZE_BR   9

// Tray icon & notification helpers
void UI_ShowNotification(HWND Window, LPCWSTR Message, LPCWSTR Title, DWORD Flags);
void UI_UpdateTrayTitle(HWND Window, LPCWSTR Title);
void UI_UpdateTrayIcon(HWND Window, HICON Icon);
void UI_AddTrayIcon(HWND Window, HICON Icon, UINT CallbackMessage);
void UI_RemoveTrayIcon(HWND Window);
void UI_ShowFileInFolder(LPCWSTR Filename);

// Region selection helpers
int  UI_GetPointResize(int X, int Y, int X0, int Y0, int X1, int Y1);
void UI_AdjustRectSizeMultipleOf2(POINT* Selection, int Adjust, int Ref);

// Region selection paint (returns TRUE if handled, FALSE otherwise)
BOOL UI_PaintRegion(HWND Window, HDC Context,
	HDC RectContext, HDC RectDarkContext,
	int RectWidth, int RectHeight,
	BOOL RectSelected, POINT* RectSelection,
	int* RectSetSize, HFONT Font, HFONT FontBold,
	HCURSOR CursorClick, HCURSOR* CursorResize);
