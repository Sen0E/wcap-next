#pragma once

#include "wcap.h"
#include <audioclient.h>

//
// Audio capture module — WASAPI loopback capture
//

typedef struct
{
	IAudioClient* PlayClient;
	IAudioClient* RecordClient;
	IAudioCaptureClient* CaptureClient;
	WAVEFORMATEX* Format;
	uint64_t StartQpc;
	uint64_t StartPos;
	uint64_t Freq;
	bool UseDeviceTimestamp;
	bool CheckDeviceTimestamp;

	bool Stop;
	HANDLE Event;
	HANDLE Thread;

	uint8_t* Buffer;
	uint32_t BufferSize;
	_Atomic(uint32_t) BufferRead;
	_Atomic(uint32_t) BufferWrite;
}
AudioCapture;

typedef struct
{
	void* Samples;
	size_t Count;
	uint64_t Time; // compatible with QPC
}
AudioCaptureData;

bool AudioCapture_CanCaptureApplicationLocal(void);

// make sure CoInitializeEx has been called before calling Start()
bool AudioCapture_Start(AudioCapture* Capture, HWND ApplicationWindow);
void AudioCapture_Stop(AudioCapture* Capture);
void AudioCapture_Flush(AudioCapture* Capture);

// expectedTimestamp is used only first time GetData() is called to detect abnormal device timestamps
bool AudioCapture_GetData(AudioCapture* Capture, AudioCaptureData* Data, uint64_t ExpectedTimestamp);
void AudioCapture_ReleaseData(AudioCapture* Capture, AudioCaptureData* Data);
