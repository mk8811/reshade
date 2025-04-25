// dllmain.cpp : Defines the entry point for the DLL application.
#include <imgui.h>
#include <reshade.hpp>
#include <chrono>

#include "udpreceiver.h"
#include "texture_d3d11.h"

#include <cstdio>
#include <cinttypes>

#include <cmath>
#include <mutex>
#include <unordered_map>

#include <windows.h>

#include "resource.h"

#define _VEC_2(u, v) (ImVec2(u, v))
#include "textures.h"

#pragma comment(lib, "Ws2_32.lib")

static bool s_enabled = false;
static bool s_process_attached = false;

extern "C" __declspec(dllexport) const char *NAME = "Dirt Rally 2.0 telemetry HUD";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Example add-on that displays a text in the settings.";

#define WIDTH 300
#define HEIGHT 150

struct texture_info {
	uint64_t native_device_ptr;
	uint32_t* texture_res;
	void* main_texture_d311;
	bool wasDestroyed;
};
static std::mutex mapMutex {};
static std::unordered_map<uint64_t, texture_info*> nativeDevicesMap {};

static HMODULE s_ddl_main_module = NULL;

/**
* Reshade finds this function using GetProcAddress(module, "AddonInit").
* It's invoked from addon_manager.cpp (reshade source) right after loading the module with LoadLibraryExW(...).
*
* Starting the receiver from DllMain entry point did not work properly.
* The combination of 'running c++ <thread>' and 'winsock2.2 api' caused problems/hangs.
*
* Standard reshade events (reshade::addon_event::init_device, etc) don't seem too useful.
*/
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	reshade::log::message(reshade::log::level::info, "AddonInit invoked");
	if (!startReceiver()) {
		reshade::log::message(reshade::log::level::info, "Could not start upd receiver.");
	}
	return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module) {
	reshade::log::message(reshade::log::level::info, "AddonUninit invoked");
	reshade::log::message(reshade::log::level::info, "Destroying receiver thread");
	destroyReceiver();
}

static void draw_settings(reshade::api::effect_runtime *runtime)
{
	ImGui::TextUnformatted("Hello Dirt Rally 2.0 telemetry HUD");
	ImGui::Checkbox("Enable", &s_enabled);
}


inline float clampf(float value, float min, float max) {
	if (value < min) {
		return min;
	}
	else if (value > max) {
		return max;
	}
	return value;
}

void drawGforceIndicator(reshade::api::effect_runtime *runtime, void *pMainTextureD311, float gLat, float gLon) {
	float width = ImGui::GetContentRegionAvail().x;
	float height = width;
	ImVec2 uv_0 = GFORCE_BG_UV0();
	ImVec2 uv_1 = GFORCE_BG_UV1();
	ImVec2 cursorPos = ImGui::GetCursorScreenPos();
	ImDrawList *pBgDrawList = ImGui::GetBackgroundDrawList();
	pBgDrawList->AddImage((ImTextureID)pMainTextureD311,
		cursorPos, ImVec2(cursorPos.x + width, cursorPos.y + height),
		uv_0, uv_1
	);

	gLat = clampf(gLat, -1.5f, 1.5f);
	gLon = clampf(gLon, -1.5f, 1.2f);

	ImVec2 center {
		width / 2,
		height / 2
	};

	const float indicatorWidth = GFORCE_IND_DIM;
	float offsetX = gLat / 1.5f;
	if (offsetX > 1.0f) {
		offsetX = 1.f;
	}
	else if (offsetX < -1.0f) {
		offsetX = -1.f;
	}
	float rangeX = center.x - floorf(indicatorWidth / 2);
	offsetX *= rangeX;

	const float indicatorHeight = GFORCE_IND_DIM;
	float offsetY = gLon / 1.2f;
	if (offsetY > 1.0f) {
		offsetY = 1.0f;
	}
	else if (offsetY < -1.0f) {
		offsetY = -1.0f;
	}
	float rangeY = center.y - floorf(indicatorHeight / 2);
	offsetY *= rangeY;

	uv_0 = GFORCE_IND_UV0();
	uv_1 = GFORCE_IND_UV1();
	ImVec2 topLeft {
		center.x + offsetX - indicatorWidth / 2.f,
		center.y + offsetY - indicatorHeight / 2.f
	};
	topLeft.x += cursorPos.x;
	topLeft.y += cursorPos.y;
	ImVec2 bottomRight = {
		topLeft.x + indicatorWidth,
		topLeft.y + indicatorHeight
	};
	pBgDrawList->AddImage((ImTextureID)pMainTextureD311,
		topLeft, bottomRight,
		uv_0, uv_1
	);
	
	ImGui::Dummy(ImVec2(width, height));
}

