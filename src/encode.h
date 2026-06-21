#pragma once

#include "wcap.h"
#include "config.h"
#include "resize.h"
#include "yuv.h"

#include <d3d11_4.h>
#include <mfidl.h>
#include <mfreadwrite.h>

//
// Encoder module — Media Foundation MP4 encoding pipeline
//

#define ENCODER_VIDEO_BUFFER_COUNT 8
#define ENCODER_AUDIO_BUFFER_COUNT 16

typedef struct
{
	DWORD InputWidth;   // width to what input will be cropped
	DWORD InputHeight;  // height to what input will be cropped
	DWORD OutputWidth;  // width of video output
	DWORD OutputHeight; // height of video output
	DWORD FramerateNum; // video output framerate numerator
	DWORD FramerateDen; // video output framerate denominator
	UINT64 StartTime;   // time in QPC ticks since first call of NewFrame

	IMFAsyncCallback VideoSampleCallback;
	IMFAsyncCallback AudioSampleCallback;
	ID3D11DeviceContext* Context;
	ID3D11Multithread* Multithread;
	IMFSinkWriter* Writer;
	int VideoStreamIndex;
	int AudioStreamIndex;

	ID3D11RenderTargetView* InputView;

	TexResize Resize;
	YuvConvert Convert;

	YuvConvertOutput  ConvertOutput[ENCODER_VIDEO_BUFFER_COUNT];
	IMFSample*        VideoSample[ENCODER_VIDEO_BUFFER_COUNT];
	_Atomic(uint64_t) VideoSampleAvailable;

	BOOL   VideoDiscontinuity;
	UINT64 VideoLastTime;

	IMFTransform*     Resampler;
	IMFSample*        AudioSample[ENCODER_AUDIO_BUFFER_COUNT];
	_Atomic(uint64_t) AudioSampleAvailable;

	IMFSample*      AudioInputSample;
	DWORD           AudioFrameSize;
	DWORD           AudioSampleRate;
}
Encoder;

typedef struct
{
	DWORD Width;
	DWORD Height;
	DWORD FramerateNum;
	DWORD FramerateDen;
	WAVEFORMATEX* AudioFormat;
	Config* Config;
}
EncoderConfig;

void Encoder_Init(Encoder* Encoder);
BOOL Encoder_Start(Encoder* Encoder, ID3D11Device* Device, LPWSTR FileName, const EncoderConfig* Config);
void Encoder_Stop(Encoder* Encoder);

BOOL Encoder_NewFrame(Encoder* Encoder, ID3D11Texture2D* Texture, RECT Rect, UINT64 Time, UINT64 TimePeriod);
void Encoder_NewSamples(Encoder* Encoder, LPCVOID Samples, DWORD FrameCount, UINT64 Time, UINT64 TimePeriod);
void Encoder_Update(Encoder* Encoder, UINT64 Time, UINT64 TimePeriod);
void Encoder_GetStats(Encoder* Encoder, DWORD* Bitrate, DWORD* LengthMsec, UINT64* FileSize);
