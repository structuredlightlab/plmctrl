/*
 * PLMCtrl - Phase-only Light Modulator Control Library
 * Structured Light Lab
 * Version: 1.0.0
 * Date: 25/June/2026
 * Repository : https://github.com/structuredlightlab/plmctrl
 *
 * plmctrl is an open-source library for controlling the 0.67" Texas Instruments
 * Phase-only Light Modulator (DLP6750 EVM), VIS and NIR versions. The library facilitates the creation, bitpacking, and
 * display of holograms on the PLM, ensuring precise frame pacing during hologram sequence display.

 * If you use plmctrl in your research, please cite:
 * 
	@article{rocha2024plm,
		author = {Jos\'{e} C. A. Rocha and Terry Wright and Un\.{e} G. B\={u}tait\.{e} and Joel Carpenter and George S. D. Gordon and David B. Phillips},
		journal = {Opt. Express},
		number = {24},
		pages = {43300--43314},
		publisher = {Optica Publishing Group},
		title = {Fast and light-efficient wavefront shaping with a MEMS phase-only light modulator},
		volume = {32},
		month = {Nov},
		year = {2024},
		url = {https://opg.optica.org/oe/abstract.cfm?URI=oe-32-24-43300}
	}

 *
 * External Dependencies:
 * - DirectX 11: Graphics API for rendering
 * - Dear ImGui: GUI handling and graphics API wrapping
 * - hidapi: USB communication with the PLM
 */


// To be defined if compiled as an executable
//#define PLM_DEBUG

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
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
#include <cstring>

#include "PLM/PLM.h"
#include "plmctrl.h"

#include "helpers.h"
#include "shaders_embedded.h"

// DirectX Stuff
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
ID3D11Texture2D* pTexture = nullptr;
ID3D11ShaderResourceView* data_texture_srv = nullptr;
ID3D11Texture2D* pPhaseTexture = nullptr;
ID3D11ShaderResourceView* phase_texture_srv = nullptr;
D3D11_TEXTURE2D_DESC desc = {};

// Bitpack Compute Shader declarations
static ID3D11ComputeShader* g_pComputeShader = nullptr;     // VIS
static ID3D11ComputeShader* g_pComputeShaderNIR = nullptr;  // NIR
static ID3D11ComputeShader* g_pComputeShaderUnpack = nullptr;     // VIS unpack
static ID3D11ComputeShader* g_pComputeShaderUnpackNIR = nullptr;  // NIR unpack
static ID3D11Buffer* g_pConstantBuffer = nullptr;
static ID3D11Buffer* g_pPhaseBuffer = nullptr;
static ID3D11Buffer* g_pLUTBuffer = nullptr;       // phase LUT: 17 floats for VIS, 64 floats (odd+even/shared) for NIR
static ID3D11Buffer* g_pPhaseMapBuffer = nullptr;

// Unpack pipeline: input bitpacked frame, output phase float buffer + readback,
// per-mode 16/64-int inverse-map buffers (rebuilt from the forward map every call).
static ID3D11Texture2D*           g_pUnpackInputTex      = nullptr;
static ID3D11ShaderResourceView*  g_pUnpackInputSRV      = nullptr;
static ID3D11Buffer*              g_pUnpackOutBuffer     = nullptr;
static ID3D11UnorderedAccessView* g_pUnpackOutUAV        = nullptr;
static ID3D11Buffer*              g_pUnpackOutStaging    = nullptr;
static ID3D11Buffer*              g_pInverseMapVISBuf    = nullptr;
static ID3D11ShaderResourceView*  g_pInverseMapVISSRV    = nullptr;
static ID3D11Buffer*              g_pInverseMapNIRBuf    = nullptr;
static ID3D11ShaderResourceView*  g_pInverseMapNIRSRV    = nullptr;

static ID3D11ShaderResourceView* g_pPhaseSRV = nullptr;
static ID3D11ShaderResourceView* g_pLUTSRV = nullptr;
static ID3D11ShaderResourceView* g_pPhaseMapSRV = nullptr;

static ID3D11Buffer* g_pHologramBuffer = nullptr;
static ID3D11UnorderedAccessView* g_pHologramUAV = nullptr;
ID3D11Texture2D* pHologramTexture = nullptr;
ID3D11Texture2D* pStagingTexture;
static ID3D11SamplerState* g_pSamplerNearest = nullptr;


// 16 bytes
struct c_Params {
	uint32_t N;
	uint32_t M;
	uint32_t num_holograms;
	uint32_t phase_stride;   // N*M for distinct phases per hologram, 0 for shared phase
};


bool running = false;
bool isSetupDone = false;
uint8_t* plm_image_ptr = nullptr;

std::mutex mutex;
std::mutex plm_image_mutex;
std::mutex plm_reading_status;

int N = 200, M = 200, monitor_id = 0;

int window_x0 = 0, window_y0 = 0;
int delay = 200;

enum PLM_MODE {
	PLM_IDLE = 0,
	PLM_PLAYING = 1,
	PLM_CONTINUOUS = 2
};

enum PLM_TYPE {
	VIS,
	NIR
};

PLM_TYPE plm_type = VIS;
NIRVariant nir_variant = NIR_ALPHA;
inline bool IsNIRMode() {
	return plm_type == NIR;
}
inline uint64_t ActiveHologramWidthPx() {
	return IsNIRMode() ? (3ULL * (uint64_t)N + 4ULL) : (2ULL * (uint64_t)N);
}
inline uint64_t ActiveHologramHeightPx() {
	return 2ULL * (uint64_t)M;
}

PLM_MODE plm_mode = PLM_IDLE;

bool plm_connected = false;
bool plm_monitoring_status = false;
bool first_frame_trigger = false;
bool start_playing_trigger = false;
bool plm_is_displaying = false;
bool sequence_active = false;
bool displaying_active = false;
bool continuous_mode = false;
std::atomic<bool> pause_UI = false;
std::atomic<bool> bitpacking_in_progress = false;
std::atomic<bool> UI_is_rendering = false;
std::atomic<bool> same_thread = false;

// Bumped each time InsertPLMFrame writes into frame_set; used by the Phase
// preview to invalidate its unpack cache when the bitpacked data changes.
std::atomic<uint64_t> frame_set_writes = 0;

int frames_to_play = 0;
int frames_in_sequence = -1;
int64_t frame_index = 0;
int64_t buffer_index = -1;
long long t0 = 0;

bool camera_trigger = false;
bool show_debug_window = true;

RECT monitorRect;

std::chrono::duration<double> elapsed_content;
std::chrono::duration<double> elapsed_buffer;
std::chrono::duration<double> elapsed_total;

uint64_t MAX_FRAMES = 64;
bool windowed = false;
std::vector<unsigned char> frame;
std::vector<uint8_t> frame_set;
std::vector<uint64_t> frame_order;
std::vector<float> phase_set;

std::mutex dx_mutex;

const float vis_phases[17] = { 0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1.0 };

// Empirical phase levels for NIR PLM (typical values, normalised to [0,1])
// Odd columns (1-indexed):  phase states 1-32 → indices 0-31
const float nir_phases_odd[32] = {
	0.0000f, 0.0127f, 0.0293f, 0.0662f, 0.0522f, 0.0675f, 0.0854f, 0.1261f,
	0.1427f, 0.1834f, 0.2166f, 0.2803f, 0.2561f, 0.2968f, 0.3299f, 0.3962f,
	0.3771f, 0.4102f, 0.4510f, 0.5108f, 0.4930f, 0.5261f, 0.5682f, 0.6306f,
	0.6127f, 0.6675f, 0.7338f, 0.8229f, 0.7847f, 0.8369f, 0.9045f, 1.0000f
};
// Even columns (1-indexed): phase states 1-32 → indices 0-31
const float nir_phases_even[32] = {
	0.0064f, 0.0153f, 0.0280f, 0.0599f, 0.0611f, 0.0726f, 0.0866f, 0.1223f,
	0.1414f, 0.1771f, 0.2076f, 0.2675f, 0.2573f, 0.2930f, 0.3236f, 0.3847f,
	0.3911f, 0.4178f, 0.4586f, 0.5121f, 0.5134f, 0.5414f, 0.5809f, 0.6357f,
	0.6255f, 0.6713f, 0.7350f, 0.8127f, 0.8038f, 0.8446f, 0.9057f, 0.9822f
};

// NIR Gamma phase levels: one LUT shared by odd/even columns.
const float nir_phases_gamma[32] = {
	0.0000f, 0.0161f, 0.0337f, 0.0718f, 0.0528f, 0.0718f, 0.0909f, 0.1334f,
	0.1452f, 0.1877f, 0.2243f, 0.2947f, 0.2595f, 0.3050f, 0.3416f, 0.4120f,
	0.3959f, 0.4311f, 0.4765f, 0.5411f, 0.5205f, 0.5572f, 0.6041f, 0.6701f,
	0.6276f, 0.6833f, 0.7522f, 0.8402f, 0.7991f, 0.8519f, 0.9223f, 1.0000f
};

inline const float* ActiveNIRPhasesOdd() {
	return (nir_variant == NIR_GAMMA) ? nir_phases_gamma : nir_phases_odd;
}

inline const float* ActiveNIRPhasesEven() {
	return (nir_variant == NIR_GAMMA) ? nir_phases_gamma : nir_phases_even;
}

inline const char* ActiveNIRVariantLabel() {
	return (nir_variant == NIR_GAMMA) ? ".67 NIR gamma" : ".67 NIR alpha";
}

static void UploadActiveNIRLUT() {
	D3D11_BOX box;
	box = { 0, 0, 0, (UINT)(sizeof(float) * 32), 1, 1 };
	g_pd3dDeviceContext->UpdateSubresource(g_pLUTBuffer, 0, &box, ActiveNIRPhasesOdd(), 0, 0);
	box = { (UINT)(sizeof(float) * 32), 0, 0, (UINT)(sizeof(float) * 64), 1, 1 };
	g_pd3dDeviceContext->UpdateSubresource(g_pLUTBuffer, 0, &box, ActiveNIRPhasesEven(), 0, 0);
}

