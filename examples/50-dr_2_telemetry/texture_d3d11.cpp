#include "imgui.h"
#include "reshade.hpp"

#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstdlib>

#include <mutex>
#include <cinttypes>

//#include <d3dcompiler.h>  // Optional: for shader compilation

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
// #pragma comment(lib, "d3dcompiler.lib")  // Optional

static std::mutex s_textures_mutex {};
#define MAX_TEXTURES 512
static ID3D11ShaderResourceView *all_textures[MAX_TEXTURES] {};

char *createRGBATexData(int width, int height, unsigned int rgbaColor) {
	char *rgbaData = new char[ width * height * sizeof(rgbaColor) ];
	if (rgbaData == NULL) {
		return NULL;
	}
	for (int i = 0; i < width * height; i++) {
		int offset = i * sizeof(rgbaColor);
		unsigned int* pOffset = (unsigned int *) &rgbaData[offset];
		*pOffset = rgbaColor;
	}
	return rgbaData;
}

bool loadFileFull(const char *fileName, char** pOutBuffer, size_t* pSize) {
	FILE *pFile = NULL;
	errno_t myErr = fopen_s(&pFile, fileName, "rb");
	if (myErr != 0) {
		if (pFile != NULL) {
			fclose(pFile);
		}
		return false;
	}
	if (pFile == NULL) {
		return false;
	}
	if (fseek(pFile, 0l, SEEK_END) != 0) {
		fclose(pFile);
		return false;
	}
	long fileSize = ftell(pFile);
	if (fseek(pFile, 0l, SEEK_SET) != 0) {
		fclose(pFile);

		return false;
	}
	*pOutBuffer = new char[fileSize];
	if (*pOutBuffer == NULL) {
		return false;
	}
	*pSize = fread(*pOutBuffer, sizeof(char), fileSize, pFile);
	fclose(pFile);
	return true;
}

bool releaseTexture(void *pTexture) {
	std::lock_guard<std::mutex>lock(s_textures_mutex);
	for (int i = 0; i < MAX_TEXTURES; i++)
	{
		if (all_textures[i] == pTexture) {
			all_textures[i] = NULL;
			i = MAX_TEXTURES;
		}
	}
	ID3D11ShaderResourceView *pd311Texture = (ID3D11ShaderResourceView *)pTexture;
	pd311Texture->Release();
	return true;
}

bool loadShaderRecourceViewfromBuffer(void *d3d11Device, uint8_t* pBuffer, int width, int height, DXGI_FORMAT pixelFormat ,void ** pOut) {
	using namespace reshade::log;
	D3D11_TEXTURE2D_DESC desc {};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = pixelFormat;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	
	D3D11_SUBRESOURCE_DATA initData {};
	initData.pSysMem = pBuffer;
	initData.SysMemPitch = width * 4;
	
	ID3D11Device* pDevice = (ID3D11Device*) d3d11Device;
	ID3D11Texture2D *pTextureOut = NULL;
	message(level::info, "CreateTexture2D ....");
    HRESULT result = pDevice->CreateTexture2D(&desc, &initData, &pTextureOut);
    if (FAILED(result)) {
        char buffer[1024];
        sprintf_s(buffer, 1024, "CreateTexture2D failed. HRESULT: 0x%08X, Width: %d, Height: %d, Format: %d", 
                  result, width, height, pixelFormat);
        message(level::info, buffer);
        return false;
    }
	if (pTextureOut == NULL) {
		message(level::info, "Could not init pTextureOut");
		return false;
	}
	ID3D11ShaderResourceView *out_srv = NULL;

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = pixelFormat;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	message(level::info, "CreateShaderResourceView ....");
	result = pDevice->CreateShaderResourceView(pTextureOut, &srvDesc, &out_srv);
	if (FAILED(result)) {
		message(level::info, "Hello");
		char buffer[1024];
		sprintf_s(buffer, 1024, "Could not CreateShaderResourceView( ....), result was  %d", result);
		message(level::info, buffer);
		if (pTextureOut != NULL) {
			pTextureOut->Release();
			pTextureOut = NULL;
		}
		return false;
	}

	message(level::info, "pTextureOut->Release() ....");
	pTextureOut->Release();
	pTextureOut = NULL;
	message(level::info, "pTextureOut->Released");
	*pOut = out_srv;

	/*
	std::lock_guard<std::mutex>lock(s_textures_mutex);
	int idx = 0;
	int freeSlot = -1;
	while (idx < MAX_TEXTURES && freeSlot < 0) {
		if (all_textures[idx] == NULL) {
			freeSlot = idx;
		}
		else {
			idx++;
		}
	}
	if (freeSlot < 0) {
		out_srv->Release();
		return false;
	}

	all_textures[freeSlot] = out_srv;
	*/
	*pOut = out_srv;
	
	return true;
}


bool loadTexRGBA32fromBuffer(void *d3d11Device, uint8_t *pBuffer, int width, int height, void **pOut) {
	return loadShaderRecourceViewfromBuffer(d3d11Device, pBuffer, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, pOut);
}

bool loadTexBGRA32fromBuffer(void *d3d11Device, uint8_t *pBuffer, int width, int height, void **pOut) {
	return loadShaderRecourceViewfromBuffer(d3d11Device, pBuffer, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, pOut);
}
