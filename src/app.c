#include "app.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <uxtheme.h>

#if defined(_M_AMD64)
// this is needed to be able to use Nvidia Media Foundation encoders on Optimus systems
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
#endif

// Single-instance global pointer for callbacks that don't receive AppState directly
static AppState* gApp;

// Wrappers for ConfigCapabilities callbacks (no-arg signatures)
static void App_DisableHotKeysWrapper(void) { App_DisableHotKeys(gApp); }
static BOOL App_EnableHotKeysWrapper(void) { return App_EnableHotKeys(gApp); }

// Helper to get AppState from window's user data
static AppState* App_FromWindow(HWND Window)
{
	return (AppState*)GetWindowLongPtrW(Window, GWLP_USERDATA);
}

// Forward declarations
static LRESULT CALLBACK App_WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
static void App_StartRecording(AppState* App, ID3D11Device* Device, HWND TargetWindow);
static void App_StopRecording(AppState* App);
static void App_EncodeCapturedAudio(AppState* App);
static ID3D11Device* App_CreateDevice(AppState* App);

bool App_OnCaptureFrame(ScreenCapture* Capture, ScreenCaptureFrame* Frame)
{
	(void)Capture;
	AppState* App = gApp;

	if (Frame == NULL)
	{
		PostMessageW(App->Window, WM_WCAP_STOP_CAPTURE, 0, 0);
		return true;
	}

	BOOL DoEncode = TRUE;
	DWORD LimitFramerate = App->RecordingLimitFramerate;
	if (LimitFramerate != 0)
	{
		if (Frame->Time * LimitFramerate < App->RecordingNextEncode)
		{
			DoEncode = FALSE;
		}
		else
		{
			if (App->RecordingNextEncode == 0)
			{
				App->RecordingNextEncode = Frame->Time * LimitFramerate;
			}
			App->RecordingNextEncode += App->TickFreq.QuadPart;
		}
	}

	// ignore frames if it comes from the past
	if (Frame->Time <= App->RecordingLastFrame)
	{
		DoEncode = FALSE;
	}
	App->RecordingLastFrame = Frame->Time;

	if (DoEncode)
	{
		if (!Encoder_NewFrame(&App->Encoder, Frame->Texture, Frame->Rect, Frame->Time, App->TickFreq.QuadPart))
		{
			// TODO: maybe highlight tray icon when dropped frames are increasing too much?
			App->RecordingDroppedFrames++;
		}
	}

	if (App->Config.EnableLimitLength || App->Config.EnableLimitSize)
	{
		BOOL Stop = FALSE;

		if (App->Config.EnableLimitLength)
		{
			if (Frame->Time - App->Encoder.StartTime >= (UINT64)(App->Config.LimitLength * App->TickFreq.QuadPart))
			{
				Stop = TRUE;
			}
		}
		if (App->Config.EnableLimitSize && !Stop)
		{
			UINT64 FileSize;
			DWORD Bitrate, LengthMsec;
			Encoder_GetStats(&App->Encoder, &Bitrate, &LengthMsec, &FileSize);

			// reserve 0.5% for mp4 format overhead (probably an overestimate)
			if (1000 * FileSize >= (995ULL * App->Config.LimitSize) << 20)
			{
				Stop = TRUE;
			}
		}

		if (Stop)
		{
			PostMessageW(App->Window, WM_WCAP_STOP_CAPTURE, 0, 0);
			return true;
		}
	}

	// update tray title with stats once every second
	if (App->RecordingNextTooltip == 0)
	{
		App->RecordingNextTooltip = Frame->Time + App->TickFreq.QuadPart;
	}
	else if (Frame->Time >= App->RecordingNextTooltip)
	{
		App->RecordingNextTooltip += App->TickFreq.QuadPart;

		// do the update, but not from frame callback to minimize time when texture is used
		PostMessageW(App->Window, WM_WCAP_TRAY_TITLE, 0, 0);
	}

	return true;
}

void App_DisableHotKeys(AppState* App)
{
	UnregisterHotKey(App->Window, HOT_RECORD_MONITOR);
	UnregisterHotKey(App->Window, HOT_RECORD_WINDOW);
	UnregisterHotKey(App->Window, HOT_RECORD_REGION);
}

BOOL App_EnableHotKeys(AppState* App)
{
	BOOL Success = TRUE;
	if (App->Config.ShortcutMonitor)
	{
		Success = Success && RegisterHotKey(App->Window, HOT_RECORD_MONITOR, HOT_GET_MOD(App->Config.ShortcutMonitor), HOT_GET_KEY(App->Config.ShortcutMonitor));
	}
	if (App->Config.ShortcutWindow)
	{
		Success = Success && RegisterHotKey(App->Window, HOT_RECORD_WINDOW, HOT_GET_MOD(App->Config.ShortcutWindow), HOT_GET_KEY(App->Config.ShortcutWindow));
	}
	if (App->Config.ShortcutRegion)
	{
		Success = Success && RegisterHotKey(App->Window, HOT_RECORD_REGION, HOT_GET_MOD(App->Config.ShortcutRegion), HOT_GET_KEY(App->Config.ShortcutRegion));
	}
	return Success;
}

