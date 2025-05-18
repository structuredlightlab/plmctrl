#pragma once

#define INCLUDE_LIGHTCRAFTER_WRAPPERS

#ifdef INCLUDE_LIGHTCRAFTER_WRAPPERS
#include <PLM/API.h>
#include "PLM/usb.h"


struct BitElement
{
	enum Color
	{
		RED = 1,
		GREEN = 2,
		BLUE = 4,
	};

	int exposure;
	bool clear;
	int bits;
	Color color;
	bool trigIn;
	int darkPeriod;
	bool trigOut2;
	int splashImageIndex;
	int splashImageBitPos;

	BitElement()
	{
		exposure = 0;
		color = RED;
		bits = 1;
		trigIn = true;
		clear = true;
		splashImageIndex = 0;
		splashImageBitPos = 0x1;
		darkPeriod = 0;
		trigOut2 = true;
	}
};

typedef enum {
	VIDEO_DISABLED,
	VIDEO_HDMI,
	VIDEO_DP,
} ConnectionType;



// They follow this order: splashImageBitPos, Bits, exposure, dark period, color, trigIn, trigOut2, clear
const int bitLayoutHDMI[24][8] = {
	{ 0, 1, 1388, 0, 2, 1, 1, 0 },
	{ 1, 1, 1388, 0, 2, 0, 1, 0 },
	{ 2, 1, 1388, 0, 2, 0, 1, 0 },
	{ 3, 1, 1388, 0, 2, 0, 1, 0 },
	{ 4, 1, 1388, 0, 2, 0, 1, 0 },
	{ 5, 1, 1388, 0, 2, 0, 1, 0 },
	{ 6, 1, 1388, 0, 2, 0, 1, 0 },
	{ 7, 1, 1388, 0, 2, 0, 1, 0 },
	{ 8, 1, 1388, 0, 1, 0, 1, 0 },
	{ 9, 1, 1388, 0, 1, 0, 1, 0 },
	{10, 1, 1388, 0, 1, 0, 1, 0 },
	{11, 1, 1388, 0, 1, 0, 1, 0 },
	{12, 1, 1388, 0, 1, 0, 1, 0 },
	{13, 1, 1388, 0, 1, 0, 1, 0 },
	{14, 1, 1388, 0, 1, 0, 1, 0 },
	{15, 1, 1388, 0, 1, 0, 1, 0 },
	{16, 1, 1388, 0, 4, 0, 1, 0 },
	{17, 1, 1388, 0, 4, 0, 1, 0 },
	{18, 1, 1388, 0, 4, 0, 1, 0 },
	{19, 1, 1388, 0, 4, 0, 1, 0 },
	{20, 1, 1388, 0, 4, 0, 1, 0 },
	{21, 1, 1388, 0, 4, 0, 1, 0 },
	{22, 1, 1388, 0, 4, 0, 1, 0 },
	{23, 1, 1388, 0, 4, 0, 1, 0 }
};

// Functions for direct USB communication with the PLM
namespace PLM {

	char versionStr[255];
	unsigned int API_ver, App_ver, SWConfig_ver, SeqConfig_ver;
	enum PLAYMODES {
		PLAY_ONCE_MODE = 0,
		REPEAT_MODE = 1,
	};

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

	//////////////////  Bypassing LightCrafterGUI  //////////////////

	int SetSource(unsigned int source, unsigned int portWidth) {
		// source 0 = Parallel RGB
		// portWidth 1 = 24-bits

		if (LCR_SetInputSource(source, portWidth) < 0) {
			std::cout << "Error: Unable to set Input Source" << std::endl;
			return -1;
		};
	};

	int SetPortSwap(unsigned int port, unsigned int swap) {
		// port: 0 = Port 1, 1 = Port 2
		// swap: = 0 -> ABC to ABC = No Swap
		if (LCR_SetDataChannelSwap(port, swap) < 0) {
			std::cout << "Error: Unable to set Input Port Swap" << std::endl;
		};
		return 0;
	};

	int SetConnectionType(API_VideoConnector_t connectionType) {
		if (LCR_SetIT6535PowerMode(connectionType) < 0) {
			std::cout << "Error: Unable to set IT6535 power mode" << std::endl;
			return -1;
		};
		return 0;
	};

