#pragma once

#include "wcap.h"

//
// Configuration module — persistent settings and settings dialog
//

#define CONFIG_VIDEO_H264 0
#define CONFIG_VIDEO_H265 1
#define CONFIG_VIDEO_AV1  2

#define CONFIG_VIDEO_BASE    0
#define CONFIG_VIDEO_MAIN    1
#define CONFIG_VIDEO_HIGH    2
#define CONFIG_VIDEO_MAIN_10 3

#define CONFIG_AUDIO_AAC  0
#define CONFIG_AUDIO_FLAC 1

typedef struct
{
	// capture
	BOOL MouseCursor;
	BOOL OnlyClientArea;
	BOOL ShowRecordingBorder;
	BOOL KeepRoundedWindowCorners;
	BOOL IncludeSecondaryWindows;
	BOOL HardwareEncoder;
	BOOL HardwarePreferIntegrated;
	// output
	WCHAR OutputFolder[MAX_PATH];
	BOOL OpenFolder;
	BOOL FragmentedOutput;
	BOOL EnableLimitLength;
	BOOL EnableLimitSize;
	DWORD LimitLength;
	DWORD LimitSize;
	// video
	BOOL GammaCorrectResize;
	BOOL ImprovedColorConversion;
	DWORD VideoCodec;
	DWORD VideoProfile;
	DWORD VideoMaxWidth;
	DWORD VideoMaxHeight;
	DWORD VideoMaxFramerate;
	DWORD VideoBitrate;
	// audio
	BOOL CaptureAudio;
	BOOL ApplicationLocalAudio;
	DWORD AudioCodec;
	DWORD AudioChannels;
	DWORD AudioSamplerate;
	DWORD AudioBitrate;
	// shortcuts
	DWORD ShortcutMonitor;
	DWORD ShortcutWindow;
	DWORD ShortcutRegion;
}
Config;

#define HOT_KEY(Key, Mod) ((Key) | ((Mod) << 24))
#define HOT_GET_KEY(KeyMod) ((KeyMod) & 0xffffff)
#define HOT_GET_MOD(KeyMod) (((KeyMod) >> 24) & 0xff)

// Capabilities and callbacks provided by caller to control dialog behavior
typedef struct
{
	bool CanHideMouseCursor;
	bool CanHideRecordingBorder;
	bool CanDisableRoundedCorners;
	bool CanIncludeSecondaryWindows;
	bool CanCaptureApplicationLocal;
	void (*DisableHotKeys)(void);
	BOOL (*EnableHotKeys)(void);
}
ConfigCapabilities;

void Config_Defaults(Config* C);
void Config_Load(Config* C, LPCWSTR FileName);
void Config_Save(Config* C, LPCWSTR FileName);

// Returns TRUE if user clicked OK (settings changed), FALSE otherwise
BOOL Config_ShowDialog(Config* C, const ConfigCapabilities* Cap);
