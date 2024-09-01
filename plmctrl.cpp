/*
 * PLMCtrl - Phase-only Light Modulator Control Library
 * Maintainer: José C. A. Rocha (email: jd964@exeter.ac.uk)
 * Version: 0.1 alpha
 * Date: 01/Sep/2024
 * Repository : https://github.com/structuredlightlab/plmctrl
 *
 * This file is part of plmctrl, an open-source library for controlling the 0.67" Texas Instruments
 * Phase-only Light Modulator (DLP6750 EVM). The library facilitates the creation, bitpacking, and
 * display of holograms on the PLM, ensuring precise frame pacing during hologram sequence display.
 *
 * Core functionalities include:
 * - Generating and bitpacking 24 holograms into a single RGB frame.
 * - Displaying holograms via HDMI or DisplayPort, with support for frame rates up to 1440 Hz.
 
 * If you use plmctrl in your research, please cite:
 * @article{joserocha,
 *     title         = {Fast and light-efficient wavefront shaping with a MEMS phase-only light modulator},
 *     author        = {J. C. A. Rocha, ...},
 *     journal       = {...},
 *     volume        = {...},
 *     number        = {...},
 *     pages         = {...},
 *     year          = {...},
 *     doi           = {...},
 * }
 *
 * External Dependencies:
 * - Dear ImGUI: GUI handling and graphics API wrapping
 * - hidapi: USB communication with the PLM
 *
 * Maintainer: José C. A. Rocha (jd964@exeter.ac.uk)
 */

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
// Windows Header Files
//#include <windows.h>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <cmath>
#include <iostream>


#include "PLM/PLM.h"
#include "plmctrl.h"


// DirectX Stuff
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

ID3D11Texture2D* pTexture = nullptr;
ID3D11ShaderResourceView* data_texture_srv = nullptr;
D3D11_TEXTURE2D_DESC desc = {};

std::mutex mutex;

bool running = false;
bool isSetupDone = false;
uint8_t* plm_image_ptr = nullptr;
std::mutex plm_image_mutex;
int N = 128, M = 128, monitor_id = 0;
int window_x0 = 0, window_y0 = 0;
int delay = 200;


// PLM Specific // THIS ONLY WORKS IN plmctrl's dev branch
char versionStr[255];
unsigned int API_ver, App_ver, SWConfig_ver, SeqConfig_ver;

enum PLM_MODE {
	PLM_IDLE = 0,
	PLM_PLAYING = 1,
	PLM_CONTINUOUS = 2
};

PLM_MODE plm_mode = PLM_IDLE;

bool plm_connected = false;
int frames_to_play = 0;
int frames_in_sequence = -1;
bool first_frame_trigger = false;
bool start_playing_trigger = false;
bool plm_is_displaying = false;
bool sequence_active = false;
bool continuous_mode = false;
int64_t frame_index = 0;
int64_t buffer_index = -1;
long long t0 = 0;

bool camera_trigger = false;
bool show_debug_window = true;

UINT msg_debug = 0;


unsigned int MAX_FRAMES = 48;

std::vector<uint8_t> frame_set;
std::vector<uint64_t> frame_order;

// TI's default lookup-table
float phases[17] = { 0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1.0 };
// Binary counting phase-map, has to be calibrated.
int phase_map[] = {
	0, 0, 0, 0,
	1, 0, 0, 0,
	0, 1, 0, 0,
	1, 1, 0, 0,
	0, 0, 1, 0,
	1, 0, 1, 0,
	0, 1, 1, 0,
	1, 1, 1, 0,
	0, 0, 0, 1,
	1, 0, 0, 1,
	0, 1, 0, 1,
	1, 1, 0, 1,
	0, 0, 1, 1,
	1, 0, 1, 1,
	0, 1, 1, 1,
	1, 1, 1, 1
};

std::thread ui_thread;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

long long GetT0() {
	return t0;
}

long int QueryBufferIndex() {
	return buffer_index;
}

int QueryPLMMode() {
	return (int)plm_mode;
}

bool QueryCameraTrigger() {
	return camera_trigger;
}

int QueryHologramInSequence() {
	return frames_in_sequence;
}

void Status(bool indicator, bool same_line = true) {
	const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoPicker |
		ImGuiColorEditFlags_NoOptions |
		ImGuiColorEditFlags_NoInputs |
		ImGuiColorEditFlags_NoTooltip |
		ImGuiColorEditFlags_NoLabel |
		ImGuiColorEditFlags_NoSidePreview |
		ImGuiColorEditFlags_NoDragDrop;
	static float green[3] = { 0.0f, 1.0f, 0.0f };
	static float red[3] = { 1.0f, 0.0f, 0.0f };
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::ColorEdit3("status##", indicator ? green : red, flags);
	//ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? green : red);
	//ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
};