// NIR Alpha phase map: 32 levels x 6 cells
// Cell order: k=0 bottom-left, k=1 top-left, k=2 bottom-mid, k=3 top-mid, k=4 bottom-right, k=5 top-right
// For level l: k0=bit4(l), k1=bit2(l), k2=bit0(l), k3=bit1(l), k4=bit2(l), k5=bit3(l)
int nir_phase_map[192] = {
	0,0,0,0,0,0,  // l=0  00000
	0,0,1,0,0,0,  // l=1  00001
	0,0,0,1,0,0,  // l=2  00010
	0,0,1,1,0,0,  // l=3  00011
	0,1,0,0,1,0,  // l=4  00100
	0,1,1,0,1,0,  // l=5  00101
	0,1,0,1,1,0,  // l=6  00110
	0,1,1,1,1,0,  // l=7  00111
	0,0,0,0,0,1,  // l=8  01000
	0,0,1,0,0,1,  // l=9  01001
	0,0,0,1,0,1,  // l=10 01010
	0,0,1,1,0,1,  // l=11 01011
	0,1,0,0,1,1,  // l=12 01100
	0,1,1,0,1,1,  // l=13 01101
	0,1,0,1,1,1,  // l=14 01110
	0,1,1,1,1,1,  // l=15 01111
	1,0,0,0,0,0,  // l=16 10000
	1,0,1,0,0,0,  // l=17 10001
	1,0,0,1,0,0,  // l=18 10010
	1,0,1,1,0,0,  // l=19 10011
	1,1,0,0,1,0,  // l=20 10100
	1,1,1,0,1,0,  // l=21 10101
	1,1,0,1,1,0,  // l=22 10110
	1,1,1,1,1,0,  // l=23 10111
	1,0,0,0,0,1,  // l=24 11000
	1,0,1,0,0,1,  // l=25 11001
	1,0,0,1,0,1,  // l=26 11010
	1,0,1,1,0,1,  // l=27 11011
	1,1,0,0,1,1,  // l=28 11100
	1,1,1,0,1,1,  // l=29 11101
	1,1,0,1,1,1,  // l=30 11110
	1,1,1,1,1,1,  // l=31 11111
};

// VIS phase map: 16 levels x 4 cells
// TI DLP6750 VIS
// Default VIS phase map: 16 levels x 4 cells. This is the natural binary
// phase map reindexed by phase_map_order = (12, 8, 4, 14, 0, 6, 10, 2, 13, 5,
// 9, 1, 15, 7, 11, 3), so the correct ordering is already in place even if the
// user never calls SetPhaseMap. (base row = the unordered binary code per level.)
int vis_phase_map[64] = {
	0,0,1,1,  // l=0  (base row 12)
	0,0,0,1,  // l=1  (base row 8)
	0,0,1,0,  // l=2  (base row 4)
	0,1,1,1,  // l=3  (base row 14)
	0,0,0,0,  // l=4  (base row 0)
	0,1,1,0,  // l=5  (base row 6)
	0,1,0,1,  // l=6  (base row 10)
	0,1,0,0,  // l=7  (base row 2)
	1,0,1,1,  // l=8  (base row 13)
	1,0,1,0,  // l=9  (base row 5)
	1,0,0,1,  // l=10 (base row 9)
	1,0,0,0,  // l=11 (base row 1)
	1,1,1,1,  // l=12 (base row 15)
	1,1,1,0,  // l=13 (base row 7)
	1,1,0,1,  // l=14 (base row 11)
	1,1,0,0,  // l=15 (base row 3)
};


inline int* ActivePhaseMap()     { return IsNIRMode() ? nir_phase_map : vis_phase_map; }
inline int  ActivePhaseMapSize() { return IsNIRMode() ? 192 : 64; }

std::thread ui_thread;
std::thread plm_status_thread;


// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void DebugWindow(bool show, ImGuiIO& io);


// The shader sources are embedded directly in the binary (see shaders_embedded.h)
bool CompileComputeShaderFromSource(ID3D11Device* device, const char* source, const char* name, ID3D11ComputeShader** ppShader)
{
	ID3DBlob* pBlob = nullptr;
	ID3DBlob* pErrorBlob = nullptr;

	HRESULT hr = D3DCompile(
		source,
		strlen(source),
		name,           // source name shown in error messages
		nullptr,
		nullptr,
		"main",
		"cs_5_0",
		0,
		0,
		&pBlob,
		&pErrorBlob
	);

	if (FAILED(hr))
	{
		std::cerr << "Compute Shader Compilation Error (" << name << "):" << std::endl;
		if (pErrorBlob)
		{
			std::cerr << (char*)pErrorBlob->GetBufferPointer() << std::endl;
			pErrorBlob->Release();
		}
		if (pBlob) pBlob->Release();
		return false;
	}

	if (pBlob->GetBufferSize() == 0)
	{
		std::cerr << "Shader blob size is 0" << std::endl;
		pBlob->Release();
		return false;
	}

	hr = device->CreateComputeShader(
		pBlob->GetBufferPointer(),
		pBlob->GetBufferSize(),
		nullptr,
		ppShader
	);

	pBlob->Release();
	if (FAILED(hr))
	{
		std::cerr << "CreateComputeShader failed with HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
		return false;
	}

	return true;
};

bool CompileComputeShader(ID3D11Device* device)
{
	return CompileComputeShaderFromSource(device, plm_shaders::BitpackHologramsCS, "BitpackHologramsCS", &g_pComputeShader);
};

bool CompileComputeShaderNIR(ID3D11Device* device)
{
	return CompileComputeShaderFromSource(device, plm_shaders::BitpackHologramsNIR_CS, "BitpackHologramsNIR_CS", &g_pComputeShaderNIR);
};

bool CompileComputeShaderUnpack(ID3D11Device* device)
{
	return CompileComputeShaderFromSource(device, plm_shaders::UnpackHologramsCS, "UnpackHologramsCS", &g_pComputeShaderUnpack);
};

bool CompileComputeShaderUnpackNIR(ID3D11Device* device)
{
	return CompileComputeShaderFromSource(device, plm_shaders::UnpackHologramsNIR_CS, "UnpackHologramsNIR_CS", &g_pComputeShaderUnpackNIR);
};

bool InitBitpackResources()
{
	if (!g_pd3dDevice) return false;

	HRESULT hr;
	D3D11_BUFFER_DESC bufDesc = {};
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	// Constant buffer for shader parameters
	bufDesc = {};
	bufDesc.ByteWidth = sizeof(c_Params);
	bufDesc.Usage = D3D11_USAGE_DEFAULT;
	bufDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	hr = g_pd3dDevice->CreateBuffer(&bufDesc, nullptr, &g_pConstantBuffer);
	if (FAILED(hr)) return false;

	// Phase buffer for input data, sized for max 24 holograms
	const int max_num_holograms = 24;
	bufDesc = {};
	bufDesc.ByteWidth = sizeof(float) * N * M * max_num_holograms;
	bufDesc.Usage = D3D11_USAGE_DYNAMIC;  
	bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; 
	bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; 
	bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; 
	bufDesc.StructureByteStride = sizeof(float);
	hr = g_pd3dDevice->CreateBuffer(&bufDesc, nullptr, &g_pPhaseBuffer);
	if (FAILED(hr)) {
		g_pConstantBuffer->Release();
		g_pConstantBuffer = nullptr;
		return false;
	}

	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	srvDesc.BufferEx.FirstElement = 0;
	srvDesc.BufferEx.NumElements = N * M * max_num_holograms;
	hr = g_pd3dDevice->CreateShaderResourceView(g_pPhaseBuffer, &srvDesc, &g_pPhaseSRV);
	if (FAILED(hr)) {
		g_pPhaseBuffer->Release();
		g_pPhaseBuffer = nullptr;
		g_pConstantBuffer->Release();
		g_pConstantBuffer = nullptr;
		return false;
	}


	// NIR LUT buffer: 64 floats (odd[0..31] + even[32..63], or gamma duplicated)
	bufDesc = {};
	bufDesc.ByteWidth = sizeof(float) * 64;
	bufDesc.Usage = D3D11_USAGE_DEFAULT;
	bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufDesc.StructureByteStride = sizeof(float);
	hr = g_pd3dDevice->CreateBuffer(&bufDesc, nullptr, &g_pLUTBuffer);

	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	srvDesc.BufferEx.FirstElement = 0;
	srvDesc.BufferEx.NumElements = 64;
	hr = g_pd3dDevice->CreateShaderResourceView(g_pLUTBuffer, &srvDesc, &g_pLUTSRV);

	// Phase map buffer sized for largest case (NIR: 192 ints)
	bufDesc = {};
	bufDesc.ByteWidth = sizeof(int) * 192;
	bufDesc.Usage = D3D11_USAGE_DEFAULT;
	bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bufDesc.StructureByteStride = sizeof(int);
	hr = g_pd3dDevice->CreateBuffer(&bufDesc, nullptr, &g_pPhaseMapBuffer);

	srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	srvDesc.BufferEx.FirstElement = 0;
	srvDesc.BufferEx.NumElements = 192;
	hr = g_pd3dDevice->CreateShaderResourceView(g_pPhaseMapBuffer, &srvDesc, &g_pPhaseMapSRV);

	// Hologram buffer for output
	D3D11_TEXTURE2D_DESC texDesc = {};
	texDesc.Width = (UINT)ActiveHologramWidthPx();
	texDesc.Height = (UINT)ActiveHologramHeightPx();
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R32_UINT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	hr = g_pd3dDevice->CreateTexture2D(&texDesc, nullptr, &pHologramTexture);
	if (FAILED(hr)) {
		std::cout << "Failed to create hologram buffer with HRESULT: 0x"
			<< std::hex << hr << std::dec << std::endl;		
		g_pPhaseSRV->Release();
		g_pPhaseSRV = nullptr;
		g_pPhaseBuffer->Release();
		g_pPhaseBuffer = nullptr;
		g_pConstantBuffer->Release();
		g_pConstantBuffer = nullptr;
		return false;
	}


	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_UINT;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	hr = g_pd3dDevice->CreateUnorderedAccessView(pHologramTexture, &uavDesc, &g_pHologramUAV);
	if (FAILED(hr)) {
		std::cout << "Failed to create UAV with HRESULT: 0x"
			<< std::hex << hr << std::dec << std::endl;
		pHologramTexture->Release();
		pHologramTexture = nullptr;
		g_pPhaseSRV->Release();
		g_pPhaseSRV = nullptr;
		g_pPhaseBuffer->Release();
		g_pPhaseBuffer = nullptr;
		g_pConstantBuffer->Release();
		g_pConstantBuffer = nullptr;
		return false;
	}

	// Staging buffer to copy output to CPU
	D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
	//stagingDesc.Width = 4*2*N;  
	//stagingDesc.Height = 2*M; 
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = g_pd3dDevice->CreateTexture2D(&stagingDesc, nullptr, &pStagingTexture);

	if (FAILED(hr)) {
		g_pHologramUAV->Release();
		g_pHologramUAV = nullptr;
		pHologramTexture->Release();
		pHologramTexture = nullptr;
		g_pPhaseSRV->Release();
		g_pPhaseSRV = nullptr;
		g_pPhaseBuffer->Release();
		g_pPhaseBuffer = nullptr;
		g_pConstantBuffer->Release();
		g_pConstantBuffer = nullptr;
		return false;
	}

	return true;
}

