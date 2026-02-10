#pragma once

#define INCLUDE_LIGHTCRAFTER_WRAPPERS

#ifdef INCLUDE_LIGHTCRAFTER_WRAPPERS

#include <PLM/API.h>
#include "PLM/usb.h"

#define clamp(x, min, max) (x < min ? min : (x > max ? max : x))


struct BitElement
{
	enum Color{
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

// They follow this order: splashImageBitPos, Bits, exposure, dark period, color, trigIn, trigOut2, clear
const int bit_layout_default[24][8] = {
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

	unsigned char HWStatus = 0;
	unsigned char SysStatus = 0;
	unsigned char MainStatus = 0;

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
			return -1;
		};
		return 0;
	};

	int SetPortConfig(unsigned int data_port, unsigned int pixel_clock, unsigned int data_enable, unsigned int sync_select) {
		// HDMI: data_port = 0, pixel_clock = 0, data_enable = 0, sync_select = 0 -> Single Pixel (30 Hz)
		// DP: data_port = 2, pixel_clock = 0, data_enable = 0, sync_select = 0 -> Dual Pixel (60 Hz)
		if (LCR_SetPortConfig(data_port, pixel_clock, data_enable, sync_select) < 0) {
			std::cout << "Error: Unable to set Port Config" << std::endl;
			return -1;
		};
	}

	int SetConnectionType(int connection_type) {

		API_VideoConnector_t connectionType = VIDEO_CON_DISABLE;

		switch (connection_type) {
			case 0:
				connectionType = VIDEO_CON_DISABLE;
				break;
			case 1:
				connectionType = VIDEO_CON_HDMI;
				break;
			case 2:
				connectionType = VIDEO_CON_DP;
				break;
			default:
				return -1;
		};

		if (LCR_SetIT6535PowerMode(connectionType) < 0) {
			std::cout << "Error: Unable to set IT6535 power mode" << std::endl;
			return -1;
		};

		return 0;
	};

	int GetConnectionType() {

		static API_VideoConnector_t powerMode;
		if (LCR_GetIT6535PowerMode(&powerMode) < 0) {
			std::cout << "Error: Unable to get IT6535 power mode" << std::endl;
			return -1;
		};
		if (powerMode == VIDEO_CON_DISABLE) {
			return 0;
		};
		if (powerMode == VIDEO_CON_HDMI) {
			return 1;
		};
		if (powerMode == VIDEO_CON_DP) {
			return 2;
		};


	};