static bool firstDrawComplete = false;

static float lastTelemetryTimestamp = 0.f;

static std::chrono::time_point<std::chrono::steady_clock> lastTelemetryUpdate { };

void drawTelemetryWindow(reshade::api::effect_runtime *runtime) {
	using namespace std::chrono_literals;
	using namespace std::chrono;
	using namespace ImGui;

	if (!s_enabled) {
		firstDrawComplete = false;
		return;
	}
	
	if (s_enabled && !firstDrawComplete) {
		// Overlay was just activated. Show it for validation purpose
		reshade::log::message(reshade::log::level::info, "Overlay was just activated. Show it for validation purpose.");
		lastTelemetryUpdate = std::chrono::steady_clock().now();
	}

	TelemetryData telemetryData {};
	copyLatest(&telemetryData);

	const time_point<steady_clock> now { steady_clock().now() };

	if (telemetryData.time != lastTelemetryTimestamp) {
		// new data
		lastTelemetryTimestamp = telemetryData.time;
		lastTelemetryUpdate = now;
	}
	else {
		duration elapsedTime = now - lastTelemetryUpdate;
		if (elapsedTime > 5s) {
			// Stop drawing telemetry window - no more updates are arriving
			// reshade::log::message(reshade::log::level::info, "Hiding telemetry overlay because of no updates.");
			return;
		}
	}

	
	texture_info *currDeviceTextureInfo = NULL;
	mapMutex.lock();
	auto it = nativeDevicesMap.find(runtime->get_device()->get_native());
	if (it != nativeDevicesMap.end()) {
		currDeviceTextureInfo = it->second;
	}
	mapMutex.unlock();

	void *pMainTextureD311 = currDeviceTextureInfo == NULL ? NULL : currDeviceTextureInfo->main_texture_d311;
	/*
	ImVec2 fps_window_pos(5, 5);
	ImVec2 fps_window_size(200, 0);

	
	ImVec2 displaySize = GetIO().DisplaySize;
	
	ImVec2 myPos;
	myPos.x = 0 + 5;
	myPos.y = displaySize.y - HEIGHT - 5;
	ImVec2 mySize;
	mySize.x = WIDTH;
	mySize.y = HEIGHT;

	
	
	SetNextWindowPos(myPos);
	SetNextWindowSize(mySize);
	
	int windowFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_AlwaysAutoResize;
	ImGui::Begin("Telemetry", NULL, windowFlags);
	*/
	const float width = GetContentRegionAvail().x;
		ImGui::Text("Telemetry Window");
		char buffer[512] {};
		int stringLen = sprintf_s(buffer, sizeof(buffer), "%f", telemetryData.brake);
		ImGui::Text(buffer);
		ImGui::ProgressBar(telemetryData.brake, ImVec2(width, 20), "Brake");
		ImGui::ProgressBar(telemetryData.throttle, ImVec2(width, 20), "Throttle");
		if (pMainTextureD311 != NULL) {
			ImVec2 uv_0;
			ImVec2 uv_1;

			uv_0 = BLOCK_RED_UV0();
			uv_1 = BLOCK_RED_UV1();
			ImGui::Image((ImTextureID)pMainTextureD311,
				ImVec2(BLOCK_DIM, BLOCK_DIM),
				uv_0, uv_1
			);
			ImGui::SameLine();
			uv_0 = BLOCK_GREEN_UV0();
			uv_1 = BLOCK_GREEN_UV1();
			ImGui::Image((ImTextureID)pMainTextureD311,
				ImVec2(BLOCK_DIM, BLOCK_DIM),
				uv_0, uv_1
			);
			ImGui::SameLine();
			uv_0 = BLOCK_BLUE_UV0();
			uv_1 = BLOCK_BLUE_UV1();
			ImGui::Image((ImTextureID)pMainTextureD311,
				ImVec2(BLOCK_DIM, BLOCK_DIM),
				uv_0, uv_1
			);


			uv_0 = THROTTLE_H_UV0();
			uv_1 = THROTTLE_H_UV1();
			float uv_1_x = uv_1.x - uv_0.x;	// range
			uv_1_x *= telemetryData.throttle; // fraction of range
			if (uv_0.x < uv_1.x) {
				uv_1_x += uv_0.x;	// add initial offset
			}
			else {
				uv_1_x -= uv_0.x;	// add initial offset
			}
			uv_1.x = uv_1_x;
			float imgWidth = width * telemetryData.throttle;
			ImGui::Image((ImTextureID)pMainTextureD311,
				ImVec2(imgWidth, 20),
				uv_0, uv_1
			);

			uv_0 = BRAKE_H_UV0();
			uv_1 = BRAKE_H_UV1();
			uv_1_x = uv_1.x - uv_0.x;	// range
			uv_1_x *= telemetryData.brake; // fraction of range
			if (uv_0.x < uv_1.x) {
				uv_1_x += uv_0.x;	// add initial offset
			}
			else {
				uv_1_x -= uv_0.x;	// add initial offset
			}
			uv_1.x = uv_1_x;
			imgWidth = width * telemetryData.brake;
			ImGui::Image((ImTextureID)pMainTextureD311,
				ImVec2(imgWidth, 20),
				uv_0, uv_1
			);

			ImVec2 cursorPos = ImGui::GetCursorScreenPos();
			//ImDrawList *pFgDrawList = ImGui::GetForegroundDrawList();
			ImDrawList *pFgDrawList = ImGui::GetWindowDrawList();
			ImDrawList *pBgDrawList = ImGui::GetBackgroundDrawList();

			ImVec2 posLeftTop;
			ImVec2 posRightBtm;
			const int blockSize = 30;

			uv_0 = BLOCK_RED_UV0();
			uv_1 = BLOCK_RED_UV1();
			posLeftTop = cursorPos;
			posRightBtm = ImVec2(cursorPos.x + blockSize, cursorPos.y + blockSize);;
			pBgDrawList->AddImage((ImTextureID)pMainTextureD311,
				posLeftTop, posRightBtm,
				uv_0, uv_1
			);
			
			cursorPos.x += blockSize + 2;
			uv_0 = BLOCK_GREEN_UV0();
			uv_1 = BLOCK_GREEN_UV1();
			posLeftTop = cursorPos;
			posRightBtm = ImVec2(cursorPos.x + blockSize, cursorPos.y + blockSize);;
			pFgDrawList->AddImage((ImTextureID)pMainTextureD311,
				posLeftTop, posRightBtm,
				uv_0, uv_1
			);

			cursorPos.x += blockSize + 2;
			uv_0 = BLOCK_BLUE_UV0();
			uv_1 = BLOCK_BLUE_UV1();
			posLeftTop = cursorPos;
			posRightBtm = ImVec2(cursorPos.x + blockSize, cursorPos.y + blockSize);
			pFgDrawList->AddImage((ImTextureID)pMainTextureD311,
				posLeftTop, posRightBtm,
				uv_0, uv_1
			);
			
			ImGui::Dummy(ImVec2(width, 22));

			//drawGforceIndicator(runtime, pMainTextureD311, 0.f, 0.f);
			drawGforceIndicator(runtime, pMainTextureD311, telemetryData.gforceLat, telemetryData.gforceLon);

			ImGui::Text("Done");
		}
	//ImGui::End();

	firstDrawComplete = true;
}


