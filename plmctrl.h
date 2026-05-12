//#define STATIC_LIB

#ifdef DLL_EXPORTS
#define PLM_API __declspec(dllexport)
#elif defined(STATIC_LIB)
#define PLM_API
#else
#define PLM_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif
	PLM_API void StartUI(unsigned int number_of_frames);
	PLM_API void StopUI();
	PLM_API bool PauseUI();
	PLM_API bool ResumeUI();
	PLM_API bool StartSequence(int number_of_frames);
	PLM_API bool SetPhaseMap(int* new_phase_map);
	PLM_API void SetPLMWindowPos(int width, int height, int x0, int y0);
	PLM_API void SetWindowed(bool windowed_mode);
	PLM_API bool BitpackHolograms(
		float* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	PLM_API bool BitpackHologramsNIR(
		float* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	// NOTE: same_phase has no default arg here — MATLAB's loadlibrary cannot
	// parse C++ default-argument syntax and would produce malformed thunks.
	// Pass `false` explicitly for the legacy behavior (one phase per hologram).
	PLM_API bool BitpackHologramsNIRGPU(
		float* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms,
		bool same_phase);   // true: phase has only N*M floats, shared across all holograms
	PLM_API bool BitpackHologramsGPU(
		float* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms,
		bool same_phase);
	PLM_API bool BitpackAndInsertGPU(
		float* phase,
		unsigned long long N,
		unsigned long long M,
		int num_holograms,
		unsigned long long offset,
		bool same_phase
	);
	PLM_API bool BitpackAndInsertNIRGPU(
		float* phase,
		unsigned long long N,
		unsigned long long M,
		int num_holograms,
		unsigned long long offset,
		bool same_phase
	);
	// Inverse of BitpackHologramsGPU / BitpackHologramsNIRGPU.
	// `frame` is the bitpacked RGBA8 buffer (2N × 2M for VIS, (3N+4) × 2M for NIR).
	// `phase` receives N*M*num_holograms floats — the quantised phase recovered
	// per hologram. VIS returns bin-centre phases ((level + 0.5) / 16);
	// NIR returns the per-column-parity LUT phase (matches what the device produces).
	PLM_API bool UnpackHologramsGPU(
		unsigned char* frame,
		float* phase,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	PLM_API bool UnpackHologramsNIRGPU(
		unsigned char* frame,
		float* phase,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	PLM_API bool SetPhaseMapNIR(int* new_phase_map);
	// Returns 0 for VIS, 1 for NIR. Reflects the type inferred by SetPLMWindowPos
	// (N=1358 → VIS, N=904 → NIR) or the most recent SetPhaseMap*/Bitpack* call.
	PLM_API int GetPLMType();
	PLM_API bool SetFrameSequence(unsigned long long*, unsigned long long length);
	PLM_API bool SetPLMFrame(unsigned long long offset);
	PLM_API bool InsertPLMFrame(unsigned char* frame, unsigned long long num_frames, unsigned long long offset, int type);
	PLM_API void ResetUI();

	// Direct PLM comms

	PLM_API int SetSource(unsigned int source, unsigned int portWidth);
	PLM_API int SetPortSwap(unsigned int port, unsigned int swap);
	PLM_API int SetPortConfig(int connection_type);
	PLM_API int SetConnectionType(int connection_type);
	PLM_API int SetVideoPatternMode();
	PLM_API int UpdateLUT(int play_mode, int connection_type);
	PLM_API int GetVideoPatternMode();
	PLM_API int GetConnectionType();
	PLM_API int Play();
	PLM_API int Stop();
	PLM_API int Open();
	PLM_API int Close();

#ifdef __cplusplus
}
#endif