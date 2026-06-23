#pragma once

#include "wcap.h"

//
// Theme module — Windows 10/11 dark/light mode adaptation for Win32 UI
//

// Initialize theme: detect system dark mode and apply process-wide preferred app mode.
// Call once at application startup (before any UI is shown).
void Theme_Init(void);

// Re-detect system theme and re-apply process mode.
// Call on WM_SETTINGCHANGE with lParam == "ImmersiveColorSet".
void Theme_Refresh(void);

// Returns TRUE if system is currently in dark mode.
BOOL Theme_IsDark(void);

// Returns TRUE if system high-contrast mode is active.
// When active, theme colors are bypassed in favor of system colors.
BOOL Theme_IsHighContrast(void);

// Apply immersive dark mode to a window's title bar (non-client area).
// No-op when system is in light mode.
void Theme_ApplyTitleBar(HWND Window);

// Apply Windows 11 Mica backdrop material to a window.
// On Windows 10 / high-contrast / light mode this is a no-op.
// Requires Theme_ApplyTitleBar to be called first for best effect.
void Theme_ApplyMica(HWND Window);

// Adapt all child controls in a dialog for the current theme.
// In dark mode, disables visual styles for checkboxes/radio buttons so
// WM_CTLCOLORSTATIC text coloring takes effect (themed buttons ignore
// SetTextColor). In light mode, restores default visual styles.
// Call from WM_INITDIALOG and on WM_SETTINGCHANGE theme switches.
void Theme_ApplyToDialogControls(HWND Dialog);

// Color accessors for the current theme.
COLORREF Theme_BgColor(void);       // window/dialog background
COLORREF Theme_TextColor(void);     // normal text
COLORREF Theme_DisabledTextColor(void); // disabled controls text
COLORREF Theme_GroupBoxColor(void); // group box border
COLORREF Theme_EditBgColor(void);   // edit control / combobox list background

// Handle WM_CTLCOLORDLG / WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT / WM_CTLCOLORLISTBOX.
// Sets text/background colors in Dc and returns the brush to return from the
// dialog proc, or 0 to let default processing handle it (light mode).
// Dc may be NULL for a plain brush query (e.g. WM_CTLCOLORBTN).
INT_PTR Theme_HandleCtlColor(HDC Dc, UINT Msg);

// Handle WM_CTLCOLORBTN with explicit control window, so disabled-state
// detection and per-control coloring can be applied.
INT_PTR Theme_HandleCtlColorForWindow(HWND Control, HDC Dc, UINT Msg);

// Handle WM_DRAWITEM for owner-drawn buttons in dark mode.
// Returns TRUE if handled (button was drawn), FALSE if default processing
// should occur (light mode or non-button item).
BOOL Theme_DrawButton(const DRAWITEMSTRUCT* Dis);