void on_reshade_overlay(reshade::api::effect_runtime *runtime) {
	drawTelemetryWindow(runtime);
}



/*
static void *getMainTextureFromFile() {
	// bool loadRGBAFile(char *fileName, char** pOutBuffer, long* pSize)
	char *pOutBuffer = NULL;
	long size = -1;
	loadRGBAFile("texture.rgba32", &pOutBuffer, &size);
	return pOutBuffer;
}
*/

static texture_info* createMainTextureData(HMODULE hModule, reshade::api::device *pDevice) {
	using namespace reshade::log;
	char logBuffer[512];

	reshade::log::message(reshade::log::level::warning, "Loading main texture resource....");

	if (hModule == NULL) {
		reshade::log::message(reshade::log::level::warning, "Could not GetModuleHandle(...)");
		return NULL;
	}
	
	HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(IDR_MAIN_TEXTURE), RT_RCDATA);
	if (hRes == NULL) {
		reshade::log::message(reshade::log::level::warning, "Did not FindResource(...)");
		return NULL;
	}

	HGLOBAL hResLoad = LoadResource(hModule, hRes);
	if (hResLoad == NULL) {
		reshade::log::message(reshade::log::level::warning, "Could not LoadResource(...)");
		return NULL;
	}
	DWORD size = SizeofResource(hModule, hRes);
	void *pData = LockResource(hResLoad);
	if (pData == NULL) {
		reshade::log::message(reshade::log::level::error, "Could not lock embedded dll data");
		return NULL;
	}


	std::lock_guard<std::mutex> lock(mapMutex);

	if (nativeDevicesMap.end() != nativeDevicesMap.find(pDevice->get_native())) {
		sprintf_s(logBuffer, sizeof(logBuffer), "NOP. Texture info already present for native device id 0x%" PRIx64, pDevice->get_native());
		message(level::error, logBuffer);
		return NULL;
	}

	//reshade::log::message(reshade::log::level::warning, "Flipping bytes ....");
	const int numPixels = size / 4;
	uint8_t *pLocked = (uint8_t *)pData;
	uint32_t *pFlipped = new uint32_t[numPixels];
	for (int i = 0; i < numPixels; i++) {
		int offset = i * 4;
		uint32_t rgba = 0;
		/*
		* Electron's toBitmap() method writes the 32 bit color 
		* in bgra order. Flip blue and red to have rgba
		*/
		rgba |= pLocked[offset + 2] << 0;
		rgba |= pLocked[offset + 1] << 8;
		rgba |= pLocked[offset + 0] << 16;
		rgba |= pLocked[offset + 3] << 24;
		pFlipped[i] = rgba;
	}

	texture_info *pNewTextureInfo = new texture_info {};
	pNewTextureInfo->native_device_ptr = pDevice->get_native();
	pNewTextureInfo->texture_res = pFlipped;

	reshade::log::message(reshade::log::level::info, "Loaded main texture resource....");
	if (pFlipped != NULL) {
		message(level::error, "Loading main texture to graphics device...");
		bool texLoaded = loadTexRGBA32fromBuffer(
			(void*) pNewTextureInfo->native_device_ptr,
			(uint8_t *)pNewTextureInfo->texture_res,
			MAIN_TEX_WIDTH, MAIN_TEX_HEIGHT,
			&(pNewTextureInfo->main_texture_d311)
		);
		if (!texLoaded) {
			message(level::error, "Could not load main texture to graphics device.");
		}
		else {
			message(level::info, "Loaded main texture from dll resources.");
		}
	}
	else {
		message(level::error, "Could not find main texture resource in shared library.");
	}

	nativeDevicesMap.insert({ pNewTextureInfo->native_device_ptr, pNewTextureInfo });

	return pNewTextureInfo;
}

