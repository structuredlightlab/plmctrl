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
	PLM_API bool StartSequence(int number_of_frames);
	PLM_API bool SetPhaseMap(int* new_phase_map);
	PLM_API void SetSLMWindowPos(int width, int height, int monitor);
	PLM_API bool CreateHolograms(
		double* phase,
		unsigned char* frame,
		unsigned long long N,
		unsigned long long M,
		int num_holograms);
	PLM_API void SetLookupTable(double*);
	PLM_API bool SetFrameSequence(unsigned long long*, unsigned long long length);
	PLM_API bool SetSLMHologram(unsigned long long offset);
	PLM_API bool InsertSLMHologram(unsigned char* frame, unsigned long long num_frame, unsigned long long offset, int type);
	PLM_API int QueryPLMMode();
	PLM_API bool QueryCameraTrigger();
	PLM_API long int QueryBufferIndex();
	PLM_API long long GetT0();
	PLM_API void ResetUI();

#ifdef __cplusplus
}
#endif