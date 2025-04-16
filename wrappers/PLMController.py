import ctypes
import numpy as np

class PLMController:
    def __init__(self, MAX_FRAMES, width, height, dll_path='plmctrl.dll', x0 = 1920, y0 = 0 ):
        """
        Initialize the PLMController.

        Args:
            MAX_FRAMES (int): Maximum number of frames the PLM can handle.
            width (int): Width of the PLM display in pixels.
            height (int): Height of the PLM display in pixels.
            dll_path (str): Path to the plmctrl.dll file (default: 'plmctrl.dll').
            x0 (int): X-coordinate of the PLM window position (default: 1920). -- Top left corner of your main monitor is ( 0, 0 ), x0 and y0 are relative to that
            y0 (int): Y-coordinate of the PLM window position (default: 0).
        """
        self.MAX_FRAMES = MAX_FRAMES
        self.N = width
        self.M = height
        self.x0 = x0
        self.y0 = y0
        
        # Load the 'plmctrl' library
        self.lib = ctypes.CDLL(dll_path)
        
        # Define function prototypes with argtypes and restype
        self.lib.SetPLMWindowPos.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.StartUI.argtypes = [ctypes.c_int]
        self.lib.InsertPLMFrame.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.InsertPLMFrame.restype = ctypes.c_int
        self.lib.SetFrameSequence.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
        self.lib.StartSequence.argtypes = [ctypes.c_int]
        self.lib.SetLookupTable.argtypes = [ctypes.POINTER(ctypes.c_float)]
        self.lib.SetPLMFrame.argtypes = [ctypes.c_int]
        self.lib.SetPhaseMap.argtypes = [ctypes.POINTER(ctypes.c_int32)]
        self.lib.SetWindowed.argtypes = [ctypes.c_bool]
        self.lib.SetPhaseMap.restype = ctypes.c_int
        self.lib.BitpackHolograms.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8), 
                                              ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.BitpackHologramsGPU.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8), 
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.BitpackAndInsertGPU.argtypes = [ctypes.POINTER(ctypes.c_float), 
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]

    def start_ui(self, monitor_id):
        """Setup the PLM window on a specified monitor."""
        if not isinstance(monitor_id, int) or monitor_id <= 0:
            raise ValueError("monitor_id must be a positive integer")
        
        self.lib.SetPLMWindowPos(self.N, self.M, monitor_id, self.x0, self.y0)
        self.lib.StartUI(self.MAX_FRAMES)
        
    def set_windowed(self, windowed):
        """Set the PLM window to windowed mode -- Good for testing. I suggest using windowed = false for actual experiments"""
        if not isinstance(windowed, bool):
            raise ValueError("windowed must be a boolean value")
    
        self.lib.SetWindowed(windowed)

    def insert_frames(self, frames, offset, format):
        """
        Insert RGB bitpacked hologram frames into the plmctrl's sequence.
        
        Parameters:
            frames: 2D or 3D numpy array (dtype=np.uint8). 
                    - If 2D: a single frame of shape (H, W)
                    - If 3D: multiple frames of shape (F, H, W)
            offset: Index in the PLM sequence to start inserting at (int)
            format: 0 for RGB, 1 for RGBA (int)
        """

        if not isinstance(frames, np.ndarray) or frames.dtype != np.uint8:
            raise ValueError("frames must be a uint8 numpy array")
        
        if frames.ndim == 2:
            # Convert 2D to 3D with a single frame
            frames = frames[np.newaxis, :, :]
        elif frames.ndim != 3:
            raise ValueError("frames must be either a 2D or 3D numpy array")

        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a non-negative integer")
        if not isinstance(format, int):
            raise ValueError("format must be an integer")

        frames_ptr = frames.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        num_frames = frames.shape[0]

        res = self.lib.InsertPLMFrame(frames_ptr, num_frames, offset, format)
        return res

    def set_frame_sequence(self, sequence):
        """Set the sequence of frames for display."""
        if not isinstance(sequence, np.ndarray) or not np.issubdtype(sequence.dtype, np.integer) or sequence.ndim != 1:
            raise ValueError("sequence must be a 1D numpy array of integers")
        if np.any(sequence < 0):
            raise ValueError("sequence elements must be non-negative")
        
        if not sequence.flags['C_CONTIGUOUS']:
            sequence = np.ascontiguousarray(sequence)
        
        sequence_ptr = sequence.astype(np.uint64).ctypes.data_as(ctypes.POINTER(ctypes.c_uint64))
        self.lib.SetFrameSequence(sequence_ptr, len(sequence))

    def start_sequence(self, holograms_to_display):
        """Start displaying the sequence of frames."""
        if not isinstance(holograms_to_display, int) or holograms_to_display <= 0:
            raise ValueError("holograms_to_display must be a positive integer")
        
        self.lib.StartSequence(holograms_to_display)

    def pause_ui(self):
        """Pause the PLM UI."""
        self.lib.PauseUI()
        
    def resume_ui(self):
        """Pause the PLM UI."""
        self.lib.ResumeUI()

    def stop_ui(self):
        """Stop the PLM UI."""
        self.lib.StopUI()
        
    def play(self):
        """Equivalent to pressing the Play button on PLM UI."""
        self.lib.Play()
        
    def stop(self):
        """Equivalent to pressing the Stop button on PLM UI."""
        self.lib.Stop()

    def set_lookup_table(self, phase_levels):
        """Set the lookup table for phase levels."""
        if not isinstance(phase_levels, np.ndarray) or phase_levels.dtype != np.float32 or phase_levels.ndim != 1:
            raise ValueError("phase_levels must be a 1D numpy array of float32")
        if np.any(phase_levels < 0) or np.any(phase_levels > 1):
            raise ValueError("phase_levels must be between 0 and 1")
        
        if not phase_levels.flags['C_CONTIGUOUS']:
            phase_levels = np.ascontiguousarray(phase_levels)
        
        phase_levels_ptr = phase_levels.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        self.lib.SetLookupTable(phase_levels_ptr)

    def set_frame(self, frame):
        """Set a specific frame to display."""
        if not isinstance(frame, int) or frame < 0:
            raise ValueError("frame must be a non-negative integer")
        
        self.lib.SetPLMFrame(frame)

    def set_phase_map(self, phase_map):
        """Set the phase map for holograms."""
        if not isinstance(phase_map, np.ndarray) or not np.issubdtype(phase_map.dtype, np.integer) or phase_map.ndim != 2:
            raise ValueError("phase_map must be a 2D numpy array of integers")
        
        if not phase_map.flags['C_CONTIGUOUS']:
            phase_map = np.ascontiguousarray(phase_map)
        
        phase_map_ptr = phase_map.astype(np.int32).ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
        res = self.lib.SetPhaseMap(phase_map_ptr)
        return res

    def bitpack_holograms(self, phase):
        """Create and bit-pack holograms from phase data."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32 or phase.ndim != 3:
            raise ValueError("phase must be a 3D numpy array of float32")
        if np.any(phase < 0) or np.any(phase > 1):
            raise ValueError("phase values must be between 0 and 1")
        
        num_patterns = phase.shape[0]
        frame = np.zeros((2 * self.M, 4 * 2 * self.N), dtype=np.uint8)
        
        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        frame_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        
        self.lib.BitpackHolograms(phase_ptr, frame_ptr, self.N, self.M, num_patterns)
        return frame
    
    def bitpack_holograms_gpu(self, phase):
        """Create and bit-pack holograms from phase data. This function uses compute shaders and runs on the GPU."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32 or phase.ndim != 3:
            raise ValueError("phase must be a 3D numpy array of float32")
        if np.any(phase < 0) or np.any(phase > 1):
            raise ValueError("phase values must be between 0 and 1")

        num_patterns = phase.shape[0]
        frame = np.zeros((2 * self.M, 4 * 2 * self.N), dtype=np.uint8)
        
        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        frame_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        
        self.lib.BitpackHologramsGPU(phase_ptr, frame_ptr, self.N, self.M, num_patterns)

        return frame
    
    def bitpack_holograms_gpu_ptr(self, phase_ptr, frame_ptr, num_patterns):
        """Create and bit-pack holograms from phase data. This function uses compute shaders and runs on the GPU."""
        if not isinstance(phase_ptr, ctypes.POINTER(ctypes.c_float)):
            raise ValueError("phase_ptr must be a pointer to a float32 array")
        if not isinstance(frame_ptr, ctypes.POINTER(ctypes.c_uint8)):
            raise ValueError("frame_ptr must be a pointer to a uint8 array")
        if not isinstance(num_patterns, int) or num_patterns <= 0:
            raise ValueError("num_patterns must be a positive integer")
        
        res = self.lib.BitpackHologramsGPU(phase_ptr, frame_ptr, self.N, self.M, num_patterns)
        return res
    
    def bitpack_and_insert_gpu(self, phase, offset):
        """Create and bit-pack holograms from phase data. This function uses compute shaders and runs on the GPU."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32 or phase.ndim != 3:
            raise ValueError("phase must be a 3D numpy array of float32")
        if np.any(phase < 0) or np.any(phase > 1):
            raise ValueError("phase values must be between 0 and 1")
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a non-negative integer")
        
        num_patterns = phase.shape[0]
        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        
        res = self.lib.BitpackAndInsertGPU(phase_ptr, self.N, self.M, num_patterns, offset)
        return res

    def cleanup(self):
        """Cleanup and unload the PLM library."""
        self.lib.StopUI()
        # Library unloading is handled by Python at process exit

# Usage example:
# plm = PLMController(1000, 1358, 800, r'C:\dev\plmctrl\plmctrl.dll')
# plm.start_ui(1)
# plm.cleanup()