bool InitUnpackResources()
{
	if (!g_pd3dDevice) return false;

	const uint64_t holo_w = ActiveHologramWidthPx();
	const uint64_t holo_h = ActiveHologramHeightPx();
	const int max_num_holograms = 24;

	HRESULT hr;

	// Input texture (bitpacked frame). USAGE_DEFAULT + UpdateSubresource for upload.
	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = (UINT)holo_w;
		td.Height = (UINT)holo_h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R32_UINT;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		hr = g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_pUnpackInputTex);
		if (FAILED(hr)) return false;

		hr = g_pd3dDevice->CreateShaderResourceView(g_pUnpackInputTex, nullptr, &g_pUnpackInputSRV);
		if (FAILED(hr)) return false;
	}

	// Output buffer: structured float, sized for max input dimensions × 24 holograms.
	// Sized in (N, M) — uses the globals N, M which are set by SetPLMWindowPos.
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)(sizeof(float) * N * M * max_num_holograms);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(float);
		hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_pUnpackOutBuffer);
		if (FAILED(hr)) return false;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = (UINT)(N * M * max_num_holograms);
		hr = g_pd3dDevice->CreateUnorderedAccessView(g_pUnpackOutBuffer, &uavDesc, &g_pUnpackOutUAV);
		if (FAILED(hr)) return false;

		// Staging buffer for CPU readback.
		D3D11_BUFFER_DESC sbd = {};
		sbd.ByteWidth = bd.ByteWidth;
		sbd.Usage = D3D11_USAGE_STAGING;
		sbd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		sbd.MiscFlags = 0;
		hr = g_pd3dDevice->CreateBuffer(&sbd, nullptr, &g_pUnpackOutStaging);
		if (FAILED(hr)) return false;
	}

	// Inverse-map buffers (rebuilt and uploaded each call from the current
	// forward map; D3D11_USAGE_DEFAULT + UpdateSubresource).
	auto make_inverse_map_buffer = [&](UINT count, ID3D11Buffer** outBuf, ID3D11ShaderResourceView** outSRV) -> bool {
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)(sizeof(int) * count);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(int);
		HRESULT h = g_pd3dDevice->CreateBuffer(&bd, nullptr, outBuf);
		if (FAILED(h)) return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = DXGI_FORMAT_UNKNOWN;
		sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		sd.BufferEx.NumElements = count;
		h = g_pd3dDevice->CreateShaderResourceView(*outBuf, &sd, outSRV);
		return SUCCEEDED(h);
	};
	if (!make_inverse_map_buffer(16, &g_pInverseMapVISBuf, &g_pInverseMapVISSRV)) return false;
	if (!make_inverse_map_buffer(64, &g_pInverseMapNIRBuf, &g_pInverseMapNIRSRV)) return false;

	return true;
}

// Builds the inverse phase map from the current forward map.
// out is sized 16 (VIS) or 64 (NIR); -1 marks codes that no level produces.
static void BuildInverseMapVIS(int out[16]) {
	for (int i = 0; i < 16; i++) out[i] = -1;
	for (int level = 0; level < 16; level++) {
		int code = vis_phase_map[level * 4 + 0]
		         | (vis_phase_map[level * 4 + 1] << 1)
		         | (vis_phase_map[level * 4 + 2] << 2)
		         | (vis_phase_map[level * 4 + 3] << 3);
		out[code & 0xF] = level;
	}
}
static void BuildInverseMapNIR(int out[64]) {
	for (int i = 0; i < 64; i++) out[i] = -1;
	for (int level = 0; level < 32; level++) {
		int code = 0;
		for (int k = 0; k < 6; k++) code |= (nir_phase_map[level * 6 + k] & 1) << k;
		out[code & 0x3F] = level;
	}
}