	int SetVideoPatternMode() {

		API_DisplayMode_t SLmode = PTN_MODE_VIDEO;

		if (LCR_SetMode(SLmode) < 0) {
			std::cout << "Error: Unable to switch to video pattern mode" << std::endl;
			return -1;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for 0.5 second

		if (LCR_GetMode(&SLmode) == 0) {
			if (SLmode != PTN_MODE_VIDEO) {
				std::cout << "Error: Unable to switch to video pattern mode" << std::endl;
				return -1;
			}
		};

		return 0;
	};

	int GetVideoPatternMode() {
		API_DisplayMode_t SLmode = PTN_MODE_DISABLE;
		if (LCR_GetMode(&SLmode) == 0) {
			if (SLmode != PTN_MODE_VIDEO) {
				return -1;
			}
		};
		return 0;
	};

	int UpdateLUT(int play_mode, int connection_type) {
		// play_mode = 0 -> Play Once
		// play_mode = 1 -> Continuous (Repeat mode)
		// connection_type = 1 -> HDMI
		// connection_type = 2 -> DisplayPort

		PLM::Stop();

		const int NUM_BIT_ELEMENTS = 24;
		std::vector<BitElement> bit_layout(NUM_BIT_ELEMENTS);

		if (connection_type != 1 && connection_type != 2) {
			std::cout << "Error: Invalid connection type" << std::endl;
			return -1;
		};

		unsigned int exposure = connection_type == 1 ? 1388 : 694; // 1388 microseconds for HDMI, 694 microseconds for DisplayPort

		for (int i = 0; i < NUM_BIT_ELEMENTS; i++) {
			bit_layout[i].splashImageBitPos = bit_layout_default[i][0];
			bit_layout[i].bits = bit_layout_default[i][1];
			bit_layout[i].exposure = exposure;
			bit_layout[i].darkPeriod = bit_layout_default[i][3];
			bit_layout[i].color = static_cast<BitElement::Color>(bit_layout_default[i][4]);
			bit_layout[i].trigIn = bit_layout_default[i][5] != 0; // bool
			bit_layout[i].trigOut2 = bit_layout_default[i][6] != 0; // bool
			bit_layout[i].clear = bit_layout_default[i][7] != 0;	// bool
			bit_layout[i].splashImageIndex = 0; 
		}

		int res = 0;
		char errStr[255];

		if (bit_layout.size() <= 0){
			std::cout << "Error:No pattern sequence to send" << std::endl;
			return -1;
		}

		LCR_ClearPatLut();

		for (int i = 0; i < bit_layout.size(); i++){
			if (LCR_AddToPatLut(
				i, 
				bit_layout[i].exposure, 
				bit_layout[i].clear, 
				bit_layout[i].bits, 
				bit_layout[i].color, 
				bit_layout[i].trigIn, 
				bit_layout[i].darkPeriod, 
				bit_layout[i].trigOut2, 
				bit_layout[i].splashImageIndex, 
				bit_layout[i].splashImageBitPos) < 0) {
				std::cout << "Error: Unable to add pattern number " << i << " to the LUT" << std::endl;
				return -1;
				break;
			} else {
				std::cout << "Added pattern number " << i << " to the LUT" << std::endl;
			}
		}

		if (LCR_SendPatLut() < 0){
			std::cout << "Error: Unable to send LUT" << std::endl;
			return -1;
		}

		res = play_mode == 1 ? LCR_SetPatternConfig(bit_layout.size(), 0) : LCR_SetPatternConfig(bit_layout.size(), bit_layout.size());

		if (res < 0) {
			std::cout << "Unable to set pattern config" << std::endl;
			return -1;
		};

		return 0;

	}

	int Configure(int play_mode, int connection_type ) {

		// play_mode = 0 -> Play Once
		// play_mode = 1 -> Continuous (Repeat mode)
		// connection_type = 0 -> HDMI
		// connection_type = 1 -> DisplayPort

		// Tasks :
		//  -- Set 24 bit
		// -- Set Port Swap ABC to ABC
		// -- Set IT6535 receiver
		// -- Set VideoPatternMode
		// -- Update LUT
		// -- Play

		const unsigned int wait_time = 1500; // 1 second

		unsigned int source = 0; // Parallel RGB
		unsigned int portWidth = 1; // 24 bits
		if (SetSource(source, portWidth) < 1) return -1;

		std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

		unsigned int port = 0; // Port 1
		unsigned int swap = 0; // ABC -> ABC 
		if (SetPortSwap(0, 0) < 0) return -1;

		std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

		if (SetConnectionType(connection_type) < 0) return -1;

		std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

		if (SetVideoPatternMode() < 0) return -1;

		std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));

		UpdateLUT(play_mode, connection_type);