static ID3D11Device* App_CreateDevice(AppState* App)
{
	IDXGIAdapter* Adapter = NULL;

	if (App->Config.HardwareEncoder)
	{
		IDXGIFactory* Factory;
		if (SUCCEEDED(CreateDXGIFactory(&IID_IDXGIFactory, (void**)&Factory)))
		{
			IDXGIFactory6* Factory6;
			if (SUCCEEDED(IDXGIFactory_QueryInterface(Factory, &IID_IDXGIFactory6, (void**)&Factory6)))
			{
				DXGI_GPU_PREFERENCE Preference = App->Config.HardwarePreferIntegrated ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
				if (FAILED(IDXGIFactory6_EnumAdapterByGpuPreference(Factory6, 0, Preference, &IID_IDXGIAdapter, &Adapter)))
				{
					// just to be safe
					Adapter = NULL;
				}
				IDXGIFactory6_Release(Factory6);
			}
			IDXGIFactory_Release(Factory);
		}
	}

	ID3D11Device* Device;

	UINT flags = 0;
#ifndef NDEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	// if adapter is selected then driver type must be unknown
	D3D_DRIVER_TYPE Driver = Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	if (FAILED(D3D11CreateDevice(Adapter, Driver, NULL, flags, (D3D_FEATURE_LEVEL[]) { D3D_FEATURE_LEVEL_11_0 }, 1, D3D11_SDK_VERSION, &Device, NULL, NULL)))
	{
		UI_ShowNotification(App->Window, L"无法初始化图形设备！", L"错误", NIIF_ERROR);
		Device = NULL;
	}
	if (Adapter)
	{
		IDXGIAdapter_Release(Adapter);
	}

	if (flags & D3D11_CREATE_DEVICE_DEBUG)
	{
		ID3D11InfoQueue* Info;
		if (SUCCEEDED(ID3D11Device_QueryInterface(Device, &IID_ID3D11InfoQueue, &Info)))
		{
			ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
			ID3D11InfoQueue_Release(Info);
		}
	}

	return Device;
}

static void App_StartRecording(AppState* App, ID3D11Device* Device, HWND TargetWindow)
{
	SYSTEMTIME Time;
	GetLocalTime(&Time);

	int Error = SHCreateDirectoryExW(NULL, App->Config.OutputFolder, NULL);
	if (Error != ERROR_SUCCESS && Error != ERROR_FILE_EXISTS && Error != ERROR_ALREADY_EXISTS)
	{
		UI_ShowNotification(App->Window, L"无法创建输出文件夹！", L"无法开始录制", NIIF_WARNING);
		ScreenCapture_Stop(&App->Capture);
		ID3D11Device_Release(Device);
		return;
	}

	WCHAR Filename[256];
	StrFormat(Filename, L"%04u%02u%02u_%02u%02u%02u.mp4", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond);

	StrCpyW(App->RecordingPath, App->Config.OutputFolder);
	PathAppendW(App->RecordingPath, Filename);

	DWM_TIMING_INFO Info = { .cbSize = sizeof(Info) };
	HR(DwmGetCompositionTimingInfo(NULL, &Info));

	DWORD FramerateNum = Info.rateCompose.uiNumerator;
	DWORD FramerateDen = Info.rateCompose.uiDenominator;
	if (App->Config.VideoMaxFramerate > 0 && App->Config.VideoMaxFramerate * FramerateDen < FramerateNum)
	{
		// limit rate only if max framerate is specified and it is lower than compositor framerate
		App->RecordingLimitFramerate = App->Config.VideoMaxFramerate;
		FramerateNum = App->Config.VideoMaxFramerate;
		FramerateDen = 1;
	}
	else
	{
		App->RecordingLimitFramerate = 0;
	}

	EncoderConfig EncConfig =
	{
		.Width = App->Capture.Rect.right - App->Capture.Rect.left,
		.Height = App->Capture.Rect.bottom - App->Capture.Rect.top,
		.FramerateNum = FramerateNum,
		.FramerateDen = FramerateDen,
		.Config = &App->Config,
	};

	if (App->Config.CaptureAudio)
	{
		HWND ApplicationWindow = App->Config.ApplicationLocalAudio && AudioCapture_CanCaptureApplicationLocal() ? TargetWindow : NULL;
		if (!AudioCapture_Start(&App->Audio, ApplicationWindow))
		{
			UI_ShowNotification(App->Window, L"无法录制声音！", L"无法开始录制", NIIF_WARNING);
			ScreenCapture_Stop(&App->Capture);
			ID3D11Device_Release(Device);
			return;
		}
		EncConfig.AudioFormat = App->Audio.Format;
	}

	if (!Encoder_Start(&App->Encoder, Device, App->RecordingPath, &EncConfig))
	{
		if (App->Config.CaptureAudio)
		{
			AudioCapture_Stop(&App->Audio);
		}
		ScreenCapture_Stop(&App->Capture);
		ID3D11Device_Release(Device);
		return;
	}

	App->RecordingNextTooltip = 0;
	App->RecordingNextEncode = 0;
	App->RecordingLastFrame = 0;
	App->RecordingDroppedFrames = 0;
	ScreenCapture_Start(&App->Capture, App->Config.MouseCursor, App->Config.ShowRecordingBorder, App->Config.IncludeSecondaryWindows);

	if (App->Config.CaptureAudio)
	{
		SetTimer(App->Window, WCAP_AUDIO_CAPTURE_TIMER, WCAP_AUDIO_CAPTURE_INTERVAL, NULL);
	}
	SetTimer(App->Window, WCAP_VIDEO_UPDATE_TIMER, WCAP_VIDEO_UPDATE_INTERVAL, NULL);

	UI_UpdateTrayIcon(App->Window, App->IconRecord);
	App->RecordingState = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
	App->Recording = TRUE;

	ID3D11Device_Release(Device);
}