bool UnpackHologramsGPU(
	unsigned char* frame,
	float* phase,
	unsigned long long N_in,
	unsigned long long M_in,
	int num_holograms)
{
	plm_type = VIS;
#ifndef PLM_DEBUG
	bitpacking_in_progress.store(true);
	while (!same_thread.load() && UI_is_rendering.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

	if (num_holograms > 24 || !frame || !phase) {
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}
	if (!g_pd3dDevice || !g_pd3dDeviceContext || !g_pComputeShaderUnpack ||
	    !g_pConstantBuffer || !g_pUnpackInputTex || !g_pUnpackInputSRV ||
	    !g_pUnpackOutBuffer || !g_pUnpackOutUAV || !g_pUnpackOutStaging ||
	    !g_pInverseMapVISBuf || !g_pInverseMapVISSRV) {
		std::cout << "Unpack resources not initialized" << std::endl;
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}

	// Constant buffer
	c_Params cp = {};
	cp.N = (uint32_t)N_in;
	cp.M = (uint32_t)M_in;
	cp.num_holograms = (uint32_t)num_holograms;
	cp.phase_stride = (uint32_t)(N_in * M_in);
	g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &cp, 0, 0);

	// Inverse map (rebuilt from current vis_phase_map)
	int inv[16];
	BuildInverseMapVIS(inv);
	g_pd3dDeviceContext->UpdateSubresource(g_pInverseMapVISBuf, 0, nullptr, inv, 0, 0);

	// Upload bitpacked frame: VIS layout is 2N × 2M, 4 bytes/pixel
	const uint64_t row_bytes = 2 * N_in * 4;
	g_pd3dDeviceContext->UpdateSubresource(g_pUnpackInputTex, 0, nullptr, frame, (UINT)row_bytes, 0);

	// Bind & dispatch
	g_pd3dDeviceContext->CSSetShader(g_pComputeShaderUnpack, nullptr, 0);
	g_pd3dDeviceContext->CSSetConstantBuffers(0, 1, &g_pConstantBuffer);
	ID3D11ShaderResourceView* srvs[2] = { g_pUnpackInputSRV, g_pInverseMapVISSRV };
	g_pd3dDeviceContext->CSSetShaderResources(0, 2, srvs);
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, &g_pUnpackOutUAV, nullptr);
	g_pd3dDeviceContext->Dispatch((UINT)ceil((double)N_in / 16.0), (UINT)ceil((double)M_in / 16.0), 1);

	// Unbind UAV before readback
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

	// Readback
	g_pd3dDeviceContext->CopyResource(g_pUnpackOutStaging, g_pUnpackOutBuffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(g_pd3dDeviceContext->Map(g_pUnpackOutStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}
	memcpy(phase, mapped.pData, (size_t)N_in * M_in * num_holograms * sizeof(float));
	g_pd3dDeviceContext->Unmap(g_pUnpackOutStaging, 0);

#ifndef PLM_DEBUG
	bitpacking_in_progress.store(false);
#endif
	return true;
}

bool UnpackHologramsNIRGPU(
	unsigned char* frame,
	float* phase,
	unsigned long long N_in,
	unsigned long long M_in,
	int num_holograms)
{
	plm_type = NIR;
#ifndef PLM_DEBUG
	bitpacking_in_progress.store(true);
	while (!same_thread.load() && UI_is_rendering.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

	if (num_holograms > 24 || !frame || !phase) {
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}
	if (!g_pd3dDevice || !g_pd3dDeviceContext || !g_pComputeShaderUnpackNIR ||
	    !g_pConstantBuffer || !g_pUnpackInputTex || !g_pUnpackInputSRV ||
	    !g_pUnpackOutBuffer || !g_pUnpackOutUAV || !g_pUnpackOutStaging ||
	    !g_pLUTBuffer || !g_pLUTSRV ||
	    !g_pInverseMapNIRBuf || !g_pInverseMapNIRSRV) {
		std::cout << "Unpack NIR resources not initialized" << std::endl;
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}

	c_Params cp = {};
	cp.N = (uint32_t)N_in;
	cp.M = (uint32_t)M_in;
	cp.num_holograms = (uint32_t)num_holograms;
	cp.phase_stride = (uint32_t)(N_in * M_in);
	g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &cp, 0, 0);

	// Refresh active NIR LUT. Alpha uses odd/even LUTs; gamma uploads the same LUT twice.
	UploadActiveNIRLUT();

	int inv[64];
	BuildInverseMapNIR(inv);
	g_pd3dDeviceContext->UpdateSubresource(g_pInverseMapNIRBuf, 0, nullptr, inv, 0, 0);

	// NIR bitpacked layout: (3N+4) × 2M, 4 bytes/pixel
	const uint64_t row_bytes = (3 * N_in + 4) * 4;
	g_pd3dDeviceContext->UpdateSubresource(g_pUnpackInputTex, 0, nullptr, frame, (UINT)row_bytes, 0);

	g_pd3dDeviceContext->CSSetShader(g_pComputeShaderUnpackNIR, nullptr, 0);
	g_pd3dDeviceContext->CSSetConstantBuffers(0, 1, &g_pConstantBuffer);
	ID3D11ShaderResourceView* srvs[3] = { g_pUnpackInputSRV, g_pLUTSRV, g_pInverseMapNIRSRV };
	g_pd3dDeviceContext->CSSetShaderResources(0, 3, srvs);
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, &g_pUnpackOutUAV, nullptr);
	g_pd3dDeviceContext->Dispatch((UINT)ceil((double)N_in / 16.0), (UINT)ceil((double)M_in / 16.0), 1);

	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

	g_pd3dDeviceContext->CopyResource(g_pUnpackOutStaging, g_pUnpackOutBuffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(g_pd3dDeviceContext->Map(g_pUnpackOutStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
#ifndef PLM_DEBUG
		bitpacking_in_progress.store(false);
#endif
		return false;
	}
	memcpy(phase, mapped.pData, (size_t)N_in * M_in * num_holograms * sizeof(float));
	g_pd3dDeviceContext->Unmap(g_pUnpackOutStaging, 0);

#ifndef PLM_DEBUG
	bitpacking_in_progress.store(false);
#endif
	return true;
}

bool Cleanup() {
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	StopUI();
	return true;
};

int Play() {return PLM::Play();};
int Stop() {return PLM::Stop();};

int SetSource(unsigned int source, unsigned int port_width) {return PLM::SetSource(source, port_width);};
int SetPortSwap(unsigned int port, unsigned int swap) {return PLM::SetPortSwap(port, swap);};
int SetPortConfig(int connection_type) { 
	//HDMI = 1, DP = 2
	if (connection_type != 1 && connection_type != 2) return -1; 
	return PLM::SetPortConfig(connection_type == 1 ? 0 : 2, 0, 0, 0); 
};
int SetConnectionType(int connection_type) {return PLM::SetConnectionType(connection_type);};
int SetVideoPatternMode() {return PLM::SetVideoPatternMode();};
int UpdateLUT(int play_mode, int connection_type) {return PLM::UpdateLUT(play_mode, connection_type);};
int GetVideoPatternMode() {return PLM::GetVideoPatternMode();};
int GetConnectionType() {return PLM::GetConnectionType();};
int Open() {return PLM::Open();};
int Close() {return PLM::Close();};
//int Configure(unsigned int play_mode, unsigned int connection_type) {
//	return PLM::Configure(play_mode, connection_type);
//}


bool PauseUI() {
	pause_UI = true;
	return true;
};

bool ResumeUI() {
	pause_UI = false;
	return true;
};

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

bool StartDisplaying() {
	// Start displaying continuously on the PLM
	// Tailored for real-time applications.
	// IN CONSTRUCTION

	displaying_active = true;
	first_frame_trigger = true;

	return true;
}

bool Resynchronise(unsigned long long offset) {
	// IN CONSTRUCTION
	return true;
}

// Main code
int UI(){

	//if (!GetSecondMonitorRect(monitorRect, monitor_id)) {
	//	std::cerr << "Second monitor not found!" << std::endl;
	//	return 1;
	//}

	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"plmctrl", nullptr };
	::RegisterClassExW(&wc);

	monitorRect.left = 0;
	monitorRect.top = 0;

	HWND hwnd = ::CreateWindowEx(
		WS_EX_TOPMOST, // dwExStyle: No extended styles
		wc.lpszClassName,
		L"plmctrl",
		WS_POPUP | WS_VISIBLE,
		window_x0, window_y0,
		(int)ActiveHologramWidthPx(), (int)ActiveHologramHeightPx(),
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
	if (show_debug_window){
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // Enable Multi-Viewport / Platform Windows
	}
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigDockingWithShift = true; // Enable docking with shift key
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)  std::cout << "[plmctrl]: Viewports enabled" << std::endl;

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


	// Frame texture (holds the bitpacked holograms)
	desc.Width = (UINT)ActiveHologramWidthPx();
	desc.Height = (UINT)ActiveHologramHeightPx();
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	g_pd3dDevice->CreateTexture2D(&desc, nullptr, &pTexture);
	g_pd3dDevice->CreateShaderResourceView(pTexture, nullptr, &data_texture_srv);

	// Phase preview texture: native (N, M) so each pixel maps to one phase sample.
	D3D11_TEXTURE2D_DESC phaseDesc = desc;
	phaseDesc.Width  = (UINT)N;
	phaseDesc.Height = (UINT)M;
	g_pd3dDevice->CreateTexture2D(&phaseDesc, nullptr, &pPhaseTexture);
	g_pd3dDevice->CreateShaderResourceView(pPhaseTexture, nullptr, &phase_texture_srv);

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // Nearest neighbor (no interpolation)
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_pSamplerNearest);




	timepoint now;
	bool done = false;
	// Main UI loop. Changes the frames with VSync enabled
	while (running && !done)
	{
		UI_is_rendering.store(false);
		if (bitpacking_in_progress.load() || pause_UI.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		};
		UI_is_rendering.store(true);

		if (frames_to_play < 0 && sequence_active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			// Pause Playing the sequence.

			if (plm_connected)  PLM::Stop(); // This only works with INCLUDE_LIGHTCRAFTER_WRAPPERS is defined
			plm_is_displaying = false;
			sequence_active = false;

			buffer_index = -1; // buffer_index = -1 signals that the sequence has ended
			plm_mode = PLM_IDLE;

			camera_trigger = false;

			//std::cout << "Sequence finished" << std::endl;
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
		if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED){
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
		ImGui::Begin("DockSpace", &popen, window_flags);
		ImGui::PopStyleVar(2);
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
		ImGui::End();

		const uint64_t active_width = ActiveHologramWidthPx();
		const uint64_t active_height = ActiveHologramHeightPx();
		uint64_t frame_elements = 4ULL * active_width * active_height;
		if (frames_to_play == frames_in_sequence && sequence_active) {
			plm_mode = PLM_PLAYING;
			frame_index = 0;
			first_frame_trigger = false;
			start_playing_trigger = true;
		}

		plm_image_ptr = frame_set.data()
			+ frame_order[frame_index % MAX_FRAMES] * frame_elements;

		// PLM frame window
		PLM::ImagescPLM("PLM", plm_image_ptr, data_texture_srv, g_pd3dDevice, g_pd3dDeviceContext, g_pSamplerNearest, io, (int)active_width, (int)active_height, &mutex, window_x0, window_y0);


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
		};

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
			t0 = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
			//std::cout << "[plmctrl]: First frame trigger" << std::endl;
			PLM::Play(); // This only works if LightCrafter wrappers are included

		};
		//if (plm_mode == PLM_PLAYING) {
		//	std::cout << "[plmctrl]: Buffer Index on plmctrl: " << buffer_index << std::endl;
		//}

		end = std::chrono::high_resolution_clock::now();
		elapsed_buffer = end - start;

		end_total = std::chrono::high_resolution_clock::now();
		elapsed_total = end_total - start_total;

		// first_frame_trigger is a variable to know exactly that the first frame was already sent to the GPU buffer queue. 
		if (frames_to_play >= 0 && !(first_frame_trigger)) {
			frame_index++;
			frame_index = clamp(frame_index, 0, MAX_FRAMES - 1);
			frames_to_play--;
		};

		UI_is_rendering.store(false);

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

	frame_order.resize(MAX_FRAMES);
	for (int i = 0; i < MAX_FRAMES; i++) {
		frame_order[i] = i;
	};

	if (running) {
		StopUI();
		StartUI(number_of_frames);
		return;
	};

	running = true;
	plm_image_ptr = nullptr;

	const uint64_t active_width = ActiveHologramWidthPx();
	const uint64_t active_height = ActiveHologramHeightPx();
	frame.resize(4ULL * active_width * active_height);
	frame_set.resize(4ULL * active_width * active_height * MAX_FRAMES);
	std::fill(frame_set.begin(), frame_set.end(), 255);

	// Continuous-phase storage parallel to frame_set: 24 phase planes per frame slot.
	// Sized in (N, M) which differ between VIS and NIR.
	phase_set.assign((size_t)MAX_FRAMES * 24ULL * (size_t)N * (size_t)M, 0.0f);


#ifndef PLM_DEBUG
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

void SetWindowed(bool windowed_mode) {
	windowed = windowed_mode;
};

void ShowDebugPanel(bool show) {
	show_debug_window = show;
};

void SetPLMWindowPos(int width, int height, int x0 = 0, int y0 = 0 ) {
	N = width;
	M = height;
	window_x0 = x0;
	window_y0 = y0;

	if (N == 904 && M == 800) {
		plm_type = NIR;
	} else if (N == 1358 && M == 800) {
		plm_type = VIS;
	}
};


bool SetPhaseMap(int* new_phase_map) {
	plm_type = VIS;
	const int phase_map_size = 16 * 4;
	for (int i = 0; i < phase_map_size; i++) {
		vis_phase_map[i] = new_phase_map[i];
	};
	return true;
}

//void SetLookupTable(float* lut) {
//	for (int i = 0; i < 17; i++) {
//		phases[i] = lut[i];
//	}
//}

int GetPLMType() {
	return (int)plm_type;
}

bool SetNIRVariant(int variant) {

	if (variant != NIR_ALPHA && variant != NIR_GAMMA) {
		return false;
	}

	nir_variant = (NIRVariant)variant;
	plm_type = NIR;
	return true;
}

bool SetPhaseMapNIR(int* new_phase_map) {
	plm_type = NIR;
	// NIR: 32 levels x 6 cells per 3x2 superpixel = 192 entries
	const int phase_map_size = 32 * 6;
	for (int i = 0; i < phase_map_size; i++) {
		nir_phase_map[i] = new_phase_map[i];
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

bool InsertPLMFrame(unsigned char* frame, unsigned long long num_frames = 1, unsigned long long offset = 0, int type = 0) {

	// Type: 0 - RGB;
	// Type: 1 - RGBA;

	if (offset + num_frames > MAX_FRAMES) {
		// Exceeds the maximum number of frames we can store
		return false;
	};

	//std::cout << "Inserting " << num_frames << " frames at offset " << offset << std::endl;

	uint64_t rgb_elements = ActiveHologramWidthPx() * ActiveHologramHeightPx();
	uint64_t frame_elements = 4 * rgb_elements;
	uint64_t total_elements = num_frames * frame_elements;
	int k = 0;

	if (type == 0) {
		//std::cout << "Type: RGB" << std::endl;
		for (uint64_t n = 0; n < num_frames; n++) {
			for (uint64_t i = 0; i < rgb_elements; i++) {
				frame_set.at(4 * i + (n + offset) * frame_elements + 0) = frame[3 * i + n * (3 * rgb_elements) + 0];
				frame_set.at(4 * i + (n + offset) * frame_elements + 1) = frame[3 * i + n * (3 * rgb_elements) + 1];
				frame_set.at(4 * i + (n + offset) * frame_elements + 2) = frame[3 * i + n * (3 * rgb_elements) + 2];
			};
			//std::cout << "Frame " << n << " inserted" << std::endl;
		};
	}
	else if (type == 1) {
		//std::cout << "Type: RGBA" << std::endl;
		std::copy(
			frame,
			frame + total_elements,
			frame_set.begin() + offset * frame_elements
		);
	};
	//std::cout << num_frames << " frames inserted" << std::endl;

	frame_set_writes.fetch_add(1);
	return true;
};

bool SetPLMFrame(unsigned long long offset = 0) {

	if (offset >= MAX_FRAMES) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	frame_index = offset;

	//for (int i = 0; i < MAX_FRAMES; i++) {
	//	frame_order[i] = offset;
	//};

	return true;
};

bool GrabPLMFrame(unsigned char* hologram, uint64_t index = 0) {

	if (index >= MAX_FRAMES) {
		// Exceeds the maximum number of holograms we can store
		return false;
	};

	uint64_t frame_elements = 4ULL * ActiveHologramWidthPx() * ActiveHologramHeightPx();
	uint64_t ptr_offset = index * frame_elements;

	for (uint64_t i = 0; i < frame_elements; i++) {
		hologram[i] = frame_set.at(i + ptr_offset);
	};

	return true;
};

unsigned int QuantisePhase(float phaseVal) {
	for (int level_num = 0; level_num < 17; level_num++) {
		if ((phaseVal >= vis_phases[level_num]) && (phaseVal < vis_phases[level_num + 1])) {
			if (fabs(phaseVal - vis_phases[level_num]) < fabs(phaseVal - vis_phases[level_num + 1])) {
				return level_num;
			};
			return (level_num + 1) % 16;
		}
	}
	return 0; // Default return if no condition is met
}

unsigned int QuantisePhaseNIR(float phaseVal, int col_parity) {
	// col_parity: 0 = odd column (1-indexed), 1 = even column (1-indexed)
	const float* lut = (col_parity == 0) ? ActiveNIRPhasesOdd() : ActiveNIRPhasesEven();
	float min_dist = 2.0f;
	unsigned int best_level = 0;
	for (int l = 0; l < 32; l++) {
		float dist = fabsf(phaseVal - lut[l]);
		if (dist < min_dist) {
			min_dist = dist;
			best_level = l;
		}
	}
	return best_level;
}

bool BitpackHolograms(
	float* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms
) {
	plm_type = VIS;
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
				hologram[4 * (2 * i + 0) + (2 * j + 1) * (4 * 2 * N) + color_id] |= vis_phase_map[level * 4 + 0] << offset;
				hologram[4 * (2 * i + 0) + (2 * j + 0) * (4 * 2 * N) + color_id] |= vis_phase_map[level * 4 + 1] << offset;
				hologram[4 * (2 * i + 1) + (2 * j + 1) * (4 * 2 * N) + color_id] |= vis_phase_map[level * 4 + 2] << offset;
				hologram[4 * (2 * i + 1) + (2 * j + 0) * (4 * 2 * N) + color_id] |= vis_phase_map[level * 4 + 3] << offset;


				hologram[4 * (2 * i + 0) + (2 * j + 1) * (4 * 2 * N) + 3] = 255;
				hologram[4 * (2 * i + 0) + (2 * j + 0) * (4 * 2 * N) + 3] = 255;
				hologram[4 * (2 * i + 1) + (2 * j + 1) * (4 * 2 * N) + 3] = 255;
				hologram[4 * (2 * i + 1) + (2 * j + 0) * (4 * 2 * N) + 3] = 255;
			};
		};
		holo++;
	}

	return true;
};

