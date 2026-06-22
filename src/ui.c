#include "ui.h"
#include "theme.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <windowsx.h>

void UI_ShowNotification(HWND Window, LPCWSTR Message, LPCWSTR Title, DWORD Flags)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
		.uFlags = NIF_INFO | NIF_TIP,
		.dwInfoFlags = Flags, // NIIF_INFO, NIIF_WARNING, NIIF_ERROR
	};
	StrCpyNW(Data.szTip, WCAP_TITLE, _countof(Data.szTip));
	StrCpyNW(Data.szInfo, Message, _countof(Data.szInfo));
	StrCpyNW(Data.szInfoTitle, Title ? Title : WCAP_TITLE, _countof(Data.szInfoTitle));
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

void UI_UpdateTrayTitle(HWND Window, LPCWSTR Title)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
		.uFlags = NIF_TIP,
	};
	StrCpyNW(Data.szTip, Title, _countof(Data.szTip));
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

void UI_UpdateTrayIcon(HWND Window, HICON Icon)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
		.uFlags = NIF_ICON,
		.hIcon = Icon,
	};
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

void UI_AddTrayIcon(HWND Window, HICON Icon, UINT CallbackMessage)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
		.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
		.uCallbackMessage = CallbackMessage,
		.hIcon = Icon,
	};
	StrCpyNW(Data.szTip, WCAP_TITLE, _countof(Data.szTip));
	Shell_NotifyIconW(NIM_ADD, &Data);
}

void UI_RemoveTrayIcon(HWND Window)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
	};
	Shell_NotifyIconW(NIM_DELETE, &Data);
}

void UI_ShowFileInFolder(LPCWSTR Filename)
{
	SFGAOF Flags;
	PIDLIST_ABSOLUTE List;
	if (Filename[0] && SUCCEEDED(SHParseDisplayName(Filename, NULL, &List, 0, &Flags)))
	{
		HR(SHOpenFolderAndSelectItems(List, 0, NULL, 0));
		CoTaskMemFree(List);
	}
}

int UI_GetPointResize(int X, int Y, int X0, int Y0, int X1, int Y1)
{
	int BorderX = GetSystemMetrics(SM_CXSIZEFRAME);
	int BorderY = GetSystemMetrics(SM_CYSIZEFRAME);

	POINT P = { X, Y };

	RECT TL = { X0 - BorderX, Y0 - BorderY, X0 + BorderX, Y0 + BorderY };
	if (PtInRect(&TL, P)) return WCAP_RESIZE_TL;

	RECT TR = { X1 - BorderX, Y0 - BorderY, X1 + BorderX, Y0 + BorderY };
	if (PtInRect(&TR, P)) return WCAP_RESIZE_TR;

	RECT BL = { X0 - BorderX, Y1 - BorderY, X0 + BorderX, Y1 + BorderY };
	if (PtInRect(&BL, P)) return WCAP_RESIZE_BL;

	RECT BR = { X1 - BorderX, Y1 - BorderY, X1 + BorderX, Y1 + BorderY };
	if (PtInRect(&BR, P)) return WCAP_RESIZE_BR;

	RECT T = { X0, Y0 - BorderY, X1, Y0 + BorderY };
	if (PtInRect(&T, P)) return WCAP_RESIZE_T;

	RECT B = { X0, Y1 - BorderY, X1, Y1 + BorderY };
	if (PtInRect(&B, P)) return WCAP_RESIZE_B;

	RECT L = { X0 - BorderX, Y0, X0 + BorderX, Y1 };
	if (PtInRect(&L, P)) return WCAP_RESIZE_L;

	RECT R = { X1 - BorderX, Y0, X1 + BorderX, Y1 };
	if (PtInRect(&R, P)) return WCAP_RESIZE_R;

	RECT M = { X0, Y0, X1, Y1 };
	if (PtInRect(&M, P)) return WCAP_RESIZE_M;

	return WCAP_RESIZE_NONE;
}

void UI_AdjustRectSizeMultipleOf2(POINT* Selection, int Adjust, int Ref)
{
	int W = Selection[Ref].x - Selection[Adjust].x;
	W = (W + (W > 0)) & ~1;
	Selection[Adjust].x = Selection[Ref].x - W;

	int H = Selection[Ref].y - Selection[Adjust].y;
	H = (H + (H > 0)) & ~1;
	Selection[Adjust].y = Selection[Ref].y - H;
}