	int SetVideoPatternMode() {

		API_DisplayMode_t SLmode = PTN_MODE_VIDEO;

		if (LCR_SetMode(SLmode) < 0) {
			std::cout << "Error: Unable to switch to video pattern mode" << std::endl;
			return -1;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for 1 second

		if (LCR_GetMode(&SLmode) == 0) {
			if (SLmode != PTN_MODE_VIDEO) {
				std::cout << "Error: Unable to switch to video pattern mode" << std::endl;
				return -1;
			}
		};

		return 0;
	};

	int UpdateLUT(int play_mode) {

		#define NUM_BIT_ELEMENTS 24
		std::vector<BitElement> bitLayout(NUM_BIT_ELEMENTS);

		for (int i = 0; i < NUM_BIT_ELEMENTS; i++) {
			bitLayout[i].splashImageBitPos = bitLayoutHDMI[i][0];
			bitLayout[i].bits = bitLayoutHDMI[i][1];
			bitLayout[i].exposure = bitLayoutHDMI[i][2];
			bitLayout[i].darkPeriod = bitLayoutHDMI[i][3];
			bitLayout[i].color = static_cast<BitElement::Color>(bitLayoutHDMI[i][4]);
			bitLayout[i].trigIn = bitLayoutHDMI[i][5] != 0; // bool
			bitLayout[i].trigOut2 = bitLayoutHDMI[i][6] != 0; // bool
			bitLayout[i].clear = bitLayoutHDMI[i][7] != 0;	// bool
			bitLayout[i].splashImageIndex = 0; 
		}

		int res;
		char errStr[255];

		if (bitLayout.size() <= 0){
			std::cout << "Error:No pattern sequence to send" << std::endl;
			return -1;
		}

		LCR_ClearPatLut();

		for (int i = 0; i < bitLayout.size(); i++){
			if (LCR_AddToPatLut(
				i, 
				bitLayout[i].exposure, 
				bitLayout[i].clear, 
				bitLayout[i].bits, 
				bitLayout[i].color, 
				bitLayout[i].trigIn, 
				bitLayout[i].darkPeriod, 
				bitLayout[i].trigOut2, 
				bitLayout[i].splashImageIndex, 
				bitLayout[i].splashImageBitPos) < 0) {
				std::cout << "Error: Unable to add pattern number " << i << " to the LUT" << std::endl;
				break;
			} else {
				std::cout << "Added pattern number " << i << " to the LUT" << std::endl;
			}
		}

		if (LCR_SendPatLut() < 0){
			std::cout << "Error: Unable to send LUT" << std::endl;
			return -1;
		}

		if (play_mode == 0) {
			res = play_mode == 0 ? LCR_SetPatternConfig(bitLayout.size(), 0) : LCR_SetPatternConfig(bitLayout.size(), bitLayout.size());
		};

		if (res < 0) {
			std::cout << "Unable to set pattern config" << std::endl;
			return -1;
		};

		return 1;

	}

	//int Initialize(int playMode) {

	//	// playMode = 0 -> Play Once
	//	// playMode = 1 -> Continuous
	//	// Set 24 bit
	//	// Set Port Swap ABC to ABC
	//	// Set IT6535 receiver
	//	// Set VideoPatternMode
	//	// Update LUT
	//	// Play

	//	const int wait_time = 500; // 1 second

	//	unsigned int source = 0; // Parallel RGB
	//	unsigned int portWidth = 24; // 24 bits
	//	SetSource(source, portWidth);

	//	std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

	//	unsigned int port = 0; // Port 1
	//	unsigned int swap = 0; // ABC -> ABC 
	//	SetPortSwap(0, 0);

	//	std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

	//	API_VideoConnector_t connectionType = VIDEO_CON_HDMI;
	//	SetConnectionType(connectionType);

	//	std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

	//	SetVideoPatternMode();

	//	std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

	//	UpdateLUT();

	//	return 0;
	//}

};
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








//int GetConnectionType() {

//	ConnectionType connectionType;

//	if (LCR_GetIT6535PowerMode(&connectionType) < 0) {
//		std::cout << "Error: Unable to get IT6535 power mode" << std::endl;
//		return -1;
//	};

//	if (connectionType == VIDEO_DISABLED) {
//		std::cout << "No video connection" << std::endl;
//		return 0;
//	};

//	if (connectionType == VIDEO_HDMI) {
//		std::cout << "HDMI connection" << std::endl;
//		return 1;
//	};

//	if (connectionType == VIDEO_DP) {
//		std::cout << "DisplayPort connection" << std::endl;
//		return 2;
//	}
//};