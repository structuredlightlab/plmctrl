import ctypes
import numpy as np
import time

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

        # PLM USB comms functions
        self.lib.SetSource.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        self.lib.SetSource.restype = ctypes.c_int
        self.lib.SetPortSwap.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        self.lib.SetPortSwap.restype = ctypes.c_int
        self.lib.SetPortConfig.argtypes = [ctypes.c_uint32]
        self.lib.SetPortConfig.restype = ctypes.c_int
        self.lib.SetConnectionType.argtypes = [ctypes.c_int32]
        self.lib.SetConnectionType.restype = ctypes.c_int
        self.lib.SetVideoPatternMode.argtypes = []
        self.lib.SetVideoPatternMode.restype = ctypes.c_int
        self.lib.UpdateLUT.argtypes = [ctypes.c_int32, ctypes.c_int32]
        self.lib.UpdateLUT.restype = ctypes.c_int
        self.lib.GetConnectionType.argtypes = []
        self.lib.GetConnectionType.restype = ctypes.c_int
        self.lib.GetVideoPatternMode.argtypes = []
        self.lib.GetVideoPatternMode.restype = ctypes.c_int
        self.lib.Open.argtypes = []
        self.lib.Open.restype = ctypes.c_int
        self.lib.Close.argtypes = []
        self.lib.Close.restype = ctypes.c_int
        self.lib.Play.argtypes = []
        self.lib.Play.restype = ctypes.c_int
        self.lib.Stop.argtypes = []
        self.lib.Stop.restype = ctypes.c_int
        
    def open(self):
        """Open the PLM connection."""
        res = self.lib.Open()
        if res == -1:
            raise RuntimeError("Failed to open PLM connection (Is LightCrafter open?)")
        return res
    
    def play(self):
        """PLM starts reading from the screen"""
        res = self.lib.Play()
        if res == -1:
            raise RuntimeError("Failed to start the sequence display")
        return res
    def stop(self):
        """PLM stops reading from the screen."""
        res = self.lib.Stop()
        if res == -1:
            raise RuntimeError("Failed to stop the sequence display")
        return res

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

        if len(sequence) < self.MAX_FRAMES:
            raise ValueError(f"sequence length must be {self.MAX_FRAMES}")
        
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

    # New methods for configuration
    def set_source(self, source, port_width):
        """Set the source and port width for the PLM."""
        if not isinstance(source, int) or source < 0:
            raise ValueError("source must be a non-negative integer")
        if not isinstance(port_width, int) or port_width < 0:
            raise ValueError("port_width must be a non-negative integer")
        
        res = self.lib.SetSource(ctypes.c_uint32(source), ctypes.c_uint32(port_width))
        if res == -1:
            raise RuntimeError("SetSource failed")

    def set_port_swap(self, port, swap):
        """Set the port swap configuration."""
        if not isinstance(port, int) or port < 0:
            raise ValueError("port must be a non-negative integer")
        if not isinstance(swap, int) or swap < 0:
            raise ValueError("swap must be a non-negative integer")
        
        res = self.lib.SetPortSwap(ctypes.c_uint32(port), ctypes.c_uint32(swap))
        if res == -1:
            raise RuntimeError("SetPortSwap failed")

    def set_connection_type(self, connection_type):
        """Set the connection type for the PLM (e.g., 0 for disable, 1 for HDMI, 2 for DisplayPort)."""
        if not isinstance(connection_type, int):
            raise ValueError("connection_type must be an integer")
        
        print(f"Setting connection to {connection_type}")
        res = self.lib.SetConnectionType(ctypes.c_int32(connection_type))
        if res == -1:
            raise RuntimeError("SetConnectionType failed")
        
    def set_pixel_mode(self, connection_type):
        """Set pixel mode (e.g., 1 for Single Pixel (HDMI), 2 for Dual Pixel (DisplayPort)."""
        if not isinstance(connection_type, int):
            raise ValueError("connection_type must be an integer")
        
        print(f"Setting pixel mode to {'HDMI' if connection_type == 1 else 'DisplayPort'}")

        res = self.lib.SetPortConfig(ctypes.c_uint32(connection_type))
        if res == -1:
            raise RuntimeError("SetPortConfig failed")

    def set_video_pattern_mode(self):
        """Set the video pattern mode."""
        print("Setting Video Pattern Mode")
        res = self.lib.SetVideoPatternMode()
        if res == -1:
            raise RuntimeError("SetVideoPatternMode failed")

    def update_lut(self, play_mode, connection_type):
        """Update the lookup table with play mode and connection type."""
        if not isinstance(play_mode, int) or play_mode not in [0, 1]:
            raise ValueError("play_mode must be 0 or 1")
        if not isinstance(connection_type, int) or connection_type <= 0 or connection_type > 2:
            raise ValueError("connection_type must be 1 or 2")
        
        print("Updating bit lookup-table")
        res = self.lib.UpdateLUT(ctypes.c_int32(play_mode), ctypes.c_int32(connection_type))
        if res == -1:
            raise RuntimeError("UpdateLUT failed")

    def get_connection_type(self):
        """Get the current connection type."""
        return self.lib.GetConnectionType()

    def get_video_pattern_mode(self):
        """Get the current video pattern mode."""
        return self.lib.GetVideoPatternMode()

    def configure(self, play_mode, connection_type):
        """
        Configure the PLM with the specified play mode and connection type.
        
        Args:
            play_mode (int): 0 or 1 to set the play mode.
            connection_type (int): 1 for HDMI, 2 for DisplayPort.
        """
        if not isinstance(play_mode, int) or play_mode not in [0, 1]:
            raise ValueError("play_mode must be 0 or 1")
        if not isinstance(connection_type, int) or connection_type <= 0 or connection_type > 2:
            raise ValueError("connection_type must be 1 or 2")
        
        # Set source to Parallel RGB (0) and port width to 24 bits (1)
        self.set_source(0, 1)
        
        # Set port swap for ports 0 and 1 to ABC -> ABC (0)
        self.set_port_swap(0, 0)
        self.set_port_swap(1, 0)

        self.set_pixel_mode(connection_type)
        
        # Check and set connection type if not already 1
        if self.get_connection_type() != 1:
            self.set_connection_type(connection_type)
            time.sleep(5.5)  # Wait
        
        # Set video pattern mode
        self.set_video_pattern_mode()
        time.sleep(2.0)  # Wait
        
        # Update LUT with play mode and connection type
        self.update_lut(play_mode, connection_type)

    def cleanup(self):
        """Cleanup and unload the PLM library."""
        self.lib.StopUI()
        # Library unloading is handled by Python at process exit