void Bit(bool indicator, bool same_line = false) {
	ImU32 green = IM_COL32(0, 255, 0, 255);
	ImU32 red = IM_COL32(255, 0, 0, 255);
	ImU32 blue = IM_COL32(0, 0, 255, 255);
	ImU32 grey = IM_COL32(120, 120, 120, 255);
	ImU32 yellow = IM_COL32(255, 255, 0, 255);
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, pos.y + 2), ImVec2(pos.x + 10, pos.y + 12), indicator ? yellow : grey);
	ImGui::Text(" ");
	if (same_line) ImGui::SameLine();
}

bool Cleanup() {
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	StopUI();
	return true;
}

bool StartSequence(int number_of_frames) {

	if (number_of_frames > MAX_FRAMES) {
		return false;
	};

	frames_to_play = number_of_frames;
	frames_in_sequence = number_of_frames;
	sequence_active = true;
	first_frame_trigger = true;

	return true;
}

// Struct to hold the index and the monitor RECT together
struct MonitorData {
	int monitorIndex;
	RECT monitorRect;
};

// Function to find the second monitor
bool GetSecondMonitorRect(RECT& rect)
{
	MonitorData data = { 0, {0, 0, 0, 0} };
	bool foundSecondMonitor = false;

	EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL
		{
			MonitorData* monitorData = reinterpret_cast<MonitorData*>(dwData);
			MONITORINFO monitorInfo;
			monitorInfo.cbSize = sizeof(MONITORINFO);
			if (GetMonitorInfo(hMonitor, &monitorInfo))
			{
				if (monitorData->monitorIndex == 1)  // We're looking for the second monitor (index 1)
				{
					monitorData->monitorRect = monitorInfo.rcMonitor;
					return FALSE;  // Stop enumeration after finding the second monitor
				}
				monitorData->monitorIndex++;
			}
			return TRUE;  // Continue enumeration
		}, reinterpret_cast<LPARAM>(&data));

	if (data.monitorIndex == 1)  // Ensure that the second monitor was found
	{
		rect = data.monitorRect;
		foundSecondMonitor = true;
	}
	return foundSecondMonitor;
}