bool BitpackHologramsNIR(
	float* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms
) {
	plm_type = NIR;
	// NIR PLM: 904 x 800 physical pixels, 3x2 superpixel, 32 levels
	// Output hologram: (3*N + 4) x (2*M) pixels = 2716 x 1600 for N=904, M=800
	// 2 zero-padded columns on each side (EVM expects 2716 wide)
	// Caller must zero the hologram buffer before calling.

	if (num_holograms > 24) {
		return false;
	};

	uint64_t phase_elements = N * M;
	uint64_t holo_width = 3 * N + 4;
	uint64_t holo_height = 2 * M;
	uint64_t row_stride = 4 * holo_width;

	// NIR 3x2 superpixel cell offsets (column-major, bottom then top per column)
	// Matches VIS convention: k=0 bottom-left, k=1 top-left, ...
	static const int nir_dx[6] = {0, 0, 1, 1, 2, 2};
	static const int nir_dy[6] = {1, 0, 1, 0, 1, 0};

	uint64_t holo = 0;

	for (int n = 0; n < num_holograms; n++) {

		uint64_t color_id = floor(holo % 24 / 8);
		uint64_t offset = holo % 8;

		for (uint64_t j = 0; j < M; j++) {
			for (uint64_t i = 0; i < N; i++) {
				float phase_val = phase[i + j * N + n * phase_elements];
				int col_parity = i % 2; // 0 = odd col (1-indexed), 1 = even col
				unsigned int level = QuantisePhaseNIR(phase_val, col_parity);

				uint64_t base_col = 2 + 3 * i; // 2 columns zero-padding on left
				uint64_t base_row = 2 * j;

				for (int k = 0; k < 6; k++) {
					uint64_t col = base_col + nir_dx[k];
					uint64_t row = base_row + nir_dy[k];

					hologram[4 * col + row * row_stride + color_id] |= nir_phase_map[level * 6 + k] << offset;
					hologram[4 * col + row * row_stride + 3] = 255;
				}
			};
		};
		holo++;
	}

	// Set alpha for zero-padded columns
	for (uint64_t j = 0; j < holo_height; j++) {
		for (uint64_t pad_col = 0; pad_col < 2; pad_col++) {
			hologram[4 * pad_col + j * row_stride + 3] = 255;
		}
		for (uint64_t pad_col = holo_width - 2; pad_col < holo_width; pad_col++) {
			hologram[4 * pad_col + j * row_stride + 3] = 255;
		}
	}

	return true;
};

bool BitpackHologramsGPU(
	float* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms,
	bool same_phase
)
{
	plm_type = VIS;
#ifndef PLM_DEBUG
	bitpacking_in_progress.store(true);
	while (!same_thread.load() && UI_is_rendering.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

	// Check if the number of holograms is within the limit
	if (num_holograms > 24) return false;

	if (!phase || !hologram) {
		std::cout << "Null pointer detected" << std::endl;
		return false;
	};

	// Check all resources are initialized
	if (!g_pd3dDevice || !g_pd3dDeviceContext || !g_pComputeShader ||
		!g_pConstantBuffer || !g_pPhaseBuffer || !g_pPhaseSRV ||
		!pHologramTexture || !g_pHologramUAV || !pStagingTexture || !g_pPhaseMapBuffer) {
		std::cout << "Resource not initialized" << std::endl;
		return false;
	};

	// Update constant buffer
	c_Params constant = {};
	constant.N = (uint32_t)N;
	constant.M = (uint32_t)M;
	constant.num_holograms = (uint32_t)num_holograms;
	constant.phase_stride = same_phase ? 0u : (uint32_t)(N * M);
	g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &constant, 0, 0);

	D3D11_BOX box;

	// Update LUT buffer with current data 
	box = { 0, 0, 0, (UINT)(sizeof(float) * 17), 1, 1 };
	g_pd3dDeviceContext->UpdateSubresource(g_pLUTBuffer, 0, &box, vis_phases, 0, 0);
	ZeroMemory(&box, sizeof(box));

	// Update PhaseMap buffer with VIS phase map (16 levels x 4 cells = 64 ints)
	box = { 0, 0, 0, (UINT)(sizeof(int) * 64), 1, 1 };
	g_pd3dDeviceContext->UpdateSubresource(g_pPhaseMapBuffer, 0, &box, vis_phase_map, 0, 0);

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr_ = g_pd3dDeviceContext->Map(g_pPhaseBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (SUCCEEDED(hr_)) {
		BYTE* pDest = reinterpret_cast<BYTE*>(mappedResource.pData);
		size_t phase_count = same_phase ? (size_t)(N * M) : (size_t)(N * M * num_holograms);
		memcpy(pDest, phase, phase_count * sizeof(float));
		g_pd3dDeviceContext->Unmap(g_pPhaseBuffer, 0);
	} else {
		return false;
	};

	g_pd3dDeviceContext->CSSetShader(g_pComputeShader, nullptr, 0);
	g_pd3dDeviceContext->CSSetConstantBuffers(0, 1, &g_pConstantBuffer);
	g_pd3dDeviceContext->CSSetShaderResources(0, 1, &g_pLUTSRV);
	g_pd3dDeviceContext->CSSetShaderResources(1, 1, &g_pPhaseMapSRV);
	g_pd3dDeviceContext->CSSetShaderResources(2, 1, &g_pPhaseSRV);
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, &g_pHologramUAV, nullptr);

	g_pd3dDeviceContext->Dispatch(ceil(2.0 * N / 16.0), ceil(2.0 * M / 16.0), 1);

	// Copy result to staging texture
	g_pd3dDeviceContext->CopyResource(pStagingTexture, pHologramTexture);

	if (pStagingTexture == nullptr) {
		std::cerr << "Failed to copy hologram to staging texture" << std::endl;
		return false;
	};
	// Map staging texture and copy to CPU
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = g_pd3dDeviceContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

	if (FAILED(hr)) {
		//std::cerr << "Failed to map staging texture: 0x" << std::hex << hr << std::dec << std::endl;
		return false;
	}

	// Copy to hologram array, accounting for RowPitch
	uint8_t* dest = static_cast<uint8_t*>(hologram);         // Destination buffer
	uint8_t* src = static_cast<uint8_t*>(mapped.pData);      // Source: mapped texture data
	uint32_t widthBytes = (uint32_t)(2 * N * 4);             // VIS: 2*N pixels per row, 4 bytes each
	uint32_t height = (uint32_t)(2 * M);

	for (uint32_t row = 0; row < height; ++row) {
		// Copy each row, respecting the pitch of the mapped resource
		memcpy(dest + row * widthBytes,                   // Destination offset
			src + row * mapped.RowPitch,                  // Source offset with pitch
			widthBytes);                                  // Bytes per row (no padding in dest)
	}

	g_pd3dDeviceContext->Unmap(pStagingTexture, 0);

#ifndef PLM_DEBUG
	bitpacking_in_progress.store(false);
#endif

	return true;
}

bool BitpackAndInsertGPU(
	float* phase,
	unsigned long long N,
	unsigned long long M,
	int num_holograms,
	unsigned long long offset,
	bool same_phase
) {
	if (!BitpackHologramsGPU(phase, frame.data(), N, M, num_holograms, same_phase)) {
		std::cerr << "Failed to bitpack holograms" << std::endl;
		return false;
	};

	InsertPLMFrame((unsigned char*) frame.data(), 1, offset, 1);
	SetPLMFrame(offset);

	return true;
}

bool BitpackHologramsNIRGPU(
	float* phase,
	unsigned char* hologram,
	unsigned long long N,
	unsigned long long M,
	int num_holograms,
	bool same_phase
)
{
	plm_type = NIR;
#ifndef PLM_DEBUG
	bitpacking_in_progress.store(true);
	while (!same_thread.load() && UI_is_rendering.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif

	if (num_holograms > 24) return false;

	if (!phase || !hologram) {
		std::cout << "Null pointer detected" << std::endl;
		return false;
	};

	if (!g_pd3dDevice || !g_pd3dDeviceContext || !g_pComputeShaderNIR ||
		!g_pConstantBuffer || !g_pPhaseBuffer || !g_pPhaseSRV ||
		!pHologramTexture || !g_pHologramUAV || !pStagingTexture ||
		!g_pLUTBuffer || !g_pLUTSRV || !g_pPhaseMapBuffer) {
		std::cout << "Resource not initialized" << std::endl;
		return false;
	};

	c_Params constant = {};
	constant.N = (uint32_t)N;
	constant.M = (uint32_t)M;
	constant.num_holograms = (uint32_t)num_holograms;
	constant.phase_stride = same_phase ? 0u : (uint32_t)(N * M);
	g_pd3dDeviceContext->UpdateSubresource(g_pConstantBuffer, 0, nullptr, &constant, 0, 0);

	UploadActiveNIRLUT();

	D3D11_BOX box;
	box = { 0, 0, 0, (UINT)(sizeof(int) * 192), 1, 1 };
	g_pd3dDeviceContext->UpdateSubresource(g_pPhaseMapBuffer, 0, &box, nir_phase_map, 0, 0);

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr_ = g_pd3dDeviceContext->Map(g_pPhaseBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (SUCCEEDED(hr_)) {
		size_t phase_count = same_phase ? (size_t)(N * M) : (size_t)(N * M * num_holograms);
		memcpy(mappedResource.pData, phase, phase_count * sizeof(float));
		g_pd3dDeviceContext->Unmap(g_pPhaseBuffer, 0);
	} else {
		return false;
	};

	g_pd3dDeviceContext->CSSetShader(g_pComputeShaderNIR, nullptr, 0);
	g_pd3dDeviceContext->CSSetConstantBuffers(0, 1, &g_pConstantBuffer);
	g_pd3dDeviceContext->CSSetShaderResources(0, 1, &g_pPhaseSRV);
	g_pd3dDeviceContext->CSSetShaderResources(1, 1, &g_pLUTSRV);
	g_pd3dDeviceContext->CSSetShaderResources(2, 1, &g_pPhaseMapSRV);
	g_pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, &g_pHologramUAV, nullptr);

	uint64_t holo_width  = 3 * N + 4;
	uint64_t holo_height = 2 * M;
	g_pd3dDeviceContext->Dispatch(
		(UINT)ceil((double)holo_width  / 16.0),
		(UINT)ceil((double)holo_height / 16.0),
		1
	);

	g_pd3dDeviceContext->CopyResource(pStagingTexture, pHologramTexture);

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(g_pd3dDeviceContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped)))
		return false;

	uint8_t* dest = static_cast<uint8_t*>(hologram);
	uint8_t* src  = static_cast<uint8_t*>(mapped.pData);
	uint32_t widthBytes = (uint32_t)(holo_width * 4);
	for (uint32_t row = 0; row < (uint32_t)holo_height; ++row)
		memcpy(dest + row * widthBytes, src + row * mapped.RowPitch, widthBytes);

	g_pd3dDeviceContext->Unmap(pStagingTexture, 0);

