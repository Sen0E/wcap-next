#pragma once

#include "wcap.h"
#include <d3d11.h>
#include <dwmapi.h>

#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION 0x130000
#include <windows.graphics.capture.h>

typedef struct IGraphicsCaptureItemInterop IGraphicsCaptureItemInterop;

//
// Screen capture module — Windows.Graphics.Capture WinRT API wrapper
//

typedef struct
{
	// public
	ID3D11Texture2D* Texture;
	uint64_t Time;
	RECT Rect;
	// private
	uint32_t Width;
	uint32_t Height;
	IUnknown* NextFrame;
}
ScreenCaptureFrame;

typedef struct ScreenCapture ScreenCapture;

// return true to continue capture, or false stop any further captures
typedef bool ScreenCapture_OnFrameCallback(ScreenCapture* Capture, ScreenCaptureFrame* Frame);

typedef struct ScreenCapture
{
	IGraphicsCaptureItemInterop* ItemInterop;
	__x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFramePoolStatics* FramePoolStatics;
	__x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFramePoolStatics2* FramePoolStatics2;
	IInspectable* Controller; // actually IDispatcherQueueController

	__x_ABI_CWindows_CGraphics_CDirectX_CDirect3D11_CIDirect3DDevice* Device;
	__x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureItem* Item;
	__x_ABI_CWindows_CGraphics_CCapture_CIDirect3D11CaptureFramePool* FramePool;
	__x_ABI_CWindows_CGraphics_CCapture_CIGraphicsCaptureSession* Session;

	ScreenCapture_OnFrameCallback* OnFrame;
	__FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable OnFrameHandler;
	__FITypedEventHandler_2_Windows__CGraphics__CCapture__CGraphicsCaptureItem_IInspectable OnCloseHandler;
	EventRegistrationToken OnFrameToken;
	EventRegistrationToken OnCloseToken;

	__x_ABI_CWindows_CGraphics_CSizeInt32 CurrentSize;
	RECT Rect;
	HWND Window;
	bool OnlyClientArea;

	bool RestoreWindowCornerPreference;
	DWM_WINDOW_CORNER_PREFERENCE WindowCornerPreference;
}
ScreenCapture;

bool ScreenCapture_IsSupported(void);
bool ScreenCapture_CanHideMouseCursor(void);
bool ScreenCapture_CanHideRecordingBorder(void);
bool ScreenCapture_CanDisableRoundedCorners(void);
bool ScreenCapture_CanIncludeSecondaryWindows(void);

// if OnFrame is NULL then you need to periodically call GetFrame/ReleaseFrame manually
// if OnFrame is not NULL and CallbackOnThread is false then callback will be invoked from message processing loop on the same thread as Create call
// if OnFrame is not NULL and CallbackOnThread is true, then callback will be invoked on background thread
void ScreenCapture_Create(ScreenCapture* Capture, ScreenCapture_OnFrameCallback* OnFrame, bool CallbackOnThread);
void ScreenCapture_Release(ScreenCapture* Capture);

bool ScreenCapture_CreateForWindow(ScreenCapture* Capture, ID3D11Device* Device, HWND Window, bool OnlyClientArea, bool DisableRoundedCorners);
bool ScreenCapture_CreateForMonitor(ScreenCapture* Capture, ID3D11Device* Device, HMONITOR Monitor, const RECT* Rect);
void ScreenCapture_Start(ScreenCapture* Capture, bool WithMouseCursor, bool WithRecordingBorder, bool IncludeSecondaryWindows);
void ScreenCapture_Stop(ScreenCapture* Capture);

bool ScreenCapture_GetFrame(ScreenCapture* Capture, ScreenCaptureFrame* Frame);
void ScreenCapture_ReleaseFrame(ScreenCapture* Capture, ScreenCaptureFrame* Frame);