// Main code
int UI()
{

	RECT monitorRect;
	if (!GetSecondMonitorRect(monitorRect)){
		std::cerr << "Second monitor not found!" << std::endl;
		return 1;
	}

	// Create application window
	// ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowEx(
		WS_EX_TOPMOST, // dwExStyle: No extended styles
		wc.lpszClassName,
		L"plmctrl DirectX11 version",
		WS_POPUP  | WS_VISIBLE,
		monitorRect.left, monitorRect.top,
		monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOW);
	::UpdateWindow(hwnd);

	//SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
	//SetWindowPos(hwnd, HWND_TOP, 
	//	monitorRect.left, monitorRect.top,
	//	monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top, SWP_FRAMECHANGED);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigDockingWithShift = true; // Enable docking with shift key
	io.ConfigFlags& ImGuiConfigFlags_ViewportsEnable ? std::cout << "true" << std::endl : std::cout << "false" << std::endl;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable){
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Our state
	bool show_demo_window = false;
	bool show_demo_plot_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	ImGuiMouseButton LMB = ImGuiMouseButton_Left;

	static ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_AutoSelectNewTabs
		| ImGuiTabBarFlags_Reorderable
		| ImGuiTabBarFlags_FittingPolicyResizeDown;

	using timepoint = std::chrono::time_point<std::chrono::high_resolution_clock>;
	timepoint start_total;
	timepoint end_total;
	timepoint start;
	timepoint end;
	std::chrono::duration<double> elapsed_content;
	std::chrono::duration<double> elapsed_buffer;
	std::chrono::duration<double> elapsed_total;

	desc.Width = 2 * N;
	desc.Height = 2 * M;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	g_pd3dDevice->CreateTexture2D(&desc, nullptr, &pTexture);
	g_pd3dDevice->CreateShaderResourceView(pTexture, nullptr, &data_texture_srv);



	//g_pSwapChain->SetFullscreenState(TRUE, nullptr);

	//DXGI_MODE_DESC modeDesc = {};
	//modeDesc.Width = monitorRect.right - monitorRect.left;
	//modeDesc.Height = monitorRect.bottom - monitorRect.top;
	//modeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	////modeDesc.RefreshRate.Numerator = 100;
	////modeDesc.RefreshRate.Denominator = 1;
	//g_pSwapChain->ResizeTarget(&modeDesc);


	timepoint now;
	bool done = false;
	while (running && !done)
	{

		if (frames_to_play < 0 && sequence_active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			// Pause Playing the sequence // THIS ONLY WORKS IN THE plmctrl's dev branch
			// if (plm_connected)  LCR_PatternDisplay(0x1);
			plm_is_displaying = false;
			sequence_active = false;

			buffer_index = -1; // buffer_index = -1 signals that the sequence has ended
			plm_mode = PLM_IDLE;

			camera_trigger = false;

			std::cout << "Sequence ended" << std::endl;
		};


		start_total = std::chrono::high_resolution_clock::now();
		start = std::chrono::high_resolution_clock::now();

		static ImVec2 mouse_pos, image_pos, image_size;

		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			//std::cout << msg.message << std::endl;
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// Handle window being minimized or screen locked
		if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
		{
			::Sleep(10);
			continue;
		}
		g_SwapChainOccluded = false;

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}


		// Start the Dear ImGui frame
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		static bool show_demo_window = false;
		if (show_demo_window)
			ImGui::ShowDemoWindow(&show_demo_window);

		const ImGuiViewport* viewport = ImGui::GetMainViewport();

		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
		static ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;

		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		bool popen = true;
		ImGui::Begin("DockSpace Demo", &popen, window_flags);
		ImGui::PopStyleVar(2);
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		ImGui::End();

		uint64_t frame_elements = 3 * (2 * N) * (2 * M);

		if (frames_to_play == frames_in_sequence && sequence_active) {
			plm_mode = PLM_PLAYING;
			frame_index = 0;
			first_frame_trigger = false;
			start_playing_trigger = true;
		}

		plm_image_ptr = frame_set.data()
			+ frame_order[frame_index % MAX_FRAMES] * frame_elements;

		PLM::ImagescPLM("PLM", plm_image_ptr, data_texture_srv, g_pd3dDevice, g_pd3dDeviceContext, io, 2 * N, 2 * M, &mutex, window_x0, window_y0);

		ImVec2 pos = viewport->WorkPos;
		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);

		if (show_debug_window) {
			ImGui::Begin("Debug Panel DirectX");

			ImGui::SeparatorText("Warning");
			
			ImGui::Text("- This version does not support commanding the PLM to Play/Stop the sequence display");
			ImGui::Text("- To enable this feature, follow the Wiki entry on this topic");
	

			//ImGui::Text("Number of monitors detected: %d", count);
			//for (int i = 0; i < count; i++) {
			//	Status(i == monitor_id);
			//	//ImGui::Text("monitor_id %d: %s", i, glfwGetMonitorName(monitors[i]));
			//};
			//if (ImGui::Button("Reset Monitor")) {
			//	DXGI_MODE_DESC modeDesc = {};
			//	modeDesc.Width = monitorRect.right - monitorRect.left;
			//	modeDesc.Height = monitorRect.bottom - monitorRect.top;
			//	modeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			//	modeDesc.RefreshRate.Numerator = 100;
			//	modeDesc.RefreshRate.Denominator = 1;
			//	g_pSwapChain->ResizeTarget(&modeDesc);
			//};
			//if (ImGui::Button("Fullscreen")) {
			//	g_pSwapChain->SetFullscreenState(TRUE, nullptr);
			//};

			ImGui::Text("Frametime %f ms (%f FPS)", 1000.0 * io.DeltaTime, io.Framerate);
			ImGui::SeparatorText("PLM Status");
			// THIS ONLY WORKS IN THE plmctrl's dev branch
			//if (USB_IsConnected() == false)
			//{
			//	USB_Open();
			//	LCR_GetVersion(&App_ver, &API_ver, &SWConfig_ver, &SeqConfig_ver);
			//};
			Status(plm_connected);
			//plm_connected = USB_IsConnected();
			ImGui::Text("%d.%d.%d", (App_ver >> 24), ((App_ver << 8) >> 24), ((App_ver << 16) >> 16));
			plm_connected = false;
			ImGui::BeginDisabled(!plm_connected);
			Status(plm_is_displaying);
			if (ImGui::Button("Start")) {
				//LCR_PatternDisplay(0x2); // THIS ONLY WORKS IN THE plmctrl's dev branch
				plm_is_displaying = true;
			};
			ImGui::SameLine();
			if (ImGui::Button("Stop")) {
				//LCR_PatternDisplay(0x1); // THIS ONLY WORKS IN THE plmctrl's dev branch
				plm_is_displaying = false;
			};
			ImGui::EndDisabled();
			ImGui::Separator();
			Status(first_frame_trigger);
			//ImGui::SliderInt("Delay", &delay, 0, 100);
			ImGui::Text("Frames to play: %d/%d", frames_to_play, frames_in_sequence);
			ImGui::Text("Buffer Index %d/%d", buffer_index, 24*frames_in_sequence);
			ImGui::Text("Frame pointer [%p]", plm_image_ptr);
			ImGui::Text("Frame index: [%d]", frame_index % MAX_FRAMES);
			// Display frame_order array
			ImGui::Text("Frame order:");
			ImGui::SameLine();
			ImGui::Text("[");
			ImGui::SameLine();
			for (int i = 0; i < (MAX_FRAMES <= 4 ? (MAX_FRAMES - 1) : 4); ++i) {
				ImGui::Text("%llu", frame_order[i]);
				ImGui::SameLine();
			};
			ImGui::Text("... %llu], total: %d", frame_order[MAX_FRAMES - 1], MAX_FRAMES);
			ImGui::PlotLines("LUT", phases, 17);
			ImGui::Separator();
			ImGui::Text("UI Content: %f ms", elapsed_content.count() * 1000);
			ImGui::Text("Buffer Swap: %f ms", elapsed_buffer.count() * 1000);
			ImGui::Text("Total: %f ms", elapsed_total.count() * 1000);

			// Display phase_map
			if (ImGui::TreeNode("Phase map [0...15]: ")) {

				for (int j = 0; j < 4; j++) {
					for (int i = 0; i < 16; i++) {
						Bit(phase_map[i * 4 + j], i < 15);
					};
				};
				ImGui::TreePop();
			}
			ImGui::End();
		}

		// ImGui Rendering
		ImGui::Render();

		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable){
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		int display_w, display_h;

		// Present with VSync. This is the most important part for correct frame-pace
		HRESULT hr = g_pSwapChain->Present(1, 0);   
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);


		end = std::chrono::high_resolution_clock::now();
		elapsed_content = end - start;

		start = std::chrono::high_resolution_clock::now();

		buffer_index = camera_trigger ? buffer_index + 1 : -1;

		if (plm_mode == PLM_PLAYING && frames_to_play == frames_in_sequence) {
			camera_trigger = true;
			buffer_index = 0;
			// LCR_PatternDisplay(0x2);  // THIS ONLY WORKS IN THE plmctrl's dev branch
			now = std::chrono::high_resolution_clock::now();
			t0 = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
			std::cout << "FIRST FRAME ON PLM" << std::endl;
		};
		if (plm_mode == PLM_PLAYING) {
			std::cout << "Buffer Index on plmctrl: " << buffer_index << std::endl;
			now = std::chrono::high_resolution_clock::now();
			std::cout << "plmctrl time: " << std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()-t0<< " microseconds" << std::endl;
		}

		end = std::chrono::high_resolution_clock::now();
		elapsed_buffer = end - start;

		end_total = std::chrono::high_resolution_clock::now();
		elapsed_total = end_total - start_total;

		// first_frame_trigger is a variable to make sure that the UI is displaying the first 
		// hologram in the sequence before sending the PLM the trigger commands.
		if (frames_to_play >= 0 && !(first_frame_trigger)) {
			frame_index++;
			frames_to_play--;
		};

	};


	// Cleanup
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	if (running) {
		std::thread cleanup_thread(Cleanup);
		cleanup_thread.detach();
	};

	return 0;
}

