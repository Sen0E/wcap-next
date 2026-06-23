#include "theme.h"

#include <stdlib.h>

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

// Link comctl32.lib for SetWindowSubclass / RemoveWindowSubclass.
#pragma comment(lib, "comctl32.lib")

// DWM attribute for immersive dark mode title bar.
// Value 20 on Windows 10 2004 (build 19041) and later (incl. Windows 11).
// Value 19 on earlier Windows 10 builds (1809..1909). We try both for compatibility.
#define DWMWA_USE_IMMERSIVE_DARK_MODE_NEW 20
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19

// DWM attribute for Windows 11 Mica / backdrop material.
//   DWMWA_SYSTEMBACKDROP_TYPE (1029): Win11 22000+
//     0 = None, 1 = Mica, 2 = Acrylic, 3 = MicaAlt
//   DWMWA_MICA_EFFECT (1029 on some builds, fallback): undocumented
// Requires DWMWA_USE_IMMERSIVE_DARK_MODE to be set first, and the window
// must have an extended frame or be transparent for the effect to show.
#define DWMWA_SYSTEMBACKDROP_TYPE 1029

// uxtheme.dll undocumented ordinal exports for dark mode support:
//   104: RefreshImmersiveColorPolicyState()
//   132: AllowDarkModeForApp(BOOL)          — Windows 10 1809..1903
//   133: AllowDarkModeForWindow(HWND, BOOL)
//   135: SetPreferredAppMode(int)           — Windows 10 1909+
//   136: FlushMenuThemes()                 — refresh menu rendering state

typedef enum
{
	THEME_APPMODE_DEFAULT     = 0,
	THEME_APPMODE_ALLOW_DARK  = 1,
	THEME_APPMODE_FORCE_DARK  = 2,
	THEME_APPMODE_FORCE_LIGHT = 3,
}
ThemeAppMode;

typedef enum
{
	THEME_BACKDROP_NONE    = 0,
	THEME_BACKDROP_MICA    = 1,
	THEME_BACKDROP_ACRYLIC = 2,
	THEME_BACKDROP_MICAALT = 3,
}
ThemeBackdrop;

typedef void  (WINAPI* ThemeRefreshImmersiveColorPolicyStateFn)(void);
typedef BOOL  (WINAPI* ThemeAllowDarkModeForAppFn)(BOOL);
typedef BOOL  (WINAPI* ThemeAllowDarkModeForWindowFn)(HWND, BOOL);
typedef long  (WINAPI* ThemeSetPreferredAppModeFn)(ThemeAppMode);
typedef void  (WINAPI* ThemeFlushMenuThemesFn)(void);

// Dark mode palette (matches Windows 11 dark theme tones)
#define THEME_DARK_BG            RGB(32, 32, 32)    // #202020 — dialog/window background
#define THEME_DARK_EDIT_BG       RGB(45, 45, 45)    // #2D2D2D — edit/combo background
#define THEME_DARK_TEXT          RGB(255, 255, 255)
#define THEME_DARK_DISABLED_TEXT RGB(160, 160, 160) // gray for disabled controls
#define THEME_DARK_GROUPBOX      RGB(90, 90, 90)    // subtle group box border
#define THEME_DARK_SCROLLBAR     RGB(60, 60, 60)    // scrollbar thumb

static struct
{
	BOOL Dark;
	BOOL HighContrast;
	HMODULE Uxtheme;
	ThemeRefreshImmersiveColorPolicyStateFn RefreshImmersiveColorPolicyState;
	ThemeAllowDarkModeForAppFn             AllowDarkModeForApp;
	ThemeAllowDarkModeForWindowFn          AllowDarkModeForWindow;
	ThemeSetPreferredAppModeFn             SetPreferredAppMode;
	ThemeFlushMenuThemesFn                 FlushMenuThemes;
	HBRUSH BgBrush;
	HBRUSH EditBgBrush;
}
gTheme;

static BOOL Theme__ReadSystemDark(void)
{
	DWORD Light = 1; // default: light theme
	DWORD Size = sizeof(Light);
	HKEY Key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			0, KEY_READ, &Key) == ERROR_SUCCESS)
	{
		RegQueryValueExW(Key, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&Light, &Size);
		RegCloseKey(Key);
	}
	return Light == 0;
}

