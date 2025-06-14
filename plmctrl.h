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
	PLM_API bool BitpackHologramsGPU(
		float* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	PLM_API bool BitpackAndInsertGPU(
		float* phase,
		unsigned long long N,
		unsigned long long M,
		int num_holograms,
		unsigned long long offset
	);
	PLM_API void SetLookupTable(float* lut);
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