void StartUI(unsigned int number_of_frames) {

	MAX_FRAMES = number_of_frames;

	if (running) {
		StopUI();
		StartUI(number_of_frames);
		return;
	};

	running = true;
	plm_image_ptr = nullptr;

	frame_set.resize(3 * (2 * N) * (2 * M) * MAX_FRAMES);
	std::fill(frame_set.begin(), frame_set.end(), 255);

	frame_order.resize(MAX_FRAMES);

	ui_thread = std::thread(UI);

	return;
}

void ResetUI() {
	running = false;
	StopUI();
	StartUI(MAX_FRAMES);
}

void StopUI() {

	isSetupDone = false;
	running = false;
	plm_image_ptr = nullptr;
	ui_thread.join();

	if (plm_connected) {
		//USB_Close(); // THIS ONLY WORKS IN THE plmctrl's dev branch
		plm_connected = false;
	};

	return;
}

void SetPLMWindowPos(int width, int height, int monitor) {
	N = width;
	M = height;
	monitor_id = monitor;
}

void SetLookupTable(double* phase) {
	for (int i = 0; i < 17; i++) {
		phases[i] = phase[i];
	}
}

bool SetPhaseMap(int* new_phase_map) {
	const int phase_map_size = 16 * 4;
	for (int i = 0; i < phase_map_size; i++) {
		phase_map[i] = new_phase_map[i];
	};
	return true;
}