static BOOL Theme__ReadHighContrast(void)
{
	HIGHCONTRASTW Info = { .cbSize = sizeof(Info) };
	SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(Info), &Info, 0);
	return (Info.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

static void Theme__LoadUxtheme(void)
{
	if (gTheme.Uxtheme) return;
	gTheme.Uxtheme = LoadLibraryW(L"uxtheme.dll");
	if (!gTheme.Uxtheme) return;

	gTheme.RefreshImmersiveColorPolicyState =
		(ThemeRefreshImmersiveColorPolicyStateFn)GetProcAddress(gTheme.Uxtheme, MAKEINTRESOURCEA(104));
	gTheme.AllowDarkModeForApp =
		(ThemeAllowDarkModeForAppFn)GetProcAddress(gTheme.Uxtheme, MAKEINTRESOURCEA(132));
	gTheme.AllowDarkModeForWindow =
		(ThemeAllowDarkModeForWindowFn)GetProcAddress(gTheme.Uxtheme, MAKEINTRESOURCEA(133));
	gTheme.SetPreferredAppMode =
		(ThemeSetPreferredAppModeFn)GetProcAddress(gTheme.Uxtheme, MAKEINTRESOURCEA(135));
	gTheme.FlushMenuThemes =
		(ThemeFlushMenuThemesFn)GetProcAddress(gTheme.Uxtheme, MAKEINTRESOURCEA(136));
}

static void Theme__ApplyAppMode(void)
{
	// In high-contrast mode, let the system fully control colors — do not
	// force dark/light app mode, otherwise it conflicts with HC themes.
	if (gTheme.HighContrast)
	{
		if (gTheme.SetPreferredAppMode)
		{
			gTheme.SetPreferredAppMode(THEME_APPMODE_DEFAULT);
		}
		if (gTheme.RefreshImmersiveColorPolicyState)
		{
			gTheme.RefreshImmersiveColorPolicyState();
		}
		if (gTheme.FlushMenuThemes)
		{
			gTheme.FlushMenuThemes();
		}
		return;
	}

	if (gTheme.SetPreferredAppMode)
	{
		gTheme.SetPreferredAppMode(gTheme.Dark ? THEME_APPMODE_FORCE_DARK : THEME_APPMODE_FORCE_LIGHT);
	}
	else if (gTheme.AllowDarkModeForApp)
	{
		gTheme.AllowDarkModeForApp(gTheme.Dark);
	}

	if (gTheme.RefreshImmersiveColorPolicyState)
	{
		gTheme.RefreshImmersiveColorPolicyState();
	}

	// FlushMenuThemes forces menus (popup menus, context menus) to re-render
	// with the new app mode. Without this, tray/context menus may stay light
	// after switching to dark mode until the process restarts.
	if (gTheme.FlushMenuThemes)
	{
		gTheme.FlushMenuThemes();
	}
}

static void Theme__UpdateBrushes(void)
{
	if (gTheme.BgBrush)
	{
		DeleteObject(gTheme.BgBrush);
		gTheme.BgBrush = NULL;
	}
	if (gTheme.EditBgBrush)
	{
		DeleteObject(gTheme.EditBgBrush);
		gTheme.EditBgBrush = NULL;
	}

	if (gTheme.Dark)
	{
		gTheme.BgBrush = CreateSolidBrush(THEME_DARK_BG);
		gTheme.EditBgBrush = CreateSolidBrush(THEME_DARK_EDIT_BG);
	}
}

void Theme_Init(void)
{
	Theme__LoadUxtheme();
	gTheme.HighContrast = Theme__ReadHighContrast();
	gTheme.Dark = Theme__ReadSystemDark();
	Theme__ApplyAppMode();
	Theme__UpdateBrushes();
}

void Theme_Refresh(void)
{
	BOOL NewHighContrast = Theme__ReadHighContrast();
	BOOL NewDark = Theme__ReadSystemDark();

	if (NewHighContrast != gTheme.HighContrast || NewDark != gTheme.Dark)
	{
		gTheme.HighContrast = NewHighContrast;
		gTheme.Dark = NewDark;
		Theme__ApplyAppMode();
		Theme__UpdateBrushes();
	}
	else if (gTheme.RefreshImmersiveColorPolicyState)
	{
		// still refresh policy in case of high-contrast or other theme changes
		gTheme.RefreshImmersiveColorPolicyState();
	}
}

BOOL Theme_IsDark(void)
{
	return gTheme.Dark && !gTheme.HighContrast;
}

BOOL Theme_IsHighContrast(void)
{
	return gTheme.HighContrast;
}

void Theme_ApplyTitleBar(HWND Window)
{
	// In high-contrast mode, let the system render the title bar.
	if (gTheme.HighContrast)
	{
		return;
	}

	BOOL Value = gTheme.Dark;

	// attribute 20 (Win10 2004+ / Win11), fall back to 19 (Win10 1809..1909)
	if (FAILED(DwmSetWindowAttribute(Window, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &Value, sizeof(Value))))
	{
		DwmSetWindowAttribute(Window, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &Value, sizeof(Value));
	}

	if (gTheme.Dark && gTheme.AllowDarkModeForWindow)
	{
		gTheme.AllowDarkModeForWindow(Window, TRUE);
	}
}

void Theme_ApplyMica(HWND Window)
{
	// Mica is a Windows 11 (build 22000+) backdrop material. It has no effect
	// on Windows 10, in light mode, or under high-contrast. We attempt the
	// DWMWA_SYSTEMBACKDROP_TYPE attribute (1029); on older builds DwmSetWindowAttribute
	// returns an error code which we silently ignore.
	if (!Theme_IsDark())
	{
		// Restore default backdrop in light mode.
		INT_PTR Value = THEME_BACKDROP_NONE;
		DwmSetWindowAttribute(Window, DWMWA_SYSTEMBACKDROP_TYPE, &Value, sizeof(Value));
		return;
	}

	INT_PTR Value = THEME_BACKDROP_MICA;
	DwmSetWindowAttribute(Window, DWMWA_SYSTEMBACKDROP_TYPE, &Value, sizeof(Value));
}

// Subclass ID for ComboBox dark-mode painting.
#define THEME_COMBO_SUBCLASS_ID 0x54434D42 // 'TCMB'

// Overpaint a classic ComboBox in dark mode to fix:
//   - Square outer border → rounded border
//   - Light drop-down button background → dark
//   - Separator line between edit field and button → removed
//   - White inner border around the edit field → removed
//
// The system draws these with COLOR_BTNFACE / COLOR_WINDOW which stay light
// in dark mode and cannot be changed via WM_CTLCOLOR* messages.
static void Theme__PaintComboDropDown(HWND Hwnd)
{
	// Use COMBOBOXINFO to get exact positions of the edit field and the
	// drop-down button. This lets us erase the separator and inner border
	// precisely without overpainting the edit text.
	COMBOBOXINFO Cbi;
	Cbi.cbSize = sizeof(Cbi);
	BOOL HasInfo = GetComboBoxInfo(Hwnd, &Cbi);

	RECT WinRect;
	GetWindowRect(Hwnd, &WinRect);
	int W = WinRect.right - WinRect.left;
	int H = WinRect.bottom - WinRect.top;

	int Border = 1;
	int BtnW = GetSystemMetrics(SM_CXVSCROLL);

	// Convert button rect from screen to window-relative coords.
	RECT BtnRect;
	if (HasInfo)
	{
		BtnRect.left   = Cbi.rcButton.left   - WinRect.left;
		BtnRect.top    = Cbi.rcButton.top    - WinRect.top;
		BtnRect.right  = Cbi.rcButton.right  - WinRect.left;
		BtnRect.bottom = Cbi.rcButton.bottom - WinRect.top;
	}
	else
	{
		BtnRect.left   = W - BtnW - Border;
		BtnRect.top    = Border;
		BtnRect.right  = W - Border;
		BtnRect.bottom = H - Border;
	}

	// ---- Non-client area: erase square border, draw rounded border ----
	HDC Dc = GetWindowDC(Hwnd);
	if (!Dc) return;

	int SavedDc = SaveDC(Dc);

	// Erase the system square border (frame area only)
	ExcludeClipRect(Dc, Border, Border, W - Border, H - Border);
	HBRUSH BgBrush = CreateSolidBrush(THEME_DARK_BG);
	RECT FullRect = { 0, 0, W, H };
	FillRect(Dc, &FullRect, BgBrush);
	DeleteObject(BgBrush);

	RestoreDC(Dc, SavedDc);
	SavedDc = SaveDC(Dc);

	// Draw rounded border
	HPEN BorderPen = CreatePen(PS_SOLID, 1, THEME_DARK_GROUPBOX);
	HPEN OldPen = (HPEN)SelectObject(Dc, BorderPen);
	HBRUSH OldBrush = (HBRUSH)SelectObject(Dc, GetStockObject(NULL_BRUSH));
	RoundRect(Dc, 0, 0, W, H, 8, 8);
	SelectObject(Dc, OldPen);
	SelectObject(Dc, OldBrush);
	DeleteObject(BorderPen);

	RestoreDC(Dc, SavedDc);
	ReleaseDC(Hwnd, Dc);

	// ---- Client area: erase separator + inner border, fill button ----
	HDC ClientDc = GetDC(Hwnd);
	if (!ClientDc) return;

	int SavedClient = SaveDC(ClientDc);

	// Fill the drop-down button area with dark edit color
	HBRUSH BtnBrush = CreateSolidBrush(THEME_DARK_EDIT_BG);
	FillRect(ClientDc, &BtnRect, BtnBrush);

	// Erase the separator line between the edit field and the button.
	// The system draws a 1px vertical line at the left edge of the button.
	RECT SepRect = { BtnRect.left, BtnRect.top, BtnRect.left + 1, BtnRect.bottom };
	FillRect(ClientDc, &SepRect, BtnBrush);
	DeleteObject(BtnBrush);

	// Erase the white inner border around the edit field. The edit field
	// (rcItem) is inset from the client area by ~1px; fill that gap with
	// the edit background color so there's no light border.
	if (HasInfo)
	{
		RECT EditRect;
		EditRect.left   = Cbi.rcItem.left   - WinRect.left;
		EditRect.top    = Cbi.rcItem.top    - WinRect.top;
		EditRect.right  = Cbi.rcItem.right  - WinRect.left;
		EditRect.bottom = Cbi.rcItem.bottom - WinRect.top;

		HBRUSH EditBrush = CreateSolidBrush(THEME_DARK_EDIT_BG);

		// Top edge gap
		RECT Gap = { Border, Border, W - Border, EditRect.top };
		FillRect(ClientDc, &Gap, EditBrush);
		// Bottom edge gap
		Gap.left = Border; Gap.top = EditRect.bottom;
		Gap.right = BtnRect.left; Gap.bottom = H - Border;
		FillRect(ClientDc, &Gap, EditBrush);
		// Left edge gap
		Gap.left = Border; Gap.top = EditRect.top;
		Gap.right = EditRect.left; Gap.bottom = EditRect.bottom;
		FillRect(ClientDc, &Gap, EditBrush);
		// Right edge gap (between edit field and separator/button)
		Gap.left = EditRect.right; Gap.top = EditRect.top;
		Gap.right = BtnRect.left; Gap.bottom = EditRect.bottom;
		FillRect(ClientDc, &Gap, EditBrush);

		DeleteObject(EditBrush);
	}

	RestoreDC(ClientDc, SavedClient);

	// Draw drop-down arrow (filled triangle pointing down)
	BOOL Enabled = IsWindowEnabled(Hwnd);
	COLORREF ArrowColor = Enabled ? THEME_DARK_TEXT : THEME_DARK_DISABLED_TEXT;

	int Cx = BtnRect.left + (BtnRect.right - BtnRect.left) / 2;
	int Cy = BtnRect.top + (BtnRect.bottom - BtnRect.top) / 2;

	POINT Arrow[3] =
	{
		{ Cx - 4, Cy - 2 },
		{ Cx + 4, Cy - 2 },
		{ Cx,     Cy + 3 },
	};

	HPEN Pen = CreatePen(PS_SOLID, 1, ArrowColor);
	HPEN OldPen2 = (HPEN)SelectObject(ClientDc, Pen);
	HBRUSH ArrowBrush = CreateSolidBrush(ArrowColor);
	HBRUSH OldBrush2 = (HBRUSH)SelectObject(ClientDc, ArrowBrush);
	Polygon(ClientDc, Arrow, 3);
	SelectObject(ClientDc, OldPen2);
	SelectObject(ClientDc, OldBrush2);
	DeleteObject(Pen);
	DeleteObject(ArrowBrush);

	ReleaseDC(Hwnd, ClientDc);
}

static LRESULT CALLBACK Theme__ComboBoxSubclass(HWND Hwnd, UINT Msg,
	WPARAM WParam, LPARAM LParam, UINT_PTR Id, DWORD_PTR RefData)
{
	(VOID)RefData;

	BOOL DoPaint = Theme_IsDark() && !gTheme.HighContrast;

	if (Msg == WM_PAINT)
	{
		// Let the system paint first, then overpaint the drop-down button
		// and rounded border.
		LRESULT Ret = DefSubclassProc(Hwnd, Msg, WParam, LParam);
		if (DoPaint)
		{
			Theme__PaintComboDropDown(Hwnd);
		}
		return Ret;
	}

	if (Msg == WM_NCPAINT)
	{
		// Let the system paint the non-client area, then overpaint the border
		// with our rounded version. This keeps the border consistent when the
		// window is resized or activated/deactivated.
		LRESULT Ret = DefSubclassProc(Hwnd, Msg, WParam, LParam);
		if (DoPaint)
		{
			Theme__PaintComboDropDown(Hwnd);
		}
		return Ret;
	}

	if (Msg == WM_NCDESTROY)
	{
		RemoveWindowSubclass(Hwnd, Theme__ComboBoxSubclass, Id);
	}

	return DefSubclassProc(Hwnd, Msg, WParam, LParam);
}

void Theme_ApplyToDialogControls(HWND Dialog)
{
	// In high-contrast mode, keep visual styles enabled so controls match
	// the user's HC theme. Only disable themes when actively in dark mode.
	BOOL DisableTheme = gTheme.Dark && !gTheme.HighContrast;

	HWND Child = GetWindow(Dialog, GW_CHILD);
	while (Child)
	{
		WCHAR ClassName[16];
		if (GetClassNameW(Child, ClassName, _countof(ClassName)) > 0)
		{
			if (lstrcmpW(ClassName, L"Button") == 0)
			{
				LONG_PTR Style = GetWindowLongPtrW(Child, GWL_STYLE);
				HANDLE Prop = GetPropW(Child, L"ThemeOrigStyle");

				// Push buttons and checkboxes/radios: classic rendering does
				// not give us enough control over colors and shapes (push
				// buttons ignore WM_CTLCOLORBTN; checkboxes draw square boxes).
				// Switch to owner-draw in dark mode so we fully control the
				// appearance via WM_DRAWITEM. Save the original style so light
				// mode can restore it.
				//
				// NOTE: check the saved prop FIRST, not the current button
				// type. In dark mode the type was already changed to
				// BS_OWNERDRAW, so checking the original type would never
				// match on the dark→light transition and the control would
				// stay owner-drawn (invisible in light mode).
				if (DisableTheme)
				{
					LONG_PTR Type = Style & BS_TYPEMASK;
					BOOL Convert = (Prop == NULL) &&
						(Type == BS_PUSHBUTTON || Type == BS_DEFPUSHBUTTON ||
						 Type == BS_AUTOCHECKBOX || Type == BS_AUTORADIOBUTTON);
					if (Convert)
					{
						SetPropW(Child, L"ThemeOrigStyle", (HANDLE)Style);
						SetWindowLongPtrW(Child, GWL_STYLE,
							(Style & ~BS_TYPEMASK) | BS_OWNERDRAW);
						SetWindowPos(Child, NULL, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
							SWP_NOACTIVATE | SWP_FRAMECHANGED);
					}
				}
				else
				{
					if (Prop)
					{
						SetWindowLongPtrW(Child, GWL_STYLE, (LONG_PTR)Prop);
						RemovePropW(Child, L"ThemeOrigStyle");
						SetWindowPos(Child, NULL, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
							SWP_NOACTIVATE | SWP_FRAMECHANGED);
					}
				}

				// Under visual styles (ComCtl32 v6) themed controls ignore
				// SetTextColor from WM_CTLCOLOR* messages. Disabling the theme
				// makes them fall back to classic rendering where text/background
				// coloring is honored. Covers: checkboxes, radio buttons, push
				// buttons, group boxes, and comboboxes.
				SetWindowTheme(Child, DisableTheme ? L"" : NULL, DisableTheme ? L"" : NULL);
			}
			else if (lstrcmpW(ClassName, L"ComboBox") == 0)
			{
				SetWindowTheme(Child, DisableTheme ? L"" : NULL, DisableTheme ? L"" : NULL);
				// Subclass to overpaint the drop-down arrow button, whose
				// background (COLOR_BTNFACE) cannot be themed otherwise.
				if (DisableTheme)
				{
					SetWindowSubclass(Child, Theme__ComboBoxSubclass,
						THEME_COMBO_SUBCLASS_ID, 0);
				}
				else
				{
					RemoveWindowSubclass(Child, Theme__ComboBoxSubclass,
						THEME_COMBO_SUBCLASS_ID);
				}
			}
			else if (lstrcmpW(ClassName, L"ScrollBar") == 0)
			{
				// Scrollbars inside comboboxes/listboxes: disable theme so
				// WM_CTLCOLORSCROLLBAR coloring can be applied in dark mode.
				SetWindowTheme(Child, DisableTheme ? L"" : NULL, DisableTheme ? L"" : NULL);
			}
		}
		Child = GetWindow(Child, GW_HWNDNEXT);
	}
}

COLORREF Theme_BgColor(void)
{
	return Theme_IsDark() ? THEME_DARK_BG : GetSysColor(COLOR_BTNFACE);
}

COLORREF Theme_TextColor(void)
{
	return Theme_IsDark() ? THEME_DARK_TEXT : GetSysColor(COLOR_BTNTEXT);
}

COLORREF Theme_DisabledTextColor(void)
{
	return Theme_IsDark() ? THEME_DARK_DISABLED_TEXT : GetSysColor(COLOR_GRAYTEXT);
}

COLORREF Theme_GroupBoxColor(void)
{
	return Theme_IsDark() ? THEME_DARK_GROUPBOX : GetSysColor(COLOR_3DSHADOW);
}

COLORREF Theme_EditBgColor(void)
{
	return Theme_IsDark() ? THEME_DARK_EDIT_BG : GetSysColor(COLOR_WINDOW);
}

INT_PTR Theme_HandleCtlColor(HDC Dc, UINT Msg)
{
	if (!Theme_IsDark())
	{
		return 0;
	}

	switch (Msg)
	{
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
			if (Dc)
			{
				SetTextColor(Dc, THEME_DARK_TEXT);
				SetBkColor(Dc, THEME_DARK_BG);
			}
			return (INT_PTR)gTheme.BgBrush;

		case WM_CTLCOLORBTN:
			if (Dc)
			{
				SetTextColor(Dc, THEME_DARK_TEXT);
				SetBkColor(Dc, THEME_DARK_BG);
			}
			return (INT_PTR)gTheme.BgBrush;

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			if (Dc)
			{
				SetTextColor(Dc, THEME_DARK_TEXT);
				SetBkColor(Dc, THEME_DARK_EDIT_BG);
			}
			return (INT_PTR)gTheme.EditBgBrush;

		case WM_CTLCOLORSCROLLBAR:
			if (Dc)
			{
				SetBkColor(Dc, THEME_DARK_BG);
			}
			return (INT_PTR)gTheme.BgBrush;

		default:
			return 0;
	}
}

INT_PTR Theme_HandleCtlColorForWindow(HWND Control, HDC Dc, UINT Msg)
{
	if (!Theme_IsDark() || !Control)
	{
		return Theme_HandleCtlColor(Dc, Msg);
	}

	BOOL Enabled = IsWindowEnabled(Control);

	switch (Msg)
	{
		case WM_CTLCOLORSTATIC:
			// Disabled static text (e.g. disabled checkbox labels) needs a
			// softer color, otherwise white-on-dark is too harsh.
			if (Dc)
			{
				SetTextColor(Dc, Enabled ? THEME_DARK_TEXT : THEME_DARK_DISABLED_TEXT);
				SetBkColor(Dc, THEME_DARK_BG);
			}
			return (INT_PTR)gTheme.BgBrush;

		case WM_CTLCOLORBTN:
			// Disabled push buttons: keep readable but slightly dimmed text.
			if (Dc)
			{
				SetTextColor(Dc, Enabled ? THEME_DARK_TEXT : THEME_DARK_DISABLED_TEXT);
				SetBkColor(Dc, THEME_DARK_BG);
			}
			return (INT_PTR)gTheme.BgBrush;

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			if (Dc)
			{
				SetTextColor(Dc, Enabled ? THEME_DARK_TEXT : THEME_DARK_DISABLED_TEXT);
				SetBkColor(Dc, THEME_DARK_EDIT_BG);
			}
			return (INT_PTR)gTheme.EditBgBrush;

		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSCROLLBAR:
		default:
			return Theme_HandleCtlColor(Dc, Msg);
	}
}

BOOL Theme_DrawButton(const DRAWITEMSTRUCT* Dis)
{
	if (!Theme_IsDark() || !Dis || Dis->CtlType != ODT_BUTTON)
	{
		return FALSE;
	}

	HDC Dc = Dis->hDC;
	RECT R = Dis->rcItem;

	BOOL Disabled = (Dis->itemState & ODS_DISABLED) != 0;
	BOOL Selected = (Dis->itemState & ODS_SELECTED) != 0;
	BOOL Focused = (Dis->itemState & ODS_FOCUS) != 0;

	// Background: slightly lighter when pressed for tactile feedback
	COLORREF Bg = Selected ? RGB(60, 60, 60) : THEME_DARK_EDIT_BG;
	COLORREF Text = Disabled ? THEME_DARK_DISABLED_TEXT : THEME_DARK_TEXT;
	COLORREF Border = Focused ? RGB(0, 120, 215) : THEME_DARK_GROUPBOX;

	// Fill background
	HBRUSH Brush = CreateSolidBrush(Bg);
	FillRect(Dc, &R, Brush);
	DeleteObject(Brush);

	// Rounded border (matches Windows 11 control radius ~4-6px)
	#define THEME_BTN_RADIUS 5
	HPEN Pen = CreatePen(PS_SOLID, 1, Border);
	HPEN OldPen = (HPEN)SelectObject(Dc, Pen);
	HBRUSH OldBrush = (HBRUSH)SelectObject(Dc, GetStockObject(NULL_BRUSH));
	RoundRect(Dc, R.left, R.top, R.right, R.bottom,
		THEME_BTN_RADIUS * 2, THEME_BTN_RADIUS * 2);
	SelectObject(Dc, OldPen);
	SelectObject(Dc, OldBrush);
	DeleteObject(Pen);

	// Inset for content (text + focus rect)
	RECT Inner = R;
	InflateRect(&Inner, -3, -3);

	// Draw text
	WCHAR TextBuf[64];
	int Len = GetWindowTextW(Dis->hwndItem, TextBuf, _countof(TextBuf));
	if (Len > 0)
	{
		SetTextColor(Dc, Text);
		SetBkMode(Dc, TRANSPARENT);
		HFONT OldFont = (HFONT)SendMessageW(Dis->hwndItem, WM_GETFONT, 0, 0);
		if (OldFont)
		{
			OldFont = (HFONT)SelectObject(Dc, OldFont);
		}
		DrawTextW(Dc, TextBuf, Len, &Inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (OldFont)
		{
			SelectObject(Dc, OldFont);
		}
	}

	// Focus rectangle (dashed) — slightly inset to avoid the rounded border
	if (Focused && !Disabled)
	{
		InflateRect(&Inner, -2, -2);
		DrawFocusRect(Dc, &Inner);
	}

	return TRUE;
}

BOOL Theme_DrawCheckBox(const DRAWITEMSTRUCT* Dis)
{
	if (!Theme_IsDark() || !Dis || Dis->CtlType != ODT_BUTTON)
	{
		return FALSE;
	}

	HDC Dc = Dis->hDC;
	RECT R = Dis->rcItem;

	BOOL Disabled  = (Dis->itemState & ODS_DISABLED)  != 0;
	BOOL Pressed   = (Dis->itemState & ODS_SELECTED)  != 0;
	BOOL Focused   = (Dis->itemState & ODS_FOCUS)     != 0;

	// Detect original button type via saved prop (style was changed to
	// BS_OWNERDRAW). This distinguishes checkbox/radio from push buttons.
	HANDLE Prop = GetPropW(Dis->hwndItem, L"ThemeOrigStyle");
	if (!Prop)
	{
		return FALSE;
	}
	LONG_PTR OrigType = ((LONG_PTR)Prop) & BS_TYPEMASK;
	BOOL IsRadio = (OrigType == BS_AUTORADIOBUTTON);
	if (OrigType != BS_AUTOCHECKBOX && !IsRadio)
	{
		return FALSE;
	}

	// Query the actual check state. ODS_SELECTED only reflects the
	// mouse-pressed state, NOT the checked state — so a checkbox would
	// appear unchecked after releasing the mouse without this query.
	LRESULT CheckResult = SendMessageW(Dis->hwndItem, BM_GETCHECK, 0, 0);
	BOOL Checked = (CheckResult == BST_CHECKED);

	// Layout: square indicator on the left, text on the right.
	// Indicator size scales with font height (~13px on default system font).
	int BoxSize = 13;
	int BoxX = R.left + 2;
	int BoxY = R.top + ((R.bottom - R.top) - BoxSize) / 2;

	RECT Box = { BoxX, BoxY, BoxX + BoxSize, BoxY + BoxSize };

	// Checked state drives the indicator fill; Pressed adds a subtle
	// highlight so the user gets feedback while holding the mouse.
	COLORREF BoxBg     = Checked ? RGB(0, 120, 215) :
	                     Pressed ? RGB(60, 60, 60) : THEME_DARK_EDIT_BG;
	COLORREF BoxBorder = Focused ? RGB(0, 120, 215) : THEME_DARK_GROUPBOX;
	COLORREF CheckMark = THEME_DARK_TEXT;
	COLORREF TextColor = Disabled ? THEME_DARK_DISABLED_TEXT : THEME_DARK_TEXT;

	// Fill background of whole item with dialog bg (transparent look)
	HBRUSH BgBrush = CreateSolidBrush(THEME_DARK_BG);
	FillRect(Dc, &R, BgBrush);
	DeleteObject(BgBrush);

	// Draw indicator background
	HBRUSH BoxBrush = CreateSolidBrush(BoxBg);
	HBRUSH OldBrush  = (HBRUSH)SelectObject(Dc, BoxBrush);
	HPEN   BorderPen = CreatePen(PS_SOLID, 1, BoxBorder);
	HPEN   OldPen    = (HPEN)SelectObject(Dc, BorderPen);

	if (IsRadio)
	{
		// Radio: circle
		SelectObject(Dc, GetStockObject(NULL_BRUSH));
		Ellipse(Dc, Box.left, Box.top, Box.right, Box.bottom);
		if (Checked)
		{
			SelectObject(Dc, BoxBrush);
			int Inset = 3;
			Ellipse(Dc, Box.left + Inset, Box.top + Inset,
				Box.right - Inset, Box.bottom - Inset);
		}
	}
	else
	{
		// Checkbox: rounded square (radius 2px)
		SelectObject(Dc, GetStockObject(NULL_BRUSH));
		RoundRect(Dc, Box.left, Box.top, Box.right, Box.bottom, 4, 4);
		if (Checked)
		{
			// Draw checkmark inside the box
			SelectObject(Dc, BoxBrush);
			// Fill the rounded box with accent color
			RoundRect(Dc, Box.left, Box.top, Box.right, Box.bottom, 4, 4);
			// Draw check mark (white on accent)
			HPEN CheckPen = CreatePen(PS_SOLID, 2, CheckMark);
			HPEN OldCheckPen = (HPEN)SelectObject(Dc, CheckPen);
			SelectObject(Dc, GetStockObject(NULL_BRUSH));
			MoveToEx(Dc, Box.left + 3, Box.top + 7, NULL);
			LineTo(Dc, Box.left + 6, Box.top + 10);
			LineTo(Dc, Box.left + 10, Box.top + 3);
			SelectObject(Dc, OldCheckPen);
			DeleteObject(CheckPen);
		}
	}

	SelectObject(Dc, OldPen);
	SelectObject(Dc, OldBrush);
	DeleteObject(BorderPen);
	DeleteObject(BoxBrush);

	// Draw text to the right of the indicator
	RECT TextRect = R;
	TextRect.left = Box.right + 6;
	TextRect.right = R.right;

	WCHAR TextBuf[128];
	int Len = GetWindowTextW(Dis->hwndItem, TextBuf, _countof(TextBuf));
	if (Len > 0)
	{
		SetTextColor(Dc, TextColor);
		SetBkMode(Dc, TRANSPARENT);
		HFONT OldFont = (HFONT)SendMessageW(Dis->hwndItem, WM_GETFONT, 0, 0);
		if (OldFont)
		{
			OldFont = (HFONT)SelectObject(Dc, OldFont);
		}
		DrawTextW(Dc, TextBuf, Len, &TextRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		if (OldFont)
		{
			SelectObject(Dc, OldFont);
		}
	}

	// Focus rectangle around the whole item (text + box)
	if (Focused && !Disabled)
	{
		RECT FocusRect = R;
		InflateRect(&FocusRect, -1, -1);
		DrawFocusRect(Dc, &FocusRect);
	}

	return TRUE;
}
