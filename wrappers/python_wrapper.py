import ctypes
import numpy as np

class PLMController:
    def __init__(self, MAX_FRAMES, width, height):
        """
        Initialize the PLMController.

        Args:
        MAX_FRAMES (int): The maximum number of frames that the PLM can handle.
        width (int): Width of the PLM display in pixels.
        height (int): Height of the PLM display in pixels.
        """
        self.MAX_FRAMES = MAX_FRAMES
        self.N = width
        self.M = height
        
        # Load the 'plmctrl' library
        self.lib = ctypes.CDLL(r'C:\dev\plmctrl\plmctrl.dll')  # Adjust the filename as necessary
        
        # Define function prototypes
        self.lib.SetPLMWindowPos.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.StartUI.argtypes = [ctypes.c_int]
        self.lib.InsertPLMFrame.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int]
        self.lib.SetFrameSequence.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
        self.lib.StartSequence.argtypes = [ctypes.c_int]
        self.lib.SetLookupTable.argtypes = [ctypes.POINTER(ctypes.c_double)]
        self.lib.SetPLMFrame.argtypes = [ctypes.c_int]
        self.lib.SetPhaseMap.argtypes = [ctypes.POINTER(ctypes.c_int32)]
        self.lib.BitpackHolograms.argtypes = [ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_uint8), 
                                              ctypes.c_int, ctypes.c_int, ctypes.c_int]

    def start_ui(self, monitor_id):
        """Setup the PLM window on a specified monitor."""
        if not isinstance(monitor_id, int) or monitor_id <= 0:
            raise ValueError("monitor_id must be a positive integer")
        
        self.lib.SetPLMWindowPos(self.N, self.M, monitor_id)
        self.lib.StartUI(self.MAX_FRAMES)

    def insert_frames(self, frames, offset):
        # """Insert hologram frames into the PLM."""
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a non-negative integer")
        
        frames_ptr = frames.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        if frames.ndim == 3:
            self.lib.InsertPLMFrame(frames_ptr, frames.shape[2], offset)
        elif frames.ndim == 2:
            self.lib.InsertPLMFrame(frames_ptr, 1, offset)

    def set_frame_sequence(self, sequence):
        """Set the sequence of frames for display."""
        if not isinstance(sequence, np.ndarray) or not np.issubdtype(sequence.dtype, np.integer):
            raise ValueError("sequence must be a numpy array of integers")
        
        sequence_ptr = sequence.ctypes.data_as(ctypes.POINTER(ctypes.c_uint64))
        self.lib.SetFrameSequence(sequence_ptr, len(sequence))

    def start_sequence(self, holograms_to_display):
        """Start displaying the sequence of frames."""
        if not isinstance(holograms_to_display, int) or holograms_to_display <= 0:
            raise ValueError("holograms_to_display must be a positive integer")
        
        self.lib.StartSequence(holograms_to_display)

    def stop_ui(self):
        """Stop the PLM UI."""
        self.lib.StopUI()

    def set_lookup_table(self, phase_levels):
        """Set the lookup table for phase levels."""
        if not isinstance(phase_levels, np.ndarray) or not np.issubdtype(phase_levels.dtype, np.floating):
            raise ValueError("phase_levels must be a numpy array of floats")
        if np.any(phase_levels < 0) or np.any(phase_levels > 1):
            raise ValueError("phase_levels must be between 0 and 1")
        
        phase_levels_ptr = phase_levels.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        self.lib.SetLookupTable(phase_levels_ptr)

    def set_frame(self, frame):
        """Set a specific frame to display."""
        if not isinstance(frame, int) or frame < 0:
            raise ValueError("frame must be a non-negative integer")
        
        self.lib.SetPLMFrame(frame)

    def set_phase_map(self, phase_map):
        """Set the phase map for holograms."""
        if not isinstance(phase_map, np.ndarray) or not np.issubdtype(phase_map.dtype, np.integer):
            raise ValueError("phase_map must be a 2D numpy array of integers")
        
        phase_map_ptr = phase_map.T.ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
        self.lib.SetPhaseMap(phase_map_ptr)

    def bitpack_holograms(self, phase):
        """Create and bit-pack holograms from phase data."""
        if not isinstance(phase, np.ndarray) or not np.issubdtype(phase.dtype, np.floating):
            raise ValueError("phase must be a 3D numpy array of floats")
        if np.any(phase < 0) or np.any(phase > 1):
            raise ValueError("phase values must be between 0 and 1")
        
        num_patterns = phase.shape[2]
        frame = np.zeros((3*2*self.N, 2*self.M), dtype=np.uint8)
        
        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_double))
        frame_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        
        self.lib.BitpackHolograms(phase_ptr, frame_ptr, self.N, self.M, num_patterns)
        
        return frame

    def cleanup(self):
        """Cleanup and unload the PLM library."""
        self.lib.StopUI()
        # Note: Python doesn't have a direct equivalent to MATLAB's unloadlibrary
        # The library will be automatically unloaded when the Python process exits

# Usage example:
# plm = PLMController(MAX_FRAMES, width, height)
# plm.start_ui(monitor_id)
# ... (use other methods as needed)
# plm.cleanup()