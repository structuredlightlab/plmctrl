/*
 * SLMCtrl - Spatial Light Modulator Control Library
 * Structured Light Lab
 * Version: 0.1 alpha
 * Date: 16/Sep/2024
 * Repository : https://github.com/Windier/slmctrl
 *
 * External Dependencies:
 * - Dear ImGui: GUI handling and graphics API wrapping
 * - hidapi: USB communication with the SLM
 */


 // To be defined if compiled as an executable
// #define SLM_DEBUG

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


#include "SLM/SLM.h"
#include "slmctrl.h"

#include "helpers.h"



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


bool running = false;
bool isSetupDone = false;
uint8_t* slm_image_ptr = nullptr;
std::mutex mutex;
std::mutex slm_image_mutex;
int N = 1920 / 4, M = 1080 / 4, monitor_id = 0;
int window_x0 = 0, window_y0 = 0;
int delay = 200;

enum SLM_MODE {
	SLM_IDLE = 0,
	SLM_PLAYING = 1,
	SLM_CONTINUOUS = 2
};

SLM_MODE slm_mode = SLM_IDLE;
bool slm_connected = false;
int holograms_to_play = 0;
int holograms_in_sequence = -1;
bool first_hologram_trigger = false;
bool start_playing_trigger = false;
bool slm_is_displaying = false;
bool sequence_active = false;
bool continuous_mode = false;
int64_t hologram_index = 0;
int64_t buffer_index = -1;
long long t0 = 0;

bool camera_trigger = false;
bool show_debug_window = true;

std::chrono::duration<double> elapsed_content;
std::chrono::duration<double> elapsed_buffer;
std::chrono::duration<double> elapsed_total;

unsigned int MAX_HOLOGRAMS = 48;
std::vector<uint8_t> hologram_set;
std::vector<uint64_t> hologram_order;

// TI's default lookup-table
float phases[256] = {};


std::thread ui_thread;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void DebugWindow(bool show, ImGuiIO& io);

bool Cleanup() {
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	StopUI();
	return true;
}

bool StartSequence(int number_of_holograms) {

	if (number_of_holograms > MAX_HOLOGRAMS) {
		return false;
	};

	holograms_to_play = number_of_holograms;
	holograms_in_sequence = number_of_holograms;
	sequence_active = true;
	first_hologram_trigger = true;

	return true;
}