BOOL UI_PaintRegion(HWND Window, HDC Context,
	HDC RectContext, HDC RectDarkContext,
	int RectWidth, int RectHeight,
	BOOL RectSelected, POINT* RectSelection,
	int* RectSetSize, HFONT Font, HFONT FontBold,
	HCURSOR CursorClick, HCURSOR* CursorResize)
{
	(void)CursorResize;

	if (RectContext)
	{
		// draw darkened screenshot (caller handles the BitBlt)
		// The actual BitBlt of the darkened background is done by the caller

		if (RectSelected)
		{
			// draw selected rectangle
			int X0 = min(RectSelection[0].x, RectSelection[1].x);
			int Y0 = min(RectSelection[0].y, RectSelection[1].y);
			int X1 = max(RectSelection[0].x, RectSelection[1].x);
			int Y1 = max(RectSelection[0].y, RectSelection[1].y);
			BitBlt(Context, X0, Y0, X1 - X0, Y1 - Y0, RectContext, X0, Y0, SRCCOPY);

			RECT Rect = { X0 - 1, Y0 - 1, X1 + 1, Y1 + 1 };
			FrameRect(Context, &Rect, GetStockObject(WHITE_BRUSH));

			WCHAR Text[128];
			int TextLength = StrFormat(Text, L"%d x %d", X1 - X0, Y1 - Y0);

			SelectObject(Context, FontBold);
			SetTextAlign(Context, TA_TOP | TA_RIGHT);
			SetTextColor(Context, RGB(255, 255, 255));
			SetBkMode(Context, TRANSPARENT);
			ExtTextOutW(Context, X1, Y1, 0, NULL, Text, TextLength, NULL);

			SelectObject(Context, FontBold);
			SetTextAlign(Context, TA_BOTTOM | TA_LEFT);
			SetTextColor(Context, RGB(255, 255, 255));

			const WCHAR TextResize[] = L"调整大小:  ";

			SIZE Size;
			GetTextExtentPoint32W(Context, TextResize, _countof(TextResize) - 1, &Size);
			ExtTextOutW(Context, X0, Y0, 0, NULL, TextResize, _countof(TextResize) - 1, NULL);

			int X = X0;
			SelectObject(Context, Font);

			POINT CursorPos;
			GetCursorPos(&CursorPos);
			ScreenToClient(Window, &CursorPos);

			RectSetSize[0] = RectSetSize[1] = 0;

			int Sizes[][2] = { { 800, 600 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 } };
			for (int i=0; i<_countof(Sizes); i++)
			{
				X += Size.cx;

				TextLength = StrFormat(Text, L"%dx%d  ", Sizes[i][0], Sizes[i][1]);
				GetTextExtentPoint32W(Context, Text, TextLength, &Size);

				RECT Rect = { X, Y0 - Size.cy, X + Size.cx, Y0 };
				BOOL Hovering = PtInRect(&Rect, CursorPos);
				SetTextColor(Context, Hovering ? RGB(255, 255, 255) : RGB(192, 192, 192));
				ExtTextOutW(Context, X, Y0, 0, NULL, Text, TextLength, NULL);

				if (Hovering)
				{
					RectSetSize[0] = Sizes[i][0];
					RectSetSize[1] = Sizes[i][1];
					SetCursor(CursorClick);
				}
			}
		}
		else
		{
			// draw initial message when no rectangle is selected
			SelectObject(Context, Font);
			SelectObject(Context, GetStockObject(DC_PEN));
			SelectObject(Context, GetStockObject(DC_BRUSH));

			const WCHAR Line1[] = L"用鼠标选择区域，按回车键开始录制。";
			const WCHAR Line2[] = L"按 ESC 键取消。";

			const WCHAR* Lines[] = { Line1, Line2 };
			const int LineLengths[] = { _countof(Line1) - 1, _countof(Line2) - 1 };
			int Widths[_countof(Lines)];
			int Height;

			int TotalWidth = 0;
			int TotalHeight = 0;
			for (int i = 0; i < _countof(Lines); i++)
			{
				SIZE Size;
				GetTextExtentPoint32W(Context, Lines[i], LineLengths[i], &Size);
				Widths[i] = Size.cx;
				Height = Size.cy;
				TotalWidth = max(TotalWidth, Size.cx);
				TotalHeight += Size.cy;
			}
			TotalWidth += 2 * Height;
			TotalHeight += Height;

			int MsgX = (RectWidth - TotalWidth) / 2;
			int MsgY = (RectHeight - TotalHeight) / 2;

			COLORREF BoxBg     = Theme_IsDark() ? RGB(45, 45, 45)   : RGB(0, 0, 128);
			COLORREF BoxBorder = Theme_IsDark() ? RGB(200, 200, 200) : RGB(255, 255, 255);
			COLORREF BoxText   = Theme_IsDark() ? RGB(255, 255, 255) : RGB(255, 255, 0);

			SetDCPenColor(Context, BoxBorder);
			SetDCBrushColor(Context, BoxBg);
			Rectangle(Context, MsgX, MsgY, MsgX + TotalWidth, MsgY + TotalHeight);

			SetTextAlign(Context, TA_TOP | TA_CENTER);
			SetTextColor(Context, BoxText);
			SetBkMode(Context, TRANSPARENT);
			int Y = MsgY + Height / 2;
			int X = RectWidth / 2;
			for (int i = 0; i < _countof(Lines); i++)
			{
				ExtTextOutW(Context, X, Y, 0, NULL, Lines[i], LineLengths[i], NULL);
				Y += Height;
			}
		}
		return TRUE;
	}
	else
	{
		// Recording border paint (not region selection)
		// Draw the colored border + color key interior
		// Caller handles this
		return FALSE;
	}
}