		return 0;
	}

};
#else
	namespace PLM {
		int Play() { return -1; };
		int Stop() { return -1; };
		int Open() { return -1; };
		int Close() { return -1; };
		int SetSource(unsigned int source, unsigned int port_width) { return -1; };
		int SetPortSwap(unsigned int port, unsigned int swap) { return -1; };
		int SetConnectionType(int connection_type) { return -1; };
		int SetVideoPatternMode() { return -1; };
		int UpdateLUT(int play_mode, int connection_type) { return -1; };
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
				(uint64_t)N * 4 * sizeof(uint8_t));
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


	namespace Bezier {

		enum LUT_TYPE {
			LUT_TI = 0,
			LUT_BEZIER = 1,
			LUT_BINARY = 2
		};


		double lerp(double t, double a, double b) {
			return (1 - t) * a + t * b;
		};

		ImPlotPoint bezier(double t, ImPlotPoint* P) {
			ImPlotPoint Q[] = { P[0], P[1], P[2], P[3], P[4], P[5] };
			int n = 6;
			ImPlotPoint B;
			for (int i = n - 2; i >= 0; i--) { // 4, 3, 2, 1, 0
				for (int j = 0; j <= i; j++) { // {0, 1, 2, 3, 4}, {0, 1, 2, 3}, {0, 1, 2}, {0, 1}, {0}
					Q[j].y = lerp(t, Q[j].y, Q[j + 1].y);
				}
			}
			Q[0].x = lerp(t, P[0].x, P[5].x);
			return Q[0];
		};


		const int num_points = 512;

		LUT_TYPE lut_type = LUT_BEZIER;

		bool has_changed_lut = true;
		// This is good for the lowest Mirror Bias level tested so far (level 10/255)
		//ImPlotPoint P[] = { ImPlotPoint(0.0f,0.0f), ImPlotPoint(0.2f,0.8242f), ImPlotPoint(0.4f,0.3073f), ImPlotPoint(0.6f,0.9116f), ImPlotPoint(0.8f,0.771f),ImPlotPoint(1.0f,1.0f) }; // PLM 1 Curve
		ImPlotPoint P[] = { ImPlotPoint(0.0f,0.0f), ImPlotPoint(0.2f,0.1f), ImPlotPoint(0.4f,0.035f), ImPlotPoint(0.6f,0.485f), ImPlotPoint(0.8f,0.3867f),ImPlotPoint(1.0f,1.0f) };

		ImPlotPoint P_binary[] = { ImPlotPoint(0.25f,0.0f) , ImPlotPoint(0.75f,1.0f) };


		std::vector<double> LUTi(num_points, 0.0);
		std::vector<double> LUTj(num_points, 0.0);
		ImPlotPoint LUT[num_points];

		std::vector<float> LUT_control_points_x = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
		std::vector<float> LUT_control_points_y = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };

		//float phase_level[] = { 0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1 };
		//     0.0148 0.0285 0.0450 0.0924 0.1558 0.2719 0.3001 0.3546 0.4244 0.5187 0.5828 0.7195 0.8603 0.9722 0.9818 0.9940
		float phase_level[] = { 0.0, 0.0148, 0.0285, 0.0450, 0.0924, 0.1558, 0.2719, 0.3001, 0.3546, 0.4244, 0.5187, 0.5828, 0.7195, 0.8603, 0.9722, 0.9818, 1.0 };
		int num_levels = 16;
		ImVec4 plot_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);


		bool Controls() {
			has_changed_lut = false;
			static ImPlotDragToolFlags flags = ImPlotDragToolFlags_None;
			//ImGui::CheckboxFlags("NoCursors", (unsigned int*)&flags, ImPlotDragToolFlags_NoCursors); ImGui::SameLine();
			//ImGui::CheckboxFlags("NoFit", (unsigned int*)&flags, ImPlotDragToolFlags_NoFit); ImGui::SameLine();
			//ImGui::CheckboxFlags("NoInput", (unsigned int*)&flags, ImPlotDragToolFlags_NoInputs);
			//if (ImGui::Combo("LUT Type", (int*)&lut_type, "TI\0Bezier\0\0")) {
			//	has_changed_lut = true;
			//};
			lut_type = LUT_BEZIER;

			//ImPlotAxisFlags ax_flags = ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks;
			ImPlotAxisFlags ax_flags = ImPlotFlags_None;
			bool clicked[6] = { false, false, false, false, false, false };
			bool hovered[6] = { false, false, false, false, false, false };
			bool held[6] = { false, false, false, false, false, false };

			if (ImPlot::BeginPlot("##Bezier", ImVec2(400, 400), ImPlotFlags_CanvasOnly)) {
				ImPlot::SetupAxes(nullptr, nullptr, ax_flags, ax_flags);
				ImPlot::SetupAxesLimits(0, 1, 0, 1);

				switch (lut_type) {
				case LUT_TI:
					break;
				case LUT_BEZIER:
					ImPlot::DragPoint(0, &P[0].x, &P[0].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[0], &hovered[0], &held[0]);
					ImPlot::DragPoint(1, &P[1].x, &P[1].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[1], &hovered[1], &held[1]);
					ImPlot::DragPoint(2, &P[2].x, &P[2].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[2], &hovered[2], &held[2]);
					ImPlot::DragPoint(3, &P[3].x, &P[3].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[3], &hovered[3], &held[3]);
					ImPlot::DragPoint(4, &P[4].x, &P[4].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[4], &hovered[4], &held[4]);
					ImPlot::DragPoint(5, &P[5].x, &P[5].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[5], &hovered[5], &held[5]);
					break;
				case LUT_BINARY:
					ImPlot::DragPoint(5, &P_binary[0].x, &P_binary[0].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[0], &hovered[0], &held[0]);
					ImPlot::DragPoint(6, &P_binary[1].x, &P_binary[1].y, ImVec4(0.7, 0.7, 0.7, 1), 6, flags, &clicked[1], &hovered[1], &held[1]);
					break;
				};

				const float vline_x[6][2] = { {0.0f, 0.0f},{0.2f, 0.2f}, {0.4f, 0.4f}, {0.6f, 0.6f}, {0.8f, 0.8f}, {1.0f, 1.0f} };
				const float vline_y[6][2] = { {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} };

				char label[5] = "##v0";
				for (int i = 0; i < 6; ++i) {
					label[3] = '0' + i;
					ImPlot::SetNextLineStyle(held[i] ? ImVec4(1.0, 1.0f, 1.0f, 0.8f) : ImVec4(1.0, 1.0f, 1.0f, 0.4f), held[i] ? 3.0f : 1.0f);
					ImPlot::PlotLine(label, vline_x[i], vline_y[i], 2, 0, 2 * sizeof(float));
				}

				has_changed_lut |= held[0] || held[1] || held[2] || held[3] || held[4] || held[5];

				for (int i = 0; i < num_points; ++i) {
					double t = i / (float)(num_points - 1);
					LUT[i] = bezier(t, P);

					LUT[i].x = LUT[i].x;
					LUT[i].y = clamp(round(15.0 * LUT[i].y) / 15.0, 0.0, 1.0);

					if (lut_type == LUT_BEZIER) {
						plot_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
					};

					if (lut_type == LUT_BINARY) {
						LUT[i].y = t > 0.5 ? P_binary[1].y : P_binary[0].y;
						LUT[i].y = clamp(round(15.0 * LUT[i].y) / 15.0, 0.0, 1.0);
						plot_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
					};

					static int level = 0;
					if (lut_type == LUT_TI) {
						plot_color = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
						for (int level_num = 0; level_num < 16; level_num++) {
							if ((t >= phase_level[level_num]) && (t <= phase_level[level_num + 1])) {
								if (std::abs(t - phase_level[level_num]) < std::abs(t - phase_level[level_num + 1])) {
									level = level_num;
								}
								else {
									level = level_num + 1;
								};
							};
						};

						if (t > phase_level[num_levels]) {
							if (std::abs(t - phase_level[num_levels]) < std::abs(t - 1)) {
								level = num_levels;
							}
							else {
								level = 1;
							};
						};
						LUT[i].y = level / 16.0;
					};

					LUTi[i] = LUT[i].x;
					LUTj[i] = LUT[i].y;

				};

				ImPlot::SetNextLineStyle(ImVec4(0, 0.9f, 0, 1), held[0] || held[1] || held[2] || held[3] || held[4] || held[5] ? 4.0f : 2.0f);
				ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 4, plot_color, 1, plot_color);
				ImPlot::PlotScatter("##bez", &LUT[0].x, &LUT[0].y, num_points, 0, 0, sizeof(ImPlotPoint));
				ImPlot::EndPlot();
			};

			// Store control points for saving it.
			for (int i = 0; i < 6; ++i) {
				LUT_control_points_x[i] = P[i].x;
				LUT_control_points_y[i] = P[i].y;
			};

			return has_changed_lut;

		}

	}
}

