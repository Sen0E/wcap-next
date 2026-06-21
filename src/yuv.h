#pragma once

#include "wcap.h"
#include <d3d11.h>

//
// GPU YUV conversion module — BGRA to NV12/P010 via compute shaders
//

typedef struct
{
	ID3D11Texture2D* Texture;
	ID3D11ShaderResourceView* ViewInUV;
	ID3D11UnorderedAccessView* ViewOutUV;
	ID3D11UnorderedAccessView* ViewOutY;
}
YuvConvertOutput;

typedef struct
{
	ID3D11ShaderResourceView* InputView;
	ID3D11ComputeShader* SinglePass;
	ID3D11ComputeShader* Pass1;
	ID3D11ComputeShader* Pass2;
	ID3D11Buffer* ConstantBuffer;
	uint32_t Width;
	uint32_t Height;
}
YuvConvert;

typedef enum
{
	YuvColorSpace_BT601,
	YuvColorSpace_BT709,
	YuvColorSpace_BT2020,
}
YuvColorSpace;

void YuvConvertOutput_Create(YuvConvertOutput* Output, ID3D11Device* Device, uint32_t Width, uint32_t Height, DXGI_FORMAT Format);
void YuvConvertOutput_Release(YuvConvertOutput* Output);

void YuvConvert_Create(YuvConvert* Convert, ID3D11Device* Device, ID3D11Texture2D* InputTexture, uint32_t Width, uint32_t Height, YuvColorSpace ColorSpace, bool ImprovedConversion);
void YuvConvert_Release(YuvConvert* Convert);

void YuvConvert_Dispatch(YuvConvert* Convert, ID3D11DeviceContext* Context, YuvConvertOutput* Output);