#ifndef PLM_DEBUG
	bitpacking_in_progress.store(false);
#endif

	return true;
}

bool BitpackAndInsertNIRGPU(
	float* phase,
	unsigned long long N,
	unsigned long long M,
	int num_holograms,
	unsigned long long offset,
	bool same_phase
) {
	if (!BitpackHologramsNIRGPU(phase, frame.data(), N, M, num_holograms, same_phase)) {
		std::cerr << "Failed to bitpack NIR holograms" << std::endl;
		return false;
	};

	InsertPLMFrame((unsigned char*) frame.data(), 1, offset, 1);
	SetPLMFrame(offset);

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
	sd.Windowed = windowed;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_1 };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

	printf("D3D11CreateDeviceAndSwapChain: 0x%8x\n", res);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	std::cout << "Feature Level: " << std::hex << featureLevel << std::dec << std::endl;

	// UNTESTED IN THE LAB.
	// // Minimise present latency: cap the driver's render-ahead queue to a single
	// // frame so the frame we build is the one scanned out at the next vblank,
	// // instead of sitting behind 1-2 already-queued frames (~2 frame delay).
	// {
	// 	IDXGIDevice1* dxgiDevice = nullptr;
	// 	if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice)) && dxgiDevice) {
	// 		dxgiDevice->SetMaximumFrameLatency(1);
	// 		dxgiDevice->Release();
	// 	}
	// }

	CreateRenderTarget();

    if (!CompileComputeShader(g_pd3dDevice)){
        std::cerr << "Failed to compile VIS bitpack compute shader" << std::endl;
        //return false;
    }

    if (!CompileComputeShaderNIR(g_pd3dDevice)){
        std::cerr << "Failed to compile NIR bitpack compute shader" << std::endl;
        //return false;
    }

    if (!CompileComputeShaderUnpack(g_pd3dDevice)){
        std::cerr << "Failed to compile VIS unpack compute shader" << std::endl;
        //return false;
    }

    if (!CompileComputeShaderUnpackNIR(g_pd3dDevice)){
        std::cerr << "Failed to compile NIR unpack compute shader" << std::endl;
        //return false;
    }

    if (!InitBitpackResources()){
        std::cerr << "Failed to initialize bitpack resources" << std::endl;
        //return false;
    }

    if (!InitUnpackResources()){
        std::cerr << "Failed to initialize unpack resources" << std::endl;
        //return false;
    }

	return true;
}
void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
	//// Compute shader cleanup
	if (g_pSamplerNearest) { g_pSamplerNearest->Release(); g_pSamplerNearest = nullptr; }
	if (g_pComputeShader) { g_pComputeShader->Release(); g_pComputeShader = nullptr; }
	if (g_pComputeShaderNIR) { g_pComputeShaderNIR->Release(); g_pComputeShaderNIR = nullptr; }
	if (g_pComputeShaderUnpack) { g_pComputeShaderUnpack->Release(); g_pComputeShaderUnpack = nullptr; }
	if (g_pComputeShaderUnpackNIR) { g_pComputeShaderUnpackNIR->Release(); g_pComputeShaderUnpackNIR = nullptr; }
	if (g_pInverseMapNIRSRV) { g_pInverseMapNIRSRV->Release(); g_pInverseMapNIRSRV = nullptr; }
	if (g_pInverseMapNIRBuf) { g_pInverseMapNIRBuf->Release(); g_pInverseMapNIRBuf = nullptr; }
	if (g_pInverseMapVISSRV) { g_pInverseMapVISSRV->Release(); g_pInverseMapVISSRV = nullptr; }
	if (g_pInverseMapVISBuf) { g_pInverseMapVISBuf->Release(); g_pInverseMapVISBuf = nullptr; }
	if (g_pUnpackOutStaging) { g_pUnpackOutStaging->Release(); g_pUnpackOutStaging = nullptr; }
	if (g_pUnpackOutUAV) { g_pUnpackOutUAV->Release(); g_pUnpackOutUAV = nullptr; }
	if (g_pUnpackOutBuffer) { g_pUnpackOutBuffer->Release(); g_pUnpackOutBuffer = nullptr; }
	if (g_pUnpackInputSRV) { g_pUnpackInputSRV->Release(); g_pUnpackInputSRV = nullptr; }
	if (g_pUnpackInputTex) { g_pUnpackInputTex->Release(); g_pUnpackInputTex = nullptr; }
	if (pStagingTexture) { pStagingTexture->Release(); pStagingTexture = nullptr; }
	if (g_pHologramUAV) { g_pHologramUAV->Release(); g_pHologramUAV = nullptr; }
	if (pHologramTexture) { pHologramTexture->Release(); pHologramTexture = nullptr; }
	if (g_pPhaseMapSRV) { g_pPhaseMapSRV->Release(); g_pPhaseMapSRV = nullptr; }
	if (g_pPhaseMapBuffer) { g_pPhaseMapBuffer->Release(); g_pPhaseMapBuffer = nullptr; }
	if (g_pLUTSRV) { g_pLUTSRV->Release(); g_pLUTSRV = nullptr; }
	if (g_pLUTBuffer) { g_pLUTBuffer->Release(); g_pLUTBuffer = nullptr; }
	if (g_pPhaseSRV) { g_pPhaseSRV->Release(); g_pPhaseSRV = nullptr; }
	if (g_pPhaseBuffer) { g_pPhaseBuffer->Release(); g_pPhaseBuffer = nullptr; }
	if (g_pConstantBuffer) { g_pConstantBuffer->Release(); g_pConstantBuffer = nullptr; }
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

	static long long unsigned int ui_cycles = 0;
	bool plm_updating_status = false;
	
	ui_cycles++;
	if (ui_cycles % 60 == 0) {
		ui_cycles = 0;
		plm_updating_status = true;
	}

	if (!show) return;

	// Phase preview state shared between Main/Debug tab previews.
	// Picks which of the 24 holograms in the current frame slot to visualise.
	static int phase_holo_idx = 0;

	// Repaints the phase preview texture from phase_set for the currently
	// displayed frame slot. Called on demand from the visible Phase tab so
	// we don't pay the ~1 MB CPU cost when nothing is shown.
	// Repaints the phase preview texture by unpacking the currently displayed
	// bitpacked frame from frame_set. Caches the unpacked 24 phase planes so
	// we only re-run the GPU unpack when the slot or the underlying frame data
	// changes; the hologram-index slider only triggers the cheap byte-conversion
	// repaint, not a full unpack.
	auto update_phase_texture = [&]() {
		if (!pPhaseTexture || frame_set.empty()) return;
		const size_t per_holo = (size_t)N * (size_t)M;
		const size_t slot     = (size_t)frame_order[frame_index % MAX_FRAMES];

		static std::vector<float> unpacked;
		unpacked.resize(per_holo * 24);
		static int      last_slot         = -1;
		static int      last_holo_idx     = -1;
		static uint64_t last_frame_writes = (uint64_t)-1;

		const uint64_t writes = frame_set_writes.load();
		const bool slot_changed  = ((int)slot != last_slot);
		const bool data_changed  = (writes != last_frame_writes);
		const bool holo_changed  = (phase_holo_idx != last_holo_idx);

		if (slot_changed || data_changed) {
			// Unpack the bitpacked frame at the active slot.
			const uint64_t holo_w = ActiveHologramWidthPx();
			const uint64_t holo_h = ActiveHologramHeightPx();
			const uint64_t frame_bytes = 4ULL * holo_w * holo_h;
			if ((slot + 1) * frame_bytes > frame_set.size()) return;

			unsigned char* src = frame_set.data() + slot * frame_bytes;
			same_thread.store(true);
			bool ok = IsNIRMode()
				? UnpackHologramsNIRGPU(src, unpacked.data(), N, M, 24)
				: UnpackHologramsGPU   (src, unpacked.data(), N, M, 24);
			same_thread.store(false);
			if (!ok) return;
			last_slot         = (int)slot;
			last_frame_writes = writes;
		}

		if (slot_changed || data_changed || holo_changed) {
			const size_t base = (size_t)phase_holo_idx * per_holo;
			if (base + per_holo > unpacked.size()) return;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (FAILED(g_pd3dDeviceContext->Map(pPhaseTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
			uint8_t* dst = (uint8_t*)mapped.pData;
			for (uint64_t j = 0; j < (uint64_t)M; j++) {
				uint8_t* row = dst + j * mapped.RowPitch;
				const float* psrc = unpacked.data() + base + j * (size_t)N;
				for (uint64_t i = 0; i < (uint64_t)N; i++) {
					float p = psrc[i];
					if (p < 0.0f) p = 0.0f; else if (p > 1.0f) p = 1.0f;
					uint8_t v = (uint8_t)(p * 255.0f);
					row[i * 4 + 0] = v;
					row[i * 4 + 1] = v;
					row[i * 4 + 2] = v;
					row[i * 4 + 3] = 255;
				}
			}
			g_pd3dDeviceContext->Unmap(pPhaseTexture, 0);
			last_holo_idx = phase_holo_idx;
		}
	};

	// Renders the Frame Preview content (zoom/pan sliders + Hologram/Phase tab bar).
	// `id` disambiguates ImGui IDs between the Main and Debug preview instances.
	// `zoom`, `pan_x`, `pan_y` come from the caller's static locals so each
	// preview keeps independent zoom/pan state.
	auto render_frame_preview = [&](const char* id, float& zoom, float& pan_x, float& pan_y) {
		char label[64];
		snprintf(label, sizeof(label), "Zoom##%s", id);
		ImGui::SliderFloat(label, &zoom, 1.0f, 32.0f, "%.1fx");
		const float view = 1.0f / zoom;
		const float max_pan_x = 1.0f - view;
		const float max_pan_y = 1.0f - view;
		pan_x = clamp(pan_x, 0.0f, max_pan_x);
		pan_y = clamp(pan_y, 0.0f, max_pan_y);
		snprintf(label, sizeof(label), "Pan X##%s", id);
		ImGui::SliderFloat(label, &pan_x, 0.0f, max_pan_x > 0.0f ? max_pan_x : 0.0f);
		snprintf(label, sizeof(label), "Pan Y##%s", id);
		ImGui::SliderFloat(label, &pan_y, 0.0f, max_pan_y > 0.0f ? max_pan_y : 0.0f);
		const ImVec2 uv0(pan_x,        pan_y);
		const ImVec2 uv1(pan_x + view, pan_y + view);

		auto push_nearest = []() {
			ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
				g_pd3dDeviceContext->PSSetSamplers(0, 1, &g_pSamplerNearest);
			}, nullptr);
		};
		auto pop_nearest = []() {
			ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
		};

		static bool match_scale = false;
		if (IsNIRMode()){
			ImGui::Checkbox("Match hologram <-> frame scale", &match_scale);
		} else {
			match_scale = false;
		}

		snprintf(label, sizeof(label), "PreviewTabs##%s", id);
		if (!ImGui::BeginTabBar(label)) return;
		if (ImGui::BeginTabItem("Hologram")) {
			push_nearest();
			ImGui::Image((void*)data_texture_srv,
				ImVec2((float)ActiveHologramWidthPx() / (!match_scale ? 4.0f : 6.0f), (float)ActiveHologramHeightPx() / 4.0f),
				uv0, uv1);
			pop_nearest();
			ImGui::Text("Top-left pixel: (%.0f, %.0f)",
				pan_x * (float)ActiveHologramWidthPx(),
				pan_y * (float)ActiveHologramHeightPx());
			ImGui::EndTabItem();
		}
		if (!sequence_active && ImGui::BeginTabItem("Phase")) {
			update_phase_texture();
			push_nearest();
			ImGui::Image((void*)phase_texture_srv,
				ImVec2((float)N / 2.0f, (float)M / 2.0f),
				uv0, uv1);
			pop_nearest();
			snprintf(label, sizeof(label), "Hologram (0-23)##%s", id);
			ImGui::SliderInt(label, &phase_holo_idx, 0, 23);
			ImGui::Text("Top-left pixel: (%.0f, %.0f)   greyscale = phase ∈ [0, 1]",
				pan_x * (float)N,
				pan_y * (float)M);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	};

	ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;

	ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);


	ImGui::Begin("plmctrl");


	if (ImGui::BeginTabBar("MyTabBar", tab_bar_flags))
	{
		if (ImGui::BeginTabItem("Main"))
		{


		ImGui::Text("Frametime %f ms (%f Hz)", 1000.0 * io.DeltaTime, io.Framerate);
		ImGui::Text("Framerate needs to match PLM's");
		ImGui::Text("Left: %d, Right: %d, Top: %d, Bottom: %d", monitorRect.left, monitorRect.right, monitorRect.top, monitorRect.bottom);
		ImGui::Text("PLM type: %s", IsNIRMode() ? ActiveNIRVariantLabel() : ".67 VIS");


	#ifndef INCLUDE_LIGHTCRAFTER_WRAPPERS
		ImGui::SeparatorText("!! Warning !!");
		ImGui::Text("- This version does not support commanding the PLM to Play/Stop the sequence display");
		ImGui::Text("- To enable this feature, follow the Wiki entry on this topic");
	#else
		ImGui::SeparatorText("PLM Status");
		if (PLM::IsConnected() == false) {
			PLM::Open();
		};
		Status(plm_connected);
		if (ui_cycles % 60 == 0) { // Update the status every 60 frames
			PLM::GetVersion();
			plm_connected = PLM::IsConnected();
		};
		plm_connected = PLM::IsConnected();
		ImGui::Text("%d.%d.%d", (PLM::App_ver >> 24), ((PLM::App_ver << 8) >> 24), ((PLM::App_ver << 16) >> 16));
		ImGui::BeginDisabled(!plm_connected);
		Status(plm_is_displaying);
		if (ImGui::Button("Start")) {
			PLM::Play();
			plm_is_displaying = true;
		};
		ImGui::SameLine();
		if (ImGui::Button("Stop")) {
			PLM::Stop();
			plm_is_displaying = false;
		};
		ImGui::EndDisabled();
	#endif


		//Status(first_frame_trigger);
		ImGui::SeparatorText("Sequence");
		ImGui::Text("Frames to play: %d/%d", frames_to_play, frames_in_sequence);
		ImGui::Text("Buffer Index %d/%d", 24 * buffer_index, 24 * frames_in_sequence);

		ImGui::Text("Frame order:");
		ImGui::SameLine();
		ImGui::Text("[");
		ImGui::SameLine();
		for (int i = 0; i < (MAX_FRAMES <= 4 ? (MAX_FRAMES - 1) : 4); ++i) {
			ImGui::Text("%llu", frame_order[i]);
			ImGui::SameLine();
		};
		ImGui::Text("... %llu], total: %d", frame_order[MAX_FRAMES - 1], MAX_FRAMES);

		ImGui::SeparatorText("Frame Data");
		if (ImGui::TreeNode("Frame on display")) {
			static float zoom = 1.0f, pan_x = 0.0f, pan_y = 0.0f;
			render_frame_preview("main", zoom, pan_x, pan_y);
			ImGui::TreePop();
		};
		ImGui::Text("Frame pointer [%p]", plm_image_ptr);
		ImGui::Text("Frame index: [%d], Frame [%d]", frame_index, frame_order[frame_index % MAX_FRAMES]);
		ImGui::SameLine();
		if (ImGui::ArrowButton("##left", ImGuiDir_Left)) { frame_index--; frame_index = clamp(frame_index, 0, MAX_FRAMES - 1); }
		ImGui::SameLine();
		if (ImGui::ArrowButton("##right", ImGuiDir_Right)) { frame_index++; frame_index = clamp(frame_index, 0, MAX_FRAMES - 1); }

		static int frame_index_i32 = 0;
		frame_index_i32 = frame_index;
		if (ImGui::SliderInt("Frame index", &frame_index_i32, 0, MAX_FRAMES - 1)) {
			frame_index = clamp(frame_index_i32, 0, MAX_FRAMES - 1);
		};

		// ── Stats ──────────────────────────────────────────────────────
		ImGui::SeparatorText("Stats");
		ImGui::Text("UI Content: %f ms", elapsed_content.count() * 1000);
		ImGui::Text("Buffer Swap: %f ms", elapsed_buffer.count() * 1000);
		ImGui::Text("Total: %f ms", elapsed_total.count() * 1000);

		ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Debug"))
		{
			// ── GPU Resources ─────────────────────────────────────────────
			ImGui::SeparatorText("GPU Resources");
			bool active_shader_ok = IsNIRMode() ? (g_pComputeShaderNIR != nullptr) : (g_pComputeShader != nullptr);
			ImGui::Text("Active Compute Shader (%s):", IsNIRMode() ? "NIR" : "VIS");
			ImGui::SameLine();
			BitGreen(active_shader_ok, false);
			if (!active_shader_ok) {
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
				                   "Bitpacking shader (%s) failed to compile. See stderr for details.",
				                   IsNIRMode() ? "BitpackHologramsNIR_CS" : "BitpackHologramsCS");
			}
			ImGui::Text("VIS Compute Shader:"); ImGui::SameLine(); BitGreen(g_pComputeShader != nullptr, false);
			ImGui::Text("NIR Compute Shader:"); ImGui::SameLine(); BitGreen(g_pComputeShaderNIR != nullptr, false);
			ImGui::Text("Constant Buffer:"); ImGui::SameLine(); BitGreen(g_pConstantBuffer != nullptr, false);
			ImGui::Text("Phase Buffer:"); ImGui::SameLine(); BitGreen(g_pPhaseBuffer != nullptr, false);
			ImGui::Text("Phase Map Buffer:"); ImGui::SameLine(); BitGreen(g_pPhaseMapBuffer != nullptr, false);
			ImGui::Text("Hologram Texture:"); ImGui::SameLine(); BitGreen(pHologramTexture != nullptr, false);
			ImGui::Text("Hologram UAV:"); ImGui::SameLine(); BitGreen(g_pHologramUAV != nullptr, false);
			ImGui::Text("Staging Texture:"); ImGui::SameLine(); BitGreen(pStagingTexture != nullptr, false);

			// Phase map
			const int phase_levels = IsNIRMode() ? 32 : 16;
			const int phase_cells  = IsNIRMode() ? 6 : 4;
			char phase_map_label[64];
			snprintf(phase_map_label, sizeof(phase_map_label), "Phase map [0...%d]:", phase_levels - 1);
			if (ImGui::TreeNode(phase_map_label)) {
				for (int j = 0; j < phase_cells; j++) {
					for (int i = 0; i < phase_levels; i++) {
						Bit(ActivePhaseMap()[i * phase_cells + j], i < (phase_levels - 1));
					};
				};
				ImGui::TreePop();
			};

			// ── Frame Preview ──────────────────────────────────────────────
			ImGui::SeparatorText("Frame Preview");
			if (ImGui::TreeNode("Frame on display##dbg")) {
				static float zoom = 1.0f, pan_x = 0.0f, pan_y = 0.0f;
				render_frame_preview("dbg", zoom, pan_x, pan_y);
				ImGui::TreePop();
			}

			// ── Test Pattern ───────────────────────────────────────────────
			// All 24 holograms share a single N*M phase buffer (same_phase = true).
			ImGui::SeparatorText("Test Pattern");

			static int  test_mode  = 0;     // 0=Level, 1=Half at Level, 2=Blazed grating
			static int  debug_level = 0;
			static int  half_idx   = 0;     // 0=Top, 1=Bottom, 2=Left, 3=Right
			static float kx        = 0.0f;
			static float ky        = 0.0f;

			const char* test_modes[] = { "Level", "Half at Level", "Blazed grating" };
			bool dirty = ImGui::Combo("Pattern", &test_mode, test_modes, IM_ARRAYSIZE(test_modes));

			const int max_level = IsNIRMode() ? 31 : 15;

			if (test_mode == 0 || test_mode == 1) {
				dirty |= ImGui::SliderInt("Phase Level", &debug_level, 0, max_level);
				if (IsNIRMode()) {
					ImGui::Text("Odd col ref: %.4f  |  Even col ref: %.4f",
						ActiveNIRPhasesOdd()[debug_level], ActiveNIRPhasesEven()[debug_level]);
				}
			}
			if (test_mode == 1) {
				const char* halves[] = { "Top", "Bottom", "Left", "Right" };
				dirty |= ImGui::Combo("Half", &half_idx, halves, IM_ARRAYSIZE(halves));
			}
			if (test_mode == 2) {
				dirty |= ImGui::SliderFloat("kx", &kx, -0.1f, 0.1f, "%.4f");
				dirty |= ImGui::SliderFloat("ky", &ky, -0.1f, 0.1f, "%.4f");
			}

			if (dirty) {
				static std::vector<float> single_phase;
				single_phase.resize((size_t)N * M);

				auto level_phase = [&](uint64_t i) -> float {
					if (IsNIRMode()) {
						int col_parity = i % 2;
						const float* lut = (col_parity == 0) ? ActiveNIRPhasesOdd() : ActiveNIRPhasesEven();
						return lut[debug_level];
					}
					return (debug_level + 0.5f) / 16.0f;
				};

				if (test_mode == 0) {
					// Level: uniform fill (per-column for NIR)
					for (uint64_t j = 0; j < (uint64_t)M; j++)
						for (uint64_t i = 0; i < (uint64_t)N; i++)
							single_phase[i + j * N] = level_phase(i);
				}
				else if (test_mode == 1) {
					// Half at level: chosen half pistoned to level, other half phase 0
					const uint64_t halfM = (uint64_t)M / 2;
					const uint64_t halfN = (uint64_t)N / 2;
					for (uint64_t j = 0; j < (uint64_t)M; j++) {
						for (uint64_t i = 0; i < (uint64_t)N; i++) {
							bool in_half = false;
							switch (half_idx) {
								case 0: in_half = (j <  halfM); break;  // Top
								case 1: in_half = (j >= halfM); break;  // Bottom
								case 2: in_half = (i <  halfN); break;  // Left
								case 3: in_half = (i >= halfN); break;  // Right
							}
							single_phase[i + j * N] = in_half ? level_phase(i) : 0.0f;
						}
					}
				}
				else {
					// Blazed grating centred at array centre (where the beam reflects)
					// phase = frac(kx*(i - N/2) + ky*(j - M/2)), kx/ky in cycles per pixel
					const float cx = 0.5f * (float)N;
					const float cy = 0.5f * (float)M;
					for (uint64_t j = 0; j < (uint64_t)M; j++) {
						for (uint64_t i = 0; i < (uint64_t)N; i++) {
							float p = kx * ((float)i - cx) + ky * ((float)j - cy);
							single_phase[i + j * N] = p - floorf(p);
						}
					}
				}

				// BitpackAndInsert*GPU writes the bitpacked frame into slot 0 and
				// captures `single_phase` into phase_set for the Phase preview tab.
				same_thread.store(true);
				if (IsNIRMode())
					BitpackAndInsertNIRGPU(single_phase.data(), N, M, 24, 0, true);
				else
					BitpackAndInsertGPU(single_phase.data(), N, M, 24, 0, true);
				same_thread.store(false);

			}

			// ── Stats ──────────────────────────────────────────────────────
			ImGui::SeparatorText("Stats");
			ImGui::Text("UI Content: %f ms", elapsed_content.count() * 1000);
			ImGui::Text("Buffer Swap: %f ms", elapsed_buffer.count() * 1000);
			ImGui::Text("Total: %f ms", elapsed_total.count() * 1000);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("PLM connection"))
		{

			ImGui::SeparatorText("PLM Status");
			if (PLM::IsConnected() == false) {
				PLM::Open();
				PLM::GetVersion();
			};
			Status(plm_connected);
			plm_connected = PLM::IsConnected();
			ImGui::SameLine();
			if (plm_connected) {
				ImGui::Text("Connected");
			} else {
				ImGui::Text("Not connected");
			};
			ImGui::Text("Firmware version: %d.%d.%d", (PLM::App_ver >> 24), ((PLM::App_ver << 8) >> 24), ((PLM::App_ver << 16) >> 16));
			if ((PLM::App_ver >> 24) == 0 && ((PLM::App_ver << 8) >> 24) == 0 && ((PLM::App_ver << 16) >> 16) == 0) {
				if (plm_connected) {
					ImGui::Text("PLM is connected, but TI's LightCrafter might be open");
				}
			};

			ImGui::BeginDisabled(!plm_connected);


			if (ImGui::TreeNode("PLM State")) {

				static unsigned char HWStatus = 0, SysStatus = 0, MainStatus = 0;
				static bool plm_status_success = true;

				if (plm_updating_status) {
					// Get the status of the PLM
					if (LCR_GetStatus(&HWStatus, &SysStatus, &MainStatus) < 0) {
						plm_status_success = false;
					} else {
						plm_status_success = true;
					}
				};

				ContinuousStatus((float)ui_cycles / 60.0, true);
				if (plm_status_success) ImGui::Text("Monitoring PLM status"); else ImGui::Text("Unable to get PLM status");


				// 1. Internal Memory Test Passed
				bool memTest = SysStatus & (1 << 0);
				Status(memTest);
				ImGui::Text("Internal Memory Test Passed: %s", memTest ? "Yes" : "No");

				// 2. Internal Initialization Complete
				bool initComplete = HWStatus & (1 << 0);
				Status(initComplete);
				ImGui::Text("Internal Initialization Complete: %s", initComplete ? "Yes" : "No");

				// 3. Controller/DMD Incompatible
				bool compatible = !(HWStatus & (1 << 1));
				Status(compatible);
				ImGui::Text("Controller/DMD Incompatible: %s", compatible ? "No" : "Yes");

				// 4. Secondary Present and Ready
				bool slaveReady = HWStatus & (1 << 4);
				Status(slaveReady);
				ImGui::Text("Secondary Present and Ready: %s", slaveReady ? "Yes" : "No");

				// 5. DMD Reset Waveform Controller Error
				bool dmdResetOk = !(HWStatus & (1 << 2));
				Status(dmdResetOk);
				ImGui::Text("DMD Reset Waveform Controller Error: %s", dmdResetOk ? "No" : "Yes");

				// 6. Forced Swap Error
				bool forcedSwapOk = !(HWStatus & (1 << 3));
				Status(forcedSwapOk);
				ImGui::Text("Forced Swap Error: %s", forcedSwapOk ? "No" : "Yes");

				// 7. Sequencer Abort Status Flag Error
				bool seqAbortOk = !(HWStatus & (1 << 6));
				Status(seqAbortOk);
				ImGui::Text("Sequencer Abort Status Flag Error: %s", seqAbortOk ? "No" : "Yes");

				// 8. Sequencer Error
				bool seqErrorOk = !(HWStatus & (1 << 7));
				Status(seqErrorOk);
				ImGui::Text("Sequencer Error: %s", seqErrorOk ? "No" : "Yes");

				// 9. DMD Micromirrors Parked
				bool dmdParked = MainStatus & (1 << 0);
				Status(dmdParked);
				ImGui::Text("DMD Micromirrors Parked: %s", dmdParked ? "Yes" : "No");

				// 10. Sequencer Running
				bool seqRunning = MainStatus & (1 << 1);
				Status(seqRunning);
				ImGui::Text("Sequencer Running: %s", seqRunning ? "Yes" : "No");

				// 11. Video Running
				bool videoRunning = !(MainStatus & (1 << 2));
				Status(videoRunning);
				ImGui::Text("Video Running: %s", videoRunning ? "Yes" : "No");

				// 12. Locked to External Source
				bool extLocked = MainStatus & (1 << 3);
				Status(extLocked);
				ImGui::Text("Locked to External Source: %s", extLocked ? "Yes" : "No");
				ImGui::TreePop();
			};

			//ImGui::SeparatorText("Quick actions");
			//ImGui::Button("Set everything for HDMI connection");

			ImGui::SeparatorText("Connection tests");

			// Get source
			static unsigned int source = 99, portWidth = 99;
			static bool source_problem = 0;
			if (ImGui::Button("Check Source")) {
				source_problem = LCR_GetInputSource(&source, &portWidth) < 0 ? true : false;
			};
			if (source_problem) ImGui::Text("Unable to get Input Source");
			ImGui::SameLine();
			Status(source == 0); ImGui::Text("Parallel RGB");
			ImGui::SameLine();
			Status(portWidth == 1); ImGui::Text("Port Width: %d", portWidth);

			ImGui::Separator();
			static unsigned int port = 0, swap = 99;
			static bool swap_problem = false;
			if (ImGui::Button("Check Port Swap")) {
				swap_problem = LCR_GetDataChannelSwap(port, &swap) < 0 ? true : false;
			};
			if (swap_problem) ImGui::Text("Unable to get Channel Swap Info");
			ImGui::SameLine();
			ImGui::Text("Port: %d, Swap: %d", port, swap);
			ImGui::Separator();


			static API_VideoConnector_t powerMode;
			if (ImGui::Button("Check Conn")) {
				if (LCR_GetIT6535PowerMode(&powerMode) < 0) {};
			}; 
			ImGui::SameLine();
			Status(powerMode == VIDEO_CON_DISABLE); ImGui::Text("Power down"); ImGui::SameLine();
			Status(powerMode == VIDEO_CON_HDMI); ImGui::Text("HDMI"); ImGui::SameLine();
			Status(powerMode == VIDEO_CON_DP); ImGui::Text("DisplayPort");
			ImGui::Separator();


			static API_DisplayMode_t SLmode = PTN_MODE_DISABLE;
			if (ImGui::Button("Check Video Mode")) {
				if (LCR_GetMode(&SLmode) == 0) {};
			};
			Status(SLmode == PTN_MODE_DISABLE); ImGui::Text("Disable Pattern Mode"); 
			Status(SLmode == PTN_MODE_SPLASH); ImGui::Text("Pre-stored Pattern Mode"); 
			Status(SLmode == PTN_MODE_VIDEO); ImGui::Text("Video Pattern Mode"); 
			Status(SLmode == PTN_MODE_OTF); ImGui::Text("Pattern On-The-Fly"); 


			ImGui::SeparatorText("Sequence controls");
			Status(plm_is_displaying);
			if (ImGui::Button("Start")) {
				PLM::Play();
				plm_is_displaying = true;
			};
			ImGui::SameLine();
			if (ImGui::Button("Stop")) {
				PLM::Stop();
				plm_is_displaying = false;
			};

			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();

};



#ifdef PLM_DEBUG
int main() {

	SetPLMWindowPos(904, 800, 2560);
	SetWindowed(true);
	StartUI(MAX_FRAMES);

	return 0;
}
#endif

