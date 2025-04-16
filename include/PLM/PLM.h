#pragma once

#define INCLUDE_LIGHTCRAFTER_WRAPPERS

#ifdef INCLUDE_LIGHTCRAFTER_WRAPPERS
#include <PLM/API.h>
#include "PLM/usb.h"

// Functions for direct USB communication with the PLM
namespace PLM {

	char versionStr[255];
	unsigned int API_ver, App_ver, SWConfig_ver, SeqConfig_ver;

	bool IsConnected() {
		return USB_IsConnected();
	};
	int Open() {
		return USB_Open();
	};
	int Close() {
		return USB_Close();
	};
	int GetVersion() {
		return LCR_GetVersion(&API_ver, &App_ver, &SWConfig_ver, &SeqConfig_ver);
	};
	int Play() {
		return LCR_PatternDisplay(0x2);
	};
	int Stop() {
		return LCR_PatternDisplay(0x1);
	};
}
#else
	namespace PLM {
		int Play() {
			return 0;
		};
		int Stop() {
			return 0;
		};
		int Open() {
			return 0;
	};
		int Close() {
			return 0;
		};
	}
#endif

using timepoint = std::chrono::time_point<std::chrono::high_resolution_clock>;
timepoint start;
timepoint end;

namespace PLM {

	void ImagescPLM(const char* title,
		uint8_t* data,
		ID3D11ShaderResourceView* data_texture_view,
		ID3D11Device* g_pd3dDevice,
		ID3D11DeviceContext* g_pd3dDeviceContext,
		ID3D11SamplerState* pSamplerState,
		ImGuiIO& io, int N, int M,
		std::mutex* mutex,
		int x0 = 0, int y0 = 0) {

		static int imgWidth = N / 4, imgHeight = M / 4;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2((float)N, (float)M), ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

		static ImGuiWindowFlags slm_window_flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoMouseInputs
			| ImGuiWindowFlags_NoNavInputs
			| ImGuiWindowFlags_NoNavFocus
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse;

		static bool p_open = true;
		ImGui::Begin(title, &p_open, slm_window_flags);
		ImGui::PopStyleVar(2);

		// Update texture data
		D3D11_MAPPED_SUBRESOURCE mapped_resource;
		ID3D11Texture2D* pTexture = NULL;
		data_texture_view->GetResource((ID3D11Resource**)&pTexture);
		g_pd3dDeviceContext->Map(pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);

		//g_pd3dDeviceContext->PSSetSamplers(0, 1, &pSamplerState);
		//start = std::chrono::high_resolution_clock::now();

		uint8_t* dest = static_cast<uint8_t*>(mapped_resource.pData);
		uint8_t* src = static_cast<uint8_t*>(data);

		for (uint64_t row = 0; row < M; ++row) {
			// Copy each row, taking into account the pitch
			memcpy(dest + row * mapped_resource.RowPitch,
				src + row * N * 4 * sizeof(uint8_t),
				(uint64_t) N * 4 * sizeof(uint8_t));
		}

		g_pd3dDeviceContext->Unmap(pTexture, 0);
		pTexture->Release();

		//end = std::chrono::high_resolution_clock::now();
		//std::chrono::duration<double> elapsed = end - start;
		//std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " us" << std::endl;
		static ImVec2 ulim = ImVec2(0.0f, 1.0f);
		static ImVec2 vlim = ImVec2(0.0f, 1.0f);

		int img_width = imgWidth, img_height = imgHeight;

		img_width = N;
		img_height = M;

		ImGui::Image((void*)data_texture_view, ImVec2((float)img_width, (float)img_height), ImVec2(ulim.x, vlim.x), ImVec2(ulim.y, vlim.y));

		ImGui::End();
	}

}


