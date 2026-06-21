#pragma once

#define UNICODE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <intrin.h>
#include <stdatomic.h>

#define WCAP_TITLE L"wcap"
#define WCAP_URL   L"https://github.com/mmozeiko/wcap"

#if defined(WCAP_GIT_INFO)
#	define WCAP_CONFIG_TITLE "wcap, " __DATE__ " [" WCAP_GIT_INFO "]"
#else
#	define WCAP_CONFIG_TITLE "wcap, " __DATE__
#endif

#ifdef _DEBUG
#define Assert(Cond) do { if (!(Cond)) __debugbreak(); } while (0)
#else
#define Assert(Cond) (void)(Cond)
#endif
#define HR(hr) do { HRESULT _hr = (hr); Assert(SUCCEEDED(_hr)); } while (0)

// calculates ceil(X * Num / Den)
#define MUL_DIV_ROUND_UP(X, Num, Den) (((X) * (Num) - 1) / (Den) + 1)

// calculates ceil(X / Y)
#define DIV_ROUND_UP(X, Y) ( ((X) + (Y) - 1) / (Y) )

// MF works with 100nsec units
#define MF_UNITS_PER_SECOND 10000000ULL

#include <stdio.h>
#define StrFormat(Buffer, ...) _snwprintf(Buffer, _countof(Buffer), __VA_ARGS__)

// Public API marker (no-op, for documentation)
#define WCAP_API

// Linker dependencies
#pragma comment (lib, "ntdll")
#pragma comment (lib, "kernel32")
#pragma comment (lib, "user32")
#pragma comment (lib, "gdi32")
#pragma comment (lib, "msimg32")
#pragma comment (lib, "dxgi")
#pragma comment (lib, "d3dcompiler")
#pragma comment (lib, "d3d11")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "shell32")
#pragma comment (lib, "shlwapi")
#pragma comment (lib, "mfplat")
#pragma comment (lib, "mfuuid")
#pragma comment (lib, "mfreadwrite")
#pragma comment (lib, "evr")
#pragma comment (lib, "strmiids")
#pragma comment (lib, "ksuser")
#pragma comment (lib, "mmdevapi")
#pragma comment (lib, "ole32")
#pragma comment (lib, "wmcodecdspuuid")
#pragma comment (lib, "avrt")
#pragma comment (lib, "uxtheme")
#pragma comment (lib, "OneCore")
#pragma comment (lib, "CoreMessaging")
#pragma comment (lib, "advapi32")