static void App_EncodeCapturedAudio(AppState* App)
{
	if (App->Encoder.StartTime == 0)
	{
		// we don't know when first video frame starts yet
		return;
	}

	AudioCaptureData Data;
	while (AudioCapture_GetData(&App->Audio, &Data, App->Encoder.StartTime))
	{
		UINT32 FramesToEncode = (UINT32)Data.Count;
		if (Data.Time < App->Encoder.StartTime)
		{
			const UINT32 SampleRate = App->Audio.Format->nSamplesPerSec;
			const UINT32 BytesPerFrame = App->Audio.Format->nBlockAlign;

			// figure out how much time (100nsec units) and frame count to skip from current buffer
			UINT64 TimeToSkip = App->Encoder.StartTime - Data.Time;
			UINT32 FramesToSkip = (UINT32)((TimeToSkip * SampleRate - 1) / MF_UNITS_PER_SECOND + 1);
			if (FramesToSkip < FramesToEncode)
			{
				// need to skip part of captured data
				Data.Time += FramesToSkip * MF_UNITS_PER_SECOND / SampleRate;
				FramesToEncode -= FramesToSkip;
				if (Data.Samples)
				{
					Data.Samples = (BYTE*)Data.Samples + FramesToSkip * BytesPerFrame;
				}
			}
			else
			{
				// need to skip all of captured data
				FramesToEncode = 0;
			}
		}
		if (FramesToEncode != 0)
		{
			Assert(Data.Time >= App->Encoder.StartTime);
			Encoder_NewSamples(&App->Encoder, Data.Samples, FramesToEncode, Data.Time, App->TickFreq.QuadPart);
		}
		AudioCapture_ReleaseData(&App->Audio, &Data);
	}
}

