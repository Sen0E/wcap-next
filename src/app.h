#pragma once

#include "wcap.h"
#include "config.h"
#include "audio.h"
#include "capture.h"
#include "encode.h"
#include "ui.h"

#include <d3d11.h>
#include <dxgi1_6.h>

//
// Application orchestration module — recording state machine, capture entries
//

// Custom window messages
#define WM_WCAP_ALREADY_RUNNING (WM_USER+1)
#define WM_WCAP_STOP_CAPTURE    (WM_USER+2)
#define WM_WCAP_TRAY_TITLE      (WM_USER+3)
#define WM_WCAP_COMMAND         (WM_USER+4)

// Timers
#define WCAP_AUDIO_CAPTURE_TIMER    1
#define WCAP_AUDIO_CAPTURE_INTERVAL 100 // msec

#define WCAP_VIDEO_UPDATE_TIMER     2
#define WCAP_VIDEO_UPDATE_INTERVAL  100 // msec

// Tray menu commands
#define CMD_WCAP     1
#define CMD_QUIT     2
#define CMD_SETTINGS 3

// Hotkey IDs
#define HOT_RECORD_WINDOW  1
#define HOT_RECORD_MONITOR 2
#define HOT_RECORD_REGION  3

// Application state
typedef struct
{
	// Window & UI
	HWND Window;
	HICON IconIdle;
	HICON IconRecord;
	UINT  TaskbarCreatedMsg;
	HCURSOR CursorArrow;
	HCURSOR CursorClick;
	HCURSOR CursorResize[10];
	HFONT Font;
	HFONT FontBold;
	WCHAR ConfigPath[MAX_PATH];
	LARGE_INTEGER TickFreq;

	// Configuration
	Config Config;

	// Module instances
	AudioCapture Audio;
	ScreenCapture Capture;
	Encoder Encoder;

	// Recording state
	BOOL RecordingStarted;
	BOOL Recording;
	DWORD RecordingLimitFramerate;
	DWORD RecordingDroppedFrames;
	UINT64 RecordingLastFrame;
	UINT64 RecordingNextEncode;
	UINT64 RecordingNextTooltip;
	EXECUTION_STATE RecordingState;
	WCHAR RecordingPath[MAX_PATH];

	// Region selection state
	HMONITOR RectMonitor;
	HDC RectContext;
	HDC RectDarkContext;
	HBITMAP RectBitmap;
	HBITMAP RectDarkBitmap;
	DWORD RectWidth;
	DWORD RectHeight;
	BOOL RectSelected;
	POINT RectSelection[2];
	POINT RectMousePos;
	int RectResize;
	int RectSetSize[2];
	BOOL RectSetSizeClick;
}
AppState;

// Lifecycle
BOOL App_Init(AppState* App);
int  App_Run(AppState* App);

// Hotkey management
void App_DisableHotKeys(AppState* App);
BOOL App_EnableHotKeys(AppState* App);

// Capture entry points
void App_CaptureWindow(AppState* App);
void App_CaptureMonitor(AppState* App);
void App_CaptureRegionInit(AppState* App);
void App_CaptureRegion(AppState* App);
void App_CaptureRegionRelease(AppState* App);
void App_CaptureRegionDone(AppState* App);

// Frame callback
bool App_OnCaptureFrame(ScreenCapture* Capture, ScreenCaptureFrame* Frame);