void on_reshade_init_device(reshade::api::device* pDevice) {
	using namespace reshade::log;
	using namespace reshade;
	using namespace reshade::api;
	const device_api api = pDevice->get_api();
	const uint64_t nativeDeviceAddr = pDevice->get_native();
	char logBuffer[256] {};
	switch (api) {
	case device_api::d3d11:
		snprintf(logBuffer, sizeof(logBuffer), "on_reshade_init_device: Found device_api %x (d3d11). native ptr is %" PRIx64,  api, nativeDeviceAddr);
		message(level::info, logBuffer);
		break;
	// return; on everything else
	case device_api::d3d9:
	case device_api::d3d10:
	case device_api::d3d12:
	case device_api::opengl:
	case device_api::vulkan:
		snprintf(logBuffer, sizeof(logBuffer), "on_reshade_init_device: Unsupported device_api %x. native ptr is %" PRIx64, api, nativeDeviceAddr);
		message(level::warning, logBuffer);
		return;
	default:
		snprintf(logBuffer, sizeof(logBuffer), "on_reshade_init_device: Unknown value for device_api %x. native ptr is %" PRIx64, api, nativeDeviceAddr);
		message(level::warning, logBuffer);
		return;
	}

	snprintf(logBuffer, sizeof(logBuffer), "nativeDeviceAddr is %" PRIx64, nativeDeviceAddr);
	message(level::info, logBuffer);
	
	message(level::warning, "Trying to load binary content from shared library resources....");
	createMainTextureData(s_ddl_main_module, pDevice);
	
}