static void App_StopRecording(AppState* App)
{
	App->Recording = FALSE;
	SetThreadExecutionState(App->RecordingState);

	if (App->Config.CaptureAudio)
	{
		KillTimer(App->Window, WCAP_AUDIO_CAPTURE_TIMER);
		AudioCapture_Flush(&App->Audio);
		App_EncodeCapturedAudio(App);
		AudioCapture_Stop(&App->Audio);
	}
	KillTimer(App->Window, WCAP_VIDEO_UPDATE_TIMER);

	ScreenCapture_Stop(&App->Capture);
	Encoder_Stop(&App->Encoder);
	if (App->Config.OpenFolder)
	{
		UI_ShowFileInFolder(App->RecordingPath);
	}

	SetWindowPos(App->Window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
	SetWindowLongW(App->Window, GWL_EXSTYLE, 0);

	UI_UpdateTrayIcon(App->Window, App->IconIdle);
	UI_UpdateTrayTitle(App->Window, WCAP_TITLE);
}

void App_CaptureWindow(AppState* App)
{
	HWND Window = GetForegroundWindow();
	if (Window == NULL)
	{
		UI_ShowNotification(App->Window, L"未选择窗口！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	// figure out who is owner of child window if somehow child window is selected (happens for fancy winamp skins)
	HWND Parent = GetParent(Window);
	while (Parent != NULL)
	{
		Window = Parent;
		Parent = GetParent(Window);
	}

	DWORD Affinity;
	BOOL Success = GetWindowDisplayAffinity(Window, &Affinity);
	Assert(Success);

	if (Affinity != WDA_NONE)
	{
		UI_ShowNotification(App->Window, L"该窗口不允许被录制！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	LONG ExStyle = GetWindowLongW(Window, GWL_EXSTYLE);
	if (ExStyle & WS_EX_TOOLWINDOW)
	{
		UI_ShowNotification(App->Window, L"无法录制工具栏窗口！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	ID3D11Device* Device = App_CreateDevice(App);
	if (!Device)
	{
		return;
	}

	if (!ScreenCapture_CreateForWindow(&App->Capture, Device, Window, App->Config.OnlyClientArea, !App->Config.KeepRoundedWindowCorners))
	{
		ID3D11Device_Release(Device);
		UI_ShowNotification(App->Window, L"无法录制所选窗口！", L"错误", NIIF_WARNING);
		return;
	}

	App_StartRecording(App, Device, Window);
}

void App_CaptureMonitor(AppState* App)
{
	POINT Mouse;
	GetCursorPos(&Mouse);

	HMONITOR Monitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONULL);
	if (Monitor == NULL)
	{
		UI_ShowNotification(App->Window, L"未知显示器！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	ID3D11Device* Device = App_CreateDevice(App);
	if (!Device)
	{
		return;
	}

	if (!ScreenCapture_CreateForMonitor(&App->Capture, Device, Monitor, NULL))
	{
		UI_ShowNotification(App->Window, L"无法录制所选显示器！", L"错误", NIIF_WARNING);
		return;
	}

	App_StartRecording(App, Device, NULL);
}

void App_CaptureRegionInit(AppState* App)
{
	POINT Mouse;
	GetCursorPos(&Mouse);

	HMONITOR Monitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONULL);
	if (Monitor == NULL)
	{
		UI_ShowNotification(App->Window, L"未知显示器！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	MONITORINFOEXW Info = { .cbSize = sizeof(Info) };
	GetMonitorInfoW(Monitor, (LPMONITORINFO)&Info);

	HDC DeviceContext = CreateDCW(L"DISPLAY", Info.szDevice, NULL, NULL);
	if (DeviceContext == NULL)
	{
		UI_ShowNotification(App->Window, L"获取显示器信息失败！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	DWORD Width = Info.rcMonitor.right - Info.rcMonitor.left;
	DWORD Height = Info.rcMonitor.bottom - Info.rcMonitor.top;

	// capture image from desktop

	HDC MemoryContext = CreateCompatibleDC(DeviceContext);
	Assert(MemoryContext);

	HBITMAP MemoryBitmap = CreateCompatibleBitmap(DeviceContext, Width, Height);
	Assert(MemoryBitmap);

	SelectObject(MemoryContext, MemoryBitmap);
	BitBlt(MemoryContext, 0, 0, Width, Height, DeviceContext, 0, 0, SRCCOPY);

	// prepare darkened image by doing alpha blend

	HDC MemoryDarkContext = CreateCompatibleDC(DeviceContext);
	Assert(MemoryDarkContext);

	HBITMAP MemoryDarkBitmap = CreateCompatibleBitmap(DeviceContext, Width, Height);
	Assert(MemoryDarkBitmap);

	BLENDFUNCTION Blend =
	{
		.BlendOp = AC_SRC_OVER,
		.SourceConstantAlpha = 0x40,
	};

	SelectObject(MemoryDarkContext, MemoryDarkBitmap);
	AlphaBlend(MemoryDarkContext, 0, 0, Width, Height, MemoryContext, 0, 0, Width, Height, Blend);

	// done

	DeleteDC(DeviceContext);

	App->RectMonitor = Monitor;
	App->RectContext = MemoryContext;
	App->RectDarkContext = MemoryDarkContext;
	App->RectBitmap = MemoryBitmap;
	App->RectDarkBitmap = MemoryDarkBitmap;
	App->RectWidth = Width;
	App->RectHeight = Height;
	App->RectSelected = FALSE;
	App->RectResize = WCAP_RESIZE_NONE;
	App->RectSetSize[0] = App->RectSetSize[1] = 0;
	App->RectSetSizeClick = FALSE;

	SetCursor(App->CursorResize[WCAP_RESIZE_NONE]);
	SetWindowPos(App->Window, HWND_TOPMOST, Info.rcMonitor.left, Info.rcMonitor.top, Width, Height, SWP_SHOWWINDOW);
	SetForegroundWindow(App->Window);
	InvalidateRect(App->Window, NULL, FALSE);
}

void App_CaptureRegionRelease(AppState* App)
{
	SetWindowPos(App->Window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
	SetWindowLongW(App->Window, GWL_EXSTYLE, 0);

	if (App->RectContext)
	{
		DeleteDC(App->RectContext);
		App->RectContext = NULL;

		DeleteObject(App->RectBitmap);
		App->RectBitmap = NULL;

		DeleteDC(App->RectDarkContext);
		App->RectDarkContext = NULL;

		DeleteObject(App->RectDarkBitmap);
		App->RectDarkBitmap = NULL;
	}
}

void App_CaptureRegionDone(AppState* App)
{
	SetCursor(App->CursorArrow);
	ReleaseCapture();

	App_CaptureRegionRelease(App);
}

void App_CaptureRegion(AppState* App)
{
	App_CaptureRegionDone(App);

	MONITORINFO Info = { .cbSize = sizeof(Info) };
	GetMonitorInfoW(App->RectMonitor, &Info);

	RECT Rect =
	{
		.left   = App->RectSelection[0].x,
		.right  = App->RectSelection[1].x,
		.top    = App->RectSelection[0].y,
		.bottom = App->RectSelection[1].y,
	};

	LONG ExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT;
	SetWindowLongW(App->Window, GWL_EXSTYLE, ExStyle);
	SetLayeredWindowAttributes(App->Window, RGB(255, 0, 255), 0, LWA_COLORKEY);

	ID3D11Device* Device = App_CreateDevice(App);
	if (!Device)
	{
		App_CaptureRegionRelease(App);
		return;
	}

	if (!ScreenCapture_CreateForMonitor(&App->Capture, Device, App->RectMonitor, &Rect))
	{
		UI_ShowNotification(App->Window, L"无法录制显示器！", L"错误", NIIF_WARNING);
		App_CaptureRegionRelease(App);
		return;
	}

	App_StartRecording(App, Device, NULL);

	if (App->Recording)
	{
		int X = Info.rcMonitor.left + Rect.left - (WCAP_RECT_BORDER + 1);
		int Y = Info.rcMonitor.top + Rect.top - (WCAP_RECT_BORDER + 1);
		int W = Rect.right - Rect.left + 2 * (WCAP_RECT_BORDER + 1);
		int H = Rect.bottom - Rect.top + 2 * (WCAP_RECT_BORDER + 1);
		SetWindowPos(App->Window, HWND_TOPMOST, X, Y, W, H, SWP_SHOWWINDOW);
		InvalidateRect(App->Window, NULL, FALSE);
	}
	else
	{
		App_CaptureRegionRelease(App);
	}
}

static LRESULT CALLBACK App_WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	AppState* App = App_FromWindow(Window);

	// WM_NCCREATE is the very first message — set up AppState before any other message
	if (Message == WM_NCCREATE)
	{
		App = (AppState*)((CREATESTRUCTW*)LParam)->lpCreateParams;
		SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)App);
		App->Window = Window;
		gApp = App;
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	// Guard against messages that might arrive before WM_NCCREATE sets App
	if (!App)
	{
		return DefWindowProcW(Window, Message, WParam, LParam);
	}

	if (Message == WM_CREATE)
	{
		HR(BufferedPaintInit());
		UI_AddTrayIcon(Window, App->IconIdle, WM_WCAP_COMMAND);
		return 0;
	}
	else if (Message == WM_DESTROY)
	{
		if (App->Recording)
		{
			App_StopRecording(App);
		}
		UI_RemoveTrayIcon(Window);
		PostQuitMessage(0);
		return 0;
	}
	else if (Message == WM_CLOSE)
	{
		if (App->RectContext)
		{
			App_CaptureRegionDone(App);
		}
		return 0;
	}
	else if (Message == WM_ACTIVATEAPP)
	{
		if (App->RectContext)
		{
			if (WParam == FALSE)
			{
				App_CaptureRegionDone(App);
				return 0;
			}
		}
	}
	else if (Message == WM_KEYDOWN)
	{
		if (App->RectContext)
		{
			if (WParam == VK_ESCAPE)
			{
				App_CaptureRegionDone(App);
				return 0;
			}
			else if (WParam == VK_RETURN)
			{
				if (App->RectSelected)
				{
					App_CaptureRegion(App);
				}
				return 0;
			}
		}
	}
	else if (Message == WM_LBUTTONDOWN)
	{
		if (App->RectContext)
		{
			if (App->RectSetSize[0])
			{
				App->RectSetSizeClick = TRUE;
				App->RectSelection[1].x = App->RectSelection[0].x + App->RectSetSize[0];
				App->RectSelection[1].y = App->RectSelection[0].y + App->RectSetSize[1];
				InvalidateRect(Window, NULL, FALSE);
			}
			else
			{
				int X = GET_X_LPARAM(LParam);
				int Y = GET_Y_LPARAM(LParam);

				int X0 = min(App->RectSelection[0].x, App->RectSelection[1].x);
				int Y0 = min(App->RectSelection[0].y, App->RectSelection[1].y);
				int X1 = max(App->RectSelection[0].x, App->RectSelection[1].x);
				int Y1 = max(App->RectSelection[0].y, App->RectSelection[1].y);

				int Resize = App->RectSelected ? UI_GetPointResize(X, Y, X0, Y0, X1, Y1) : WCAP_RESIZE_NONE;
				if (Resize == WCAP_RESIZE_NONE)
				{
					// initial rectangle will be empty
					App->RectSelection[0].x = App->RectSelection[1].x = X;
					App->RectSelection[0].y = App->RectSelection[1].y = Y;
					App->RectSelected = FALSE;

					InvalidateRect(Window, NULL, FALSE);
				}
				else
				{
					// resizing direction
					App->RectMousePos = (POINT){ X, Y };
				}

				App->RectResize = Resize;
				SetCapture(Window);
			}
			return 0;
		}
	}
	else if (Message == WM_LBUTTONUP)
	{
		if (App->RectContext)
		{
			if (App->RectSetSizeClick)
			{
				App->RectSetSizeClick = FALSE;
			}
			else
			{
				if (App->RectSelected)
				{
					// fix the selected rectangle coordinates, so next resizing starts on the correct side
					int X0 = min(App->RectSelection[0].x, App->RectSelection[1].x);
					int Y0 = min(App->RectSelection[0].y, App->RectSelection[1].y);
					int X1 = max(App->RectSelection[0].x, App->RectSelection[1].x);
					int Y1 = max(App->RectSelection[0].y, App->RectSelection[1].y);
					App->RectSelection[0] = (POINT){ X0, Y0 };
					App->RectSelection[1] = (POINT){ X1, Y1 };
				}
				ReleaseCapture();
			}
			return 0;
		}
	}
	else if (Message == WM_MOUSEMOVE)
	{
		if (App->RectContext)
		{
			int X = GET_X_LPARAM(LParam);
			int Y = GET_Y_LPARAM(LParam);

			if (App->RectSetSize[0])
			{
				SetCursor(App->CursorClick);
				InvalidateRect(Window, NULL, FALSE);
			}
			else if (App->RectSetSizeClick)
			{
				InvalidateRect(Window, NULL, FALSE);
			}
			else if (WParam & MK_LBUTTON)
			{
				BOOL Update = FALSE;

				if (App->RectResize == WCAP_RESIZE_TL || App->RectResize == WCAP_RESIZE_L || App->RectResize == WCAP_RESIZE_BL)
				{
					// left moved
					App->RectSelection[0].x = X;
					UI_AdjustRectSizeMultipleOf2(App->RectSelection, 0, 1);
					Update = TRUE;
				}
				else if (App->RectResize == WCAP_RESIZE_TR || App->RectResize == WCAP_RESIZE_R || App->RectResize == WCAP_RESIZE_BR)
				{
					// right moved
					App->RectSelection[1].x = X;
					UI_AdjustRectSizeMultipleOf2(App->RectSelection, 1, 0);
					Update = TRUE;
				}

				if (App->RectResize == WCAP_RESIZE_TL || App->RectResize == WCAP_RESIZE_T || App->RectResize == WCAP_RESIZE_TR)
				{
					// top moved
					App->RectSelection[0].y = Y;
					UI_AdjustRectSizeMultipleOf2(App->RectSelection, 0, 1);
					Update = TRUE;
				}
				else if (App->RectResize == WCAP_RESIZE_BL || App->RectResize == WCAP_RESIZE_B || App->RectResize == WCAP_RESIZE_BR)
				{
					// bottom moved
					App->RectSelection[1].y = Y;
					UI_AdjustRectSizeMultipleOf2(App->RectSelection, 1, 0);
					Update = TRUE;
				}

				if (App->RectResize == WCAP_RESIZE_M)
				{
					// if moving whole rectangle update both
					int DX = X - App->RectMousePos.x;
					int DY = Y - App->RectMousePos.y;
					App->RectMousePos = (POINT){ X, Y };

					App->RectSelection[0].x += DX;
					App->RectSelection[0].y += DY;
					App->RectSelection[1].x += DX;
					App->RectSelection[1].y += DY;

					Update = TRUE;
				}
				else if (App->RectResize == WCAP_RESIZE_NONE)
				{
					// no resize means we're selecting initial rectangle
					App->RectSelection[1].x = X;
					App->RectSelection[1].y = Y;
					UI_AdjustRectSizeMultipleOf2(App->RectSelection, 1, 0);
					if (App->RectSelection[0].x != App->RectSelection[1].x && App->RectSelection[0].y != App->RectSelection[1].y)
					{
						// when we have non-zero size rectangle, we're good with initial stage
						App->RectSelected = TRUE;
						Update = TRUE;
					}
				}

				if (Update)
				{
					InvalidateRect(Window, NULL, FALSE);
				}
			}
			else
			{
				int X0 = min(App->RectSelection[0].x, App->RectSelection[1].x);
				int Y0 = min(App->RectSelection[0].y, App->RectSelection[1].y);
				int X1 = max(App->RectSelection[0].x, App->RectSelection[1].x);
				int Y1 = max(App->RectSelection[0].y, App->RectSelection[1].y);
				int Resize = App->RectSelected ? UI_GetPointResize(X, Y, X0, Y0, X1, Y1) : WCAP_RESIZE_NONE;
				SetCursor(App->CursorResize[Resize]);

				if (Resize == WCAP_RESIZE_NONE)
				{
					// in case hovering over resize text
					InvalidateRect(Window, NULL, FALSE);
				}
			}

			return 0;
		}
	}
	else if (Message == WM_TIMER)
	{
		if (App->Recording)
		{
			if (WParam == WCAP_AUDIO_CAPTURE_TIMER)
			{
				App_EncodeCapturedAudio(App);
				return 0;
			}
			else if (WParam == WCAP_VIDEO_UPDATE_TIMER)
			{
				LARGE_INTEGER Time;
				QueryPerformanceCounter(&Time);
				Encoder_Update(&App->Encoder, Time.QuadPart, App->TickFreq.QuadPart);
				return 0;
			}
		}
	}
	else if (Message == WM_POWERBROADCAST)
	{
		if (WParam == PBT_APMQUERYSUSPEND)
		{
			if (App->Recording)
			{
				if (LParam & 1)
				{
					// reject request to suspend when recording
					return BROADCAST_QUERY_DENY;
				}
				else
				{
					// if cannot prevent suspend, need to stop recording
					App_StopRecording(App);
				}
			}
		}
		return TRUE;
	}
	else if (Message == WM_SETTINGCHANGE)
	{
		// Refresh theme on light/dark switch or high-contrast toggle.
		// "ImmersiveColorSet" is broadcast for dark/light theme changes.
		// "WindowMetrics" is broadcast for high-contrast on/off changes.
		if (LParam &&
			(lstrcmpW((LPCWSTR)LParam, L"ImmersiveColorSet") == 0 ||
			 lstrcmpW((LPCWSTR)LParam, L"WindowMetrics") == 0))
		{
			Theme_Refresh();
		}
	}
	else if (Message == WM_WCAP_COMMAND)
	{
		if (LOWORD(LParam) == WM_RBUTTONUP)
		{
			HMENU Menu = CreatePopupMenu();
			Assert(Menu);

			AppendMenuW(Menu, MF_STRING | (App->Recording ? MF_DISABLED : 0), CMD_SETTINGS, L"设置");
			AppendMenuW(Menu, MF_STRING, CMD_QUIT, L"退出");

			POINT Mouse;
			GetCursorPos(&Mouse);

			SetForegroundWindow(Window);
			int Command = TrackPopupMenu(Menu, TPM_RETURNCMD | TPM_NONOTIFY, Mouse.x, Mouse.y, 0, Window, NULL);
			if (Command == CMD_QUIT)
			{
				DestroyWindow(Window);
			}
			else if (Command == CMD_SETTINGS)
			{
				ConfigCapabilities Cap =
				{
					.CanHideMouseCursor = ScreenCapture_CanHideMouseCursor(),
					.CanHideRecordingBorder = ScreenCapture_CanHideRecordingBorder(),
					.CanDisableRoundedCorners = ScreenCapture_CanDisableRoundedCorners(),
					.CanIncludeSecondaryWindows = ScreenCapture_CanIncludeSecondaryWindows(),
					.CanCaptureApplicationLocal = AudioCapture_CanCaptureApplicationLocal(),
					.DisableHotKeys = App_DisableHotKeysWrapper,
					.EnableHotKeys = App_EnableHotKeysWrapper,
				};
				if (Config_ShowDialog(&App->Config, &Cap))
				{
					Config_Save(&App->Config, App->ConfigPath);
					App_DisableHotKeys(App);
					App_EnableHotKeys(App);
				}
			}

			DestroyMenu(Menu);
		}
		else if (LOWORD(LParam) == WM_LBUTTONUP)
		{
			if (!App->Recording)
			{
				ConfigCapabilities Cap =
				{
					.CanHideMouseCursor = ScreenCapture_CanHideMouseCursor(),
					.CanHideRecordingBorder = ScreenCapture_CanHideRecordingBorder(),
					.CanDisableRoundedCorners = ScreenCapture_CanDisableRoundedCorners(),
					.CanIncludeSecondaryWindows = ScreenCapture_CanIncludeSecondaryWindows(),
					.CanCaptureApplicationLocal = AudioCapture_CanCaptureApplicationLocal(),
					.DisableHotKeys = App_DisableHotKeysWrapper,
					.EnableHotKeys = App_EnableHotKeysWrapper,
				};
				if (Config_ShowDialog(&App->Config, &Cap))
				{
					Config_Save(&App->Config, App->ConfigPath);
					App_DisableHotKeys(App);
					App_EnableHotKeys(App);
				}
			}
		}
		else if (LOWORD(LParam) == NIN_BALLOONUSERCLICK)
		{
			// TODO: no idea how to prevent this happening for right-click on tray icon...
			UI_ShowFileInFolder(App->RecordingPath);
		}
		return 0;
	}
	else if (Message == WM_HOTKEY)
	{
		if (App->Recording)
		{
			App_StopRecording(App);
		}
		else if (!App->RecordingStarted)
		{
			if (App->RectContext == NULL)
			{
				if (WParam == HOT_RECORD_WINDOW)
				{
					App->RecordingStarted = TRUE;
					App_CaptureWindow(App);
					App->RecordingStarted = FALSE;
				}
				else if (WParam == HOT_RECORD_MONITOR)
				{
					App->RecordingStarted = TRUE;
					App_CaptureMonitor(App);
					App->RecordingStarted = FALSE;
				}
				else if (WParam == HOT_RECORD_REGION)
				{
					App->RecordingStarted = TRUE;
					App_CaptureRegionInit(App);
					App->RecordingStarted = FALSE;
				}
			}
		}
		return 0;
	}
	else if (Message == WM_WCAP_TRAY_TITLE)
	{
		if (App->Recording)
		{
			UINT64 FileSize;
			DWORD Bitrate, LengthMsec;
			Encoder_GetStats(&App->Encoder, &Bitrate, &LengthMsec, &FileSize);

			WCHAR LengthText[128];
			StrFromTimeIntervalW(LengthText, _countof(LengthText), LengthMsec, 6);

			WCHAR SizeText[128];
			StrFormatByteSizeW(FileSize, SizeText, _countof(SizeText));

			WCHAR Text[1024];
			StrFormat(Text, L"录制中: %dx%d @ %.2f\n时长: %ls\n码率: %u kbit/s\n大小: %ls\n丢帧: %u",
				App->Encoder.OutputWidth, App->Encoder.OutputHeight,
				(float)App->Encoder.FramerateNum / (float)App->Encoder.FramerateDen,
				LengthText,
				Bitrate,
				SizeText,
				App->RecordingDroppedFrames);

			UI_UpdateTrayTitle(Window, Text);
		}
		return 0;
	}
	else if (Message == WM_WCAP_STOP_CAPTURE)
	{
		if (App->Recording)
		{
			App_StopRecording(App);
		}
		return 0;
	}
	else if (Message == WM_WCAP_ALREADY_RUNNING)
	{
		UI_ShowNotification(Window, L"wcap 已在运行！", NULL, NIIF_INFO);
		return 0;
	}
	else if (Message == App->TaskbarCreatedMsg)
	{
		// in case taskbar was re-created (explorer.exe crashed) add our icon back
		UI_AddTrayIcon(Window, App->IconIdle, WM_WCAP_COMMAND);
		return 0;
	}
	else if (Message == WM_ERASEBKGND)
	{
		return 1;
	}
	else if (Message == WM_PAINT)
	{
		PAINTSTRUCT Paint;
		HDC PaintContext = BeginPaint(Window, &Paint);

		HDC Context;
		HPAINTBUFFER BufferedPaint = BeginBufferedPaint(PaintContext, &Paint.rcPaint, BPBF_COMPATIBLEBITMAP, NULL, &Context);
		if (BufferedPaint)
		{
			if (App->RectContext)
			{
				{
					int X = Paint.rcPaint.left;
					int Y = Paint.rcPaint.top;
					int W = Paint.rcPaint.right - Paint.rcPaint.left;
					int H = Paint.rcPaint.bottom - Paint.rcPaint.top;

					// draw darkened screenshot
					BitBlt(Context, X, Y, W, H, App->RectDarkContext, X, Y, SRCCOPY);
				}

				UI_PaintRegion(Window, Context,
					App->RectContext, App->RectDarkContext,
					App->RectWidth, App->RectHeight,
					App->RectSelected, App->RectSelection,
					App->RectSetSize, App->Font, App->FontBold,
					App->CursorClick, App->CursorResize);
			}
			else
			{
				RECT Rect;
				GetClientRect(Window, &Rect);

				HBRUSH BorderBrush = CreateSolidBrush(RGB(255, 255, 0));
				Assert(BorderBrush);
				FillRect(Context, &Rect, BorderBrush);
				DeleteObject(BorderBrush);

				Rect.left += WCAP_RECT_BORDER;
				Rect.top += WCAP_RECT_BORDER;
				Rect.right -= WCAP_RECT_BORDER;
				Rect.bottom -= WCAP_RECT_BORDER;

				HBRUSH ColorKeyBrush = CreateSolidBrush(RGB(255, 0, 255));
				Assert(ColorKeyBrush);
				FillRect(Context, &Rect, ColorKeyBrush);
				DeleteObject(ColorKeyBrush);

				FrameRect(Context, &Rect, GetStockObject(BLACK_BRUSH));
			}

			EndBufferedPaint(BufferedPaint, TRUE);
		}

		EndPaint(Window, &Paint);
		return 0;
	}

	return DefWindowProcW(Window, Message, WParam, LParam);
}

BOOL App_Init(AppState* App)
{
	Theme_Init();

	WNDCLASSEXW WindowClass =
	{
		.cbSize = sizeof(WindowClass),
		.lpfnWndProc = App_WindowProc,
		.hInstance = GetModuleHandleW(NULL),
		.lpszClassName = L"wcap_window_class",
	};

	HWND Existing = FindWindowW(WindowClass.lpszClassName, NULL);
	if (Existing)
	{
		PostMessageW(Existing, WM_WCAP_ALREADY_RUNNING, 0, 0);
		return FALSE;
	}

	if (!ScreenCapture_IsSupported())
	{
		MessageBoxW(NULL, L"需要 Windows 10 1903 版本（2019年5月更新）或更高版本！", WCAP_TITLE, MB_ICONEXCLAMATION);
		return FALSE;
	}

	GetModuleFileNameW(NULL, App->ConfigPath, _countof(App->ConfigPath));
	PathRenameExtensionW(App->ConfigPath, L".ini");

	HR(CoInitializeEx(0, COINIT_APARTMENTTHREADED));

	Config_Defaults(&App->Config);
	Config_Load(&App->Config, App->ConfigPath);
	ScreenCapture_Create(&App->Capture, &App_OnCaptureFrame, false);
	Encoder_Init(&App->Encoder);

	QueryPerformanceFrequency(&App->TickFreq);

	App->CursorArrow = LoadCursor(NULL, IDC_ARROW);
	App->CursorClick = LoadCursor(NULL, IDC_HAND);
	App->CursorResize[WCAP_RESIZE_NONE] = LoadCursor(NULL, IDC_CROSS);
	App->CursorResize[WCAP_RESIZE_M]    = LoadCursor(NULL, IDC_SIZEALL);
	App->CursorResize[WCAP_RESIZE_T]    = App->CursorResize[WCAP_RESIZE_B]  = LoadCursor(NULL, IDC_SIZENS);
	App->CursorResize[WCAP_RESIZE_L]    = App->CursorResize[WCAP_RESIZE_R]  = LoadCursor(NULL, IDC_SIZEWE);
	App->CursorResize[WCAP_RESIZE_TL]   = App->CursorResize[WCAP_RESIZE_BR] = LoadCursor(NULL, IDC_SIZENWSE);
	App->CursorResize[WCAP_RESIZE_TR]   = App->CursorResize[WCAP_RESIZE_BL] = LoadCursor(NULL, IDC_SIZENESW);

	App->Font = CreateFontW(-WCAP_UI_FONT_SIZE, 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, WCAP_UI_FONT);
	Assert(App->Font);

	App->FontBold = CreateFontW(-WCAP_UI_FONT_SIZE, 0, 0, 0, FW_BOLD,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, WCAP_UI_FONT);
	Assert(App->FontBold);

	App->IconIdle = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(1));
	App->IconRecord = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(2));
	Assert(App->IconIdle && App->IconRecord);

	App->TaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
	Assert(App->TaskbarCreatedMsg);

	ATOM Atom = RegisterClassExW(&WindowClass);
	Assert(Atom);

	App->Window = CreateWindowExW(
		0, WindowClass.lpszClassName, WCAP_TITLE, WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, WindowClass.hInstance, App);
	if (!App->Window)
	{
		return FALSE;
	}

	if (!App_EnableHotKeys(App))
	{
		MessageBoxW(NULL,
			L"无法注册 wcap 快捷键。\n可能已有其他程序使用了这些快捷键。\n请检查并调整设置！",
			WCAP_TITLE, MB_ICONEXCLAMATION);
	}

	return TRUE;
}

int App_Run(AppState* App)
{
	for (;;)
	{
		MSG Message;
		BOOL Result = GetMessageW(&Message, NULL, 0, 0);
		if (Result == 0)
		{
			return 0;
		}
		Assert(Result > 0);

		TranslateMessage(&Message);
		DispatchMessageW(&Message);
	}
}
