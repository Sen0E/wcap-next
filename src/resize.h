#pragma once

#include "wcap.h"
#include <d3d11.h>

//
// GPU texture resize module — two-pass compute shader downscaling
//

typedef struct
{
	ID3D11Texture2D* InputTexture;
	ID3D11Texture2D* OutputTexture;

	ID3D11ShaderResourceView* InputViewIn;
	ID3D11UnorderedAccessView* OutputViewOut;
	ID3D11ShaderResourceView* MiddleViewIn;
	ID3D11UnorderedAccessView* MiddleViewOut;
	ID3D11ComputeShader* PassH;
	ID3D11ComputeShader* PassV;
	uint32_t InputWidth;
	uint32_t InputHeight;
	uint32_t OutputWidth;
	uint32_t OutputHeight;
}
TexResize;

void TexResize_Create(TexResize* Resize, ID3D11Device* Device, uint32_t InputWidth, uint32_t InputHeight, uint32_t OutputWidth, uint32_t OutputHeight, bool LinearSpace, D3D11_BIND_FLAG InputUsage);
void TexResize_Release(TexResize* Resize);

void TexResize_Dispatch(TexResize* Resize, ID3D11DeviceContext* Context);