// Main code
int UI()
{

	RECT monitorRect;
	if (!GetSecondMonitorRect(monitorRect)) {
		std::cerr << "Second monitor not found!" << std::endl;
		return 1;
	}

	// Create application window
	// ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"slmctrl", nullptr };
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowEx(
		WS_EX_TOPMOST, // dwExStyle: No extended styles
		wc.lpszClassName,
		L"slmctrl",
		WS_POPUP | WS_VISIBLE,
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
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

	// Our state
	bool show_demo_window = false;
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


	// Hologram texture (holds the bitpacked holograms)
	desc.Width = N;
	desc.Height = M;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	g_pd3dDevice->CreateTexture2D(&desc, nullptr, &pTexture);
	g_pd3dDevice->CreateShaderResourceView(pTexture, nullptr, &data_texture_srv);


	timepoint now;
	bool done = false;
	// Main UI loop. Changes the holograms with VSync enabled
	while (running && !done)
	{

		if (holograms_to_play < 0 && sequence_active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			// Pause Playing the sequence.

			slm_is_displaying = false;
			sequence_active = false;

			buffer_index = -1; // buffer_index = -1 signals that the sequence has ended
			slm_mode = SLM_IDLE;

			camera_trigger = false;

			std::cout << "Sequence finished" << std::endl;
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
		};


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

		// Start the Dear ImGui hologram
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
		ImGui::Begin("DockSpace", &popen, window_flags);
		ImGui::PopStyleVar(2);
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		ImGui::End();

		static uint64_t hologram_elements =  N * M;
		if (holograms_to_play == holograms_in_sequence && sequence_active) {
			slm_mode = SLM_PLAYING;
			hologram_index = 0;
			first_hologram_trigger = false;
			start_playing_trigger = true;
		}

		slm_image_ptr = hologram_set.data()
			+ hologram_order[hologram_index % MAX_HOLOGRAMS] * hologram_elements;

		// SLM hologram window
		//SLM::ImagescSLM("SLM", slm_image_ptr, data_texture_srv, g_pd3dDevice, g_pd3dDeviceContext, io, 2 * N, 2 * M, &mutex, window_x0, window_y0);

		ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);

		DebugWindow(show_debug_window, io);

		// ImGui Rendering
		ImGui::Render();

		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		int display_w, display_h;

		// Present with VSync. This is the most important part for correct hologram-pace
		HRESULT hr = g_pSwapChain->Present(1, 0);
		g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

		end = std::chrono::high_resolution_clock::now();
		elapsed_content = end - start;
		start = std::chrono::high_resolution_clock::now();
		buffer_index = camera_trigger ? buffer_index + 1 : -1;


		if (slm_mode == SLM_PLAYING && holograms_to_play == holograms_in_sequence) {
			camera_trigger = true;
			buffer_index = 0;
			t0 = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
			std::cout << "FIRST FRAME TRIGGER" << std::endl;
		};
		if (slm_mode == SLM_PLAYING) {
			std::cout << "Buffer Index on slmctrl: " << buffer_index << std::endl;
		}

		end = std::chrono::high_resolution_clock::now();
		elapsed_buffer = end - start;

		end_total = std::chrono::high_resolution_clock::now();
		elapsed_total = end_total - start_total;

		// first_hologram_trigger is a variable to know exactly that the first hologram was already sent to the GPU buffer queue. 
		if (holograms_to_play >= 0 && !(first_hologram_trigger)) {
			hologram_index++;
			hologram_index = clamp(hologram_index, 0, MAX_HOLOGRAMS - 1);
			holograms_to_play--;
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

void StartUI(unsigned int number_of_holograms) {

	MAX_HOLOGRAMS = number_of_holograms;

	if (running) {
		StopUI();
		StartUI(number_of_holograms);
		return;
	};

	running = true;
	slm_image_ptr = nullptr;

	hologram_set.resize(4 * (2 * N) * (2 * M) * MAX_HOLOGRAMS);
	std::fill(hologram_set.begin(), hologram_set.end(), 255);

	hologram_order.resize(MAX_HOLOGRAMS);

#ifndef SLM_DEBUG
	std::cout << "Starting UI thread" << std::endl;
	ui_thread = std::thread(UI);
#else
	UI();
#endif

	return;
}

void ResetUI() {
	running = false;
	StopUI();
	StartUI(MAX_HOLOGRAMS);
}

void StopUI() {

	isSetupDone = false;
	running = false;
	slm_image_ptr = nullptr;
	ui_thread.join();

	if (slm_connected) {
		//USB_Close(); // THIS ONLY WORKS IN THE slmctrl's dev branch
		slm_connected = false;
	};

	return;
}

void SetSLMWindowPos(int width, int height, int monitor) {
	N = width;
	M = height;
	monitor_id = monitor;
}

void SetLookupTable(double* phase) {
	for (int i = 0; i < 17; i++) {
		phases[i] = phase[i];
	}
}

bool SetHologramSequence(unsigned long long* sequence, unsigned long long length) {

	if (length > MAX_HOLOGRAMS) {
		return false;
	};

	for (int i = 0; i < length; i++) {
		hologram_order[i] = sequence[i];
	};

	return true;
};

bool InsertSLMHologram(unsigned char* hologram, unsigned long long num_holograms = 1, unsigned long long offset = 0) {


	if (offset + num_holograms > MAX_HOLOGRAMS) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	std::cout << "Inserting " << num_holograms << " holograms at offset " << offset << std::endl;

	uint64_t hologram_elements = N * M;
	uint64_t total_elements = num_holograms * hologram_elements;
	int k = 0;

	hologram_set.insert(
		hologram_set.begin() + offset * hologram_elements,
		hologram,
		hologram + total_elements
	);

	std::cout << num_holograms << " holograms inserted" << std::endl;

	return true;
};

bool SetSLMHologram(unsigned long long offset = 0) {

	if (offset >= MAX_HOLOGRAMS) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	hologram_index = offset;

	for (int i = 0; i < MAX_HOLOGRAMS; i++) {
		hologram_order[i] = offset;
	};

	return true;
};

bool GrabSLMHologram(unsigned char* hologram, uint64_t index = 0) {

	if (index >= MAX_HOLOGRAMS) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	uint64_t hologram_elements = 4 * (2 * N) * (2 * M);
	uint64_t ptr_offset = index * hologram_elements;

	for (uint64_t i = 0; i < hologram_elements; i++) {
		hologram[i] = hologram_set.at(i + ptr_offset);
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

bool CreateHolograms(
	double* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms
) {
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
				hologram[i + j * N] = level;
			};
		};
		holo++;
	}

	return true;
};


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

	printf("D3D11CreateDeviceAndSwapChain: x%8x\n", res);
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
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
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

void DebugWindow(
	bool show,
	ImGuiIO& io
) {

	if (!show) return;

	ImGui::Begin("SLM Debug Panel");
	ImGui::Text("Frametime %f ms (%f Hz)", 1000.0 * io.DeltaTime, io.Framerate);
	ImGui::Text("Framerate needs to match SLM's");


#ifndef INCLUDE_LIGHTCRAFTER_WRAPPERS
	ImGui::SeparatorText("Warning");
	ImGui::Text("- This version does not support commanding the SLM to Play/Stop the sequence display");
	ImGui::Text("- To enable this feature, follow the Wiki entry on this topic");
	ImGui::Separator();
#else
	ImGui::SeparatorText("SLM Status");
	if (SLM::IsConnected() == false) {
		SLM::Open();
		SLM::GetVersion();
	};
	Status(slm_connected);
	slm_connected = SLM::IsConnected();
	ImGui::Text("%d.%d.%d", (SLM::App_ver >> 24), ((SLM::App_ver << 8) >> 24), ((SLM::App_ver << 16) >> 16));
	ImGui::BeginDisabled(!slm_connected);
	Status(slm_is_displaying);
	if (ImGui::Button("Start")) {
		SLM::Play();
		slm_is_displaying = true;
	};
	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		SLM::Stop();
		slm_is_displaying = false;
	};
	ImGui::EndDisabled();
	ImGui::Separator();
#endif


	//Status(first_hologram_trigger);
	ImGui::Text("Holograms to play: %d/%d", holograms_to_play, holograms_in_sequence);
	ImGui::Text("Buffer Index %d/%d", 24 * buffer_index, 24 * holograms_in_sequence);
	ImGui::Text("Hologram pointer [%p]", slm_image_ptr);
	ImGui::Text("Hologram index: [%d], Hologram [%d]", hologram_index, hologram_order[hologram_index % MAX_HOLOGRAMS]);
	ImGui::SameLine();
	if (ImGui::ArrowButton("##left", ImGuiDir_Left)) { hologram_index--; hologram_index = clamp(hologram_index, 0, MAX_HOLOGRAMS - 1); }
	ImGui::SameLine();
	if (ImGui::ArrowButton("##right", ImGuiDir_Right)) { hologram_index++; hologram_index = clamp(hologram_index, 0, MAX_HOLOGRAMS - 1); }
	// Display hologram_order array
	ImGui::Text("Hologram order:");
	ImGui::SameLine();
	ImGui::Text("[");
	ImGui::SameLine();
	for (int i = 0; i < (MAX_HOLOGRAMS <= 4 ? (MAX_HOLOGRAMS - 1) : 4); ++i) {
		ImGui::Text("%llu", hologram_order[i]);
		ImGui::SameLine();
	};
	ImGui::Text("... %llu], total: %d", hologram_order[MAX_HOLOGRAMS - 1], MAX_HOLOGRAMS);
	ImGui::PlotLines("LUT", phases, 17);
	ImGui::Separator();
	ImGui::Text("UI Content: %f ms", elapsed_content.count() * 1000);
	ImGui::Text("Buffer Swap: %f ms", elapsed_buffer.count() * 1000);
	ImGui::Text("Total: %f ms", elapsed_total.count() * 1000);


	ImGui::End();
};

#ifdef SLM_DEBUG
int main() {

	StartUI(48);
	return 0;
}
#endif