bool SetFrameSequence(unsigned long long* sequence, unsigned long long length) {

	if (length > MAX_FRAMES) {
		return false;
	};

	for (int i = 0; i < length; i++) {
		frame_order[i] = sequence[i];
	};

	return true;
};

//bool PlayOnce() {
//	return false;
//};

bool InsertPLMFrame(unsigned char* frame, unsigned long long num_frames = 1, unsigned long long offset = 0) {

	if (offset + num_frames > MAX_FRAMES) {
		// Exceeds the maximum number of frames we can store
		return false;
	};

	uint64_t frame_elements = 3 * (2 * N) * (2 * M);

	for (uint64_t n = 0; n < num_frames; n++) {
		for (uint64_t i = 0; i < frame_elements; i++) {
			frame_set.at(i + (n + offset) * frame_elements) =
				frame[i + n * frame_elements];
		};
		std::cout << "Frame " << n << " inserted" << std::endl;
	};

	return true;
};

bool SetPLMFrame(unsigned long long offset = 0) {

	if (offset >= MAX_FRAMES) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	frame_index = offset;

	for (int i = 0; i < MAX_FRAMES; i++) {
		frame_order[i] = offset;
	};

	return true;
};

bool GrabPLMFrame(unsigned char* hologram, uint64_t index = 0) {

	if (index >= MAX_FRAMES) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	uint64_t frame_elements = 3 * (2 * N) * (2 * M);
	uint64_t ptr_offset = index * frame_elements;

	for (uint64_t i = 0; i < frame_elements; i++) {
		hologram[i] = frame_set.at(i + ptr_offset);
	};

	return true;
};

unsigned int QuantisePhase(double phaseVal) {
	for (int level_num = 0; level_num < 17; level_num++) {
		if ((phaseVal > phases[level_num]) && (phaseVal < phases[level_num + 1])) {
			if (fabs(phaseVal - phases[level_num]) < fabs(phaseVal - phases[level_num + 1])) {
				return level_num;
			};
			return (level_num + 1) % 16;
		}
	}
	return 0; // Default return if no condition is met
}

bool BitpackHolograms(
	double* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms
) {
	// Check if the number of holograms is within the limit
	if (num_holograms > 24) {
		return false;
	};

	uint64_t phase_elements = N * M;

	uint64_t holo = 0;
	uint64_t color_id = 0;
	uint64_t offset = 0;
	int level = 0;

	for (int n = 0; n < num_holograms; n++) {

		color_id = floor(holo % 24 / 8);
		offset = holo % 8;

		for (uint64_t j = 0; j < M; j++) {
			for (uint64_t i = 0; i < N; i++) {
				// Quantize the phase values
				level = QuantisePhase(phase[i + j * N + n * phase_elements]);
				// Encode the phase values into the hologram
				hologram[3 * (2 * i + 0) + (2 * j + 1) * (3 * 2 * N) + color_id] |= phase_map[level * 4 + 0] << offset;
				hologram[3 * (2 * i + 0) + (2 * j + 0) * (3 * 2 * N) + color_id] |= phase_map[level * 4 + 1] << offset;
				hologram[3 * (2 * i + 1) + (2 * j + 1) * (3 * 2 * N) + color_id] |= phase_map[level * 4 + 2] << offset;
				hologram[3 * (2 * i + 1) + (2 * j + 0) * (3 * 2 * N) + color_id] |= phase_map[level * 4 + 3] << offset;

			};
		};
		holo++;
	}

	return true;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//sd.BufferDesc.RefreshRate.Numerator = 60;
	//sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = FALSE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}
void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}
void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
		msg_debug = msg;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
			return 0;
		//g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
		//g_ResizeHeight = (UINT)HIWORD(lParam);
		//return 0;
		break;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	case WM_DPICHANGED:
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
		{
			//const int dpi = HIWORD(wParam);
			//printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
			const RECT* suggested_rect = (RECT*)lParam;
			::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
		}
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