void on_reshade_destroy_device(reshade::api::device *pDevice) {
	using namespace reshade;
	using namespace reshade::api;
	using namespace reshade::log;
	device_api api = pDevice->get_api();
	const uint64_t nativePtr = pDevice->get_native();
	char logBuffer[512] {};
	switch (api) {
	case device_api::d3d11:
		snprintf(logBuffer, sizeof(logBuffer), "on_reshade_destroy_device: device_api %x (d3d11). nativePtr %" PRIx64, api, nativePtr);
		log::message(log::level::info, logBuffer);
		break;
	default:
		snprintf(logBuffer, sizeof(logBuffer), "on_reshade_destroy_device: device_api %x. nativePtr %" PRIx64 "NOP", api, nativePtr);
		log::message(log::level::info, logBuffer);
		return;
	}

	texture_info *pTexInfo = NULL;
	std::lock_guard<std::mutex> lock(mapMutex);
	//mapMutex.lock();
	auto it = nativeDevicesMap.find(pDevice->get_native());
	if (it != nativeDevicesMap.end()) {
		pTexInfo = it->second;
	}
	// TODO remove pTexInfo from the map here and unlock
	//mapMutex.unlock();

	if (pTexInfo != NULL) {
		if (pTexInfo->texture_res != NULL) {
			delete(pTexInfo->texture_res);
			pTexInfo->texture_res = NULL;
		}
		if (pTexInfo->main_texture_d311 != NULL) {
			releaseTexture(pTexInfo->main_texture_d311);
			pTexInfo->main_texture_d311 = NULL;
		}
		if (pTexInfo->wasDestroyed) {
			sprintf_s(logBuffer, sizeof(logBuffer), "Device %" PRIx64 " was already destroyed before.", nativePtr);
			message(level::warning, logBuffer);
		}
		else {
			sprintf_s(logBuffer, sizeof(logBuffer), "Device %" PRIx64 " first time destroyed.", nativePtr);
			message(level::info, logBuffer);
		}
		pTexInfo->wasDestroyed = true;
	}
	else {
		sprintf_s(logBuffer, sizeof(logBuffer), "Didn't find device with native device ptr %" PRIx64, nativePtr);
		message(level::warning, logBuffer);
	}
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
	using namespace reshade::log;

    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		s_ddl_main_module = hModule;
		if (!reshade::register_addon(hModule))
		{
			return FALSE;
		}
		message(level::warning, "DLL_PROCESS_ATTACH: after register_addon");
		//createMainTextureData(hModule);

		reshade::register_overlay(NULL, draw_settings);
		// if registered as overlay, the window will have borders, titlebar, etc.
		//reshade::register_event<reshade::addon_event::reshade_overlay>(on_reshade_overlay);
		reshade::register_overlay("OSD", drawTelemetryWindow);

		reshade::register_event<reshade::addon_event::init_device>(on_reshade_init_device);
		reshade::register_event<reshade::addon_event::destroy_device>(on_reshade_destroy_device);
		break;
	case DLL_PROCESS_DETACH:
		message(level::warning, "DLL_PROCESS_DETACH");
		//reshade::unregister_event<reshade::addon_event::reshade_overlay>(on_reshade_overlay);
		reshade::unregister_overlay("OSD", drawTelemetryWindow);

		reshade::unregister_event<reshade::addon_event::init_device>(on_reshade_init_device);
		reshade::unregister_event<reshade::addon_event::destroy_device>(on_reshade_destroy_device);

		reshade::unregister_overlay(NULL, draw_settings);
		message(level::warning, "DLL_PROCESS_DETACH: Before reshade::unregister_addon(hModule)");
		
		reshade::unregister_addon(hModule);
		s_ddl_main_module = NULL;
		break;
    case DLL_THREAD_ATTACH:
		break;
    case DLL_THREAD_DETACH:
		break;
    }
    return TRUE;
}

