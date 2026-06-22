#include "theme.h"

#include <dwmapi.h>

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
#define THEME_DARK_BG        RGB(32, 32, 32)    // #202020 — dialog/window background
#define THEME_DARK_EDIT_BG   RGB(45, 45, 45)    // #2D2D2D — edit/combo background
#define THEME_DARK_TEXT      RGB(255, 255, 255)

static struct
{
	BOOL Dark;
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
	gTheme.Dark = Theme__ReadSystemDark();
	Theme__ApplyAppMode();
	Theme__UpdateBrushes();
}

void Theme_Refresh(void)
{
	BOOL NewDark = Theme__ReadSystemDark();
	if (NewDark != gTheme.Dark)
	{
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
	return gTheme.Dark;
}

void Theme_ApplyTitleBar(HWND Window)
{
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

COLORREF Theme_BgColor(void)
{
	return gTheme.Dark ? THEME_DARK_BG : GetSysColor(COLOR_BTNFACE);
}

COLORREF Theme_TextColor(void)
{
	return gTheme.Dark ? THEME_DARK_TEXT : GetSysColor(COLOR_BTNTEXT);
}

COLORREF Theme_EditBgColor(void)
{
	return gTheme.Dark ? THEME_DARK_EDIT_BG : GetSysColor(COLOR_WINDOW);
}

INT_PTR Theme_HandleCtlColor(HDC Dc, UINT Msg)
{
	if (!gTheme.Dark)
	{
		return 0;
	}

	switch (Msg)
	{
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
			// dialog background, group boxes, static labels, checkbox text
			SetTextColor(Dc, THEME_DARK_TEXT);
			SetBkColor(Dc, THEME_DARK_BG);
			return (INT_PTR)gTheme.BgBrush;

		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			// edit boxes and combobox dropdown lists
			SetTextColor(Dc, THEME_DARK_TEXT);
			SetBkColor(Dc, THEME_DARK_EDIT_BG);
			return (INT_PTR)gTheme.EditBgBrush;

		default:
			return 0;
	}
}
