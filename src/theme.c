#include "theme.h"

#include <stdlib.h>

#include <dwmapi.h>
#include <uxtheme.h>

// DWM attribute for immersive dark mode title bar.
// Value 20 on Windows 10 2004 (build 19041) and later (incl. Windows 11).
// Value 19 on earlier Windows 10 builds (1809..1909). We try both for compatibility.
#define DWMWA_USE_IMMERSIVE_DARK_MODE_NEW 20
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19

// uxtheme.dll undocumented ordinal exports for dark mode support:
//   104: RefreshImmersiveColorPolicyState()
//   132: AllowDarkModeForApp(BOOL)          — Windows 10 1809..1903
//   133: AllowDarkModeForWindow(HWND, BOOL)
//   135: SetPreferredAppMode(int)           — Windows 10 1909+

typedef enum
{
	THEME_APPMODE_DEFAULT     = 0,
	THEME_APPMODE_ALLOW_DARK  = 1,
	THEME_APPMODE_FORCE_DARK  = 2,
	THEME_APPMODE_FORCE_LIGHT = 3,
}
ThemeAppMode;

typedef void  (WINAPI* ThemeRefreshImmersiveColorPolicyStateFn)(void);
typedef BOOL  (WINAPI* ThemeAllowDarkModeForAppFn)(BOOL);
typedef BOOL  (WINAPI* ThemeAllowDarkModeForWindowFn)(HWND, BOOL);
typedef long  (WINAPI* ThemeSetPreferredAppModeFn)(ThemeAppMode);

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
				LONG_PTR Type = Style & BS_TYPEMASK;

				// Push buttons: classic rendering still draws the panel with
				// COLOR_BTNFACE, ignoring WM_CTLCOLORBTN. Switch to owner-draw
				// in dark mode so we fully control the appearance via
				// WM_DRAWITEM. Save the original style so light mode can
				// restore it.
				if (Type == BS_PUSHBUTTON || Type == BS_DEFPUSHBUTTON)
				{
					if (DisableTheme)
					{
						if (!GetPropW(Child, L"ThemeOrigStyle"))
						{
							SetPropW(Child, L"ThemeOrigStyle", (HANDLE)Style);
						}
						SetWindowLongPtrW(Child, GWL_STYLE,
							(Style & ~BS_TYPEMASK) | BS_OWNERDRAW);
					}
					else
					{
						HANDLE Prop = GetPropW(Child, L"ThemeOrigStyle");
						if (Prop)
						{
							SetWindowLongPtrW(Child, GWL_STYLE, (LONG_PTR)Prop);
							RemovePropW(Child, L"ThemeOrigStyle");
						}
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

	// Draw 1px border
	HPEN Pen = CreatePen(PS_SOLID, 1, Border);
	HPEN OldPen = (HPEN)SelectObject(Dc, Pen);
	HBRUSH OldBrush = (HBRUSH)SelectObject(Dc, GetStockObject(NULL_BRUSH));
	Rectangle(Dc, R.left, R.top, R.right, R.bottom);
	SelectObject(Dc, OldPen);
	SelectObject(Dc, OldBrush);
	DeleteObject(Pen);

	// Inset for content (text + focus rect)
	RECT Inner = R;
	InflateRect(&Inner, -2, -2);

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

	// Focus rectangle (dashed)
	if (Focused && !Disabled)
	{
		InflateRect(&Inner, -1, -1);
		DrawFocusRect(Dc, &Inner);
	}

	return TRUE;
}
