import ctypes
import numpy as np
import time
import warnings

class PLMController:
    NIR_ALPHA = 0
    NIR_GAMMA = 1
    NIR_VARIANTS = {
        'alpha': NIR_ALPHA,
        'gamma': NIR_GAMMA,
    }
    PLM_MODELS = {
        '.67NIRalpha': (904, 800, NIR_ALPHA),
        '.67NIRgamma': (904, 800, NIR_GAMMA),
        '.67VIS': (1358, 800, None),
    }

    def __init__(self, *args, **kwargs):
        """
        Initialize the PLMController. Two call styles are supported:

        Model-based :
            PLMController(model, dll_path='plmctrl.dll', x0=1920, y0=0, MAX_FRAMES=120)
            where model is one of '.67NIRalpha', '.67NIRgamma', '.67VIS'.

        explicit-dimensions:
            PLMController(MAX_FRAMES, width, height, dll_path='plmctrl.dll', x0=1920, y0=0)
        """
        requested_nir_variant = None

        if args and isinstance(args[0], str):
            model = args[0]
            if model not in self.PLM_MODELS:
                raise ValueError(f"Unknown PLM model {model!r}. Known: {list(self.PLM_MODELS)}")
            self.N, self.M, requested_nir_variant = self.PLM_MODELS[model]
            requested_nir_variant = kwargs.pop('nir_variant', requested_nir_variant)
            dll_path     = args[1] if len(args) > 1 else kwargs.pop('dll_path', 'plmctrl.dll')
            self.x0      = args[2] if len(args) > 2 else kwargs.pop('x0', 1920)
            self.y0      = args[3] if len(args) > 3 else kwargs.pop('y0', 0)
            self.MAX_FRAMES = kwargs.pop('MAX_FRAMES', 120)
        else:
            # Legacy: (MAX_FRAMES, width, height, dll_path, x0, y0)
            self.MAX_FRAMES = args[0]      if len(args) > 0 else kwargs.pop('MAX_FRAMES')
            self.N          = args[1]      if len(args) > 1 else kwargs.pop('width')
            self.M          = args[2]      if len(args) > 2 else kwargs.pop('height')
            dll_path        = args[3]      if len(args) > 3 else kwargs.pop('dll_path', 'plmctrl.dll')
            self.x0         = args[4]      if len(args) > 4 else kwargs.pop('x0', 1920)
            self.y0         = args[5]      if len(args) > 5 else kwargs.pop('y0', 0)
            requested_nir_variant = kwargs.pop('nir_variant', None)

        # Load the 'plmctrl' library
        self.lib = ctypes.CDLL(dll_path)
        
        # Define function prototypes with argtypes and restype
        self.lib.SetPLMWindowPos.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.StartUI.argtypes = [ctypes.c_int]
        self.lib.InsertPLMFrame.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.InsertPLMFrame.restype = ctypes.c_int
        self.lib.SetFrameSequence.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
        self.lib.StartSequence.argtypes = [ctypes.c_int]
        self.lib.SetPLMFrame.argtypes = [ctypes.c_int]
        self.lib.SetPhaseMap.argtypes = [ctypes.POINTER(ctypes.c_int32)]
        self.lib.SetPhaseMapNIR.argtypes = [ctypes.POINTER(ctypes.c_int32)]
        self.lib.SetNIRVariant.argtypes = [ctypes.c_int]
        self.lib.SetWindowed.argtypes = [ctypes.c_bool]
        self.lib.ShowDebugPanel.argtypes = [ctypes.c_bool]
        self.lib.SetPhaseMap.restype = ctypes.c_int
        self.lib.SetPhaseMapNIR.restype = ctypes.c_int
        self.lib.SetNIRVariant.restype = ctypes.c_bool
        self.lib.GetPLMType.argtypes = []
        self.lib.GetPLMType.restype = ctypes.c_int
        self.lib.BitpackHolograms.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8),
                                              ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self.lib.BitpackHologramsNIR.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8),
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_int]
        # GPU variants take an extra `bool same_phase` (true = single N*M phase shared across all holograms)
        self.lib.BitpackHologramsGPU.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8),
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool]
        self.lib.BitpackHologramsNIRGPU.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_uint8),
                                                    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool]
        self.lib.BitpackAndInsertGPU.argtypes = [ctypes.POINTER(ctypes.c_float),
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                                 ctypes.c_bool]
        self.lib.BitpackAndInsertNIRGPU.argtypes = [ctypes.POINTER(ctypes.c_float),
                                                    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                                    ctypes.c_bool]

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

        # Lock in N/M on the DLL side so it can infer PLM type (VIS vs NIR),
        # then read it back. is_nir drives bitpack dispatch and frame shape.
        self.lib.SetPLMWindowPos(self.N, self.M, self.x0, self.y0)
        self.is_nir = bool(self.lib.GetPLMType())
        self.nir_variant = None
        self.nir_variant_name = None
        if self.is_nir:
            variant = self.NIR_ALPHA if requested_nir_variant is None else requested_nir_variant
            self.set_nir_variant(variant)
        elif requested_nir_variant is not None:
            raise ValueError("NIR variant can only be set for NIR PLM models")

    @property
    def frame_shape(self):
        """uint8 RGBA frame shape (height, width) for the current PLM type.
        NIR: (2M, 4*(3N+4)); VIS: (2M, 4*2N)."""
        active_w = (3 * self.N + 4) if self.is_nir else (2 * self.N)
        return (2 * self.M, 4 * active_w)

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

    def start_ui(self):
        """Setup the PLM window on specified coordinates."""
        self.lib.SetPLMWindowPos(self.N, self.M, self.x0, self.y0)
        self.lib.StartUI(self.MAX_FRAMES)
        
    def set_windowed(self, windowed):
        """Set the PLM window to windowed mode -- Good for testing. I suggest using windowed = false for actual experiments"""
        if not isinstance(windowed, bool):
            raise ValueError("windowed must be a boolean value")
    
        self.lib.SetWindowed(windowed)

    def show_debug_panel(self, show):
        """Show or hide the debug panel in the UI."""
        if not isinstance(show, bool):
            raise ValueError("show must be a boolean value")

        self.lib.ShowDebugPanel(show)

    @classmethod
    def _normalize_nir_variant(cls, variant):
        if isinstance(variant, bool):
            raise ValueError("NIR variant must be 'alpha', 'gamma', 0, or 1")
        if isinstance(variant, int):
            variant_id = variant
        elif isinstance(variant, str):
            key = variant.strip().lower().replace(' ', '').replace('_', '').replace('-', '')
            if key not in cls.NIR_VARIANTS:
                raise ValueError("NIR variant must be 'alpha', 'gamma', 0, or 1")
            variant_id = cls.NIR_VARIANTS[key]
        else:
            raise ValueError("NIR variant must be 'alpha', 'gamma', 0, or 1")

        if variant_id not in (cls.NIR_ALPHA, cls.NIR_GAMMA):
            raise ValueError("NIR variant must be 'alpha', 'gamma', 0, or 1")
        return variant_id

    def set_nir_variant(self, variant):
        """Set the NIR PLM variant: 'alpha' uses odd/even column LUTs; 'gamma' uses one shared LUT."""
        if not self.is_nir:
            raise ValueError("NIR variant can only be set for NIR PLM models")

        variant_id = self._normalize_nir_variant(variant)
        res = self.lib.SetNIRVariant(ctypes.c_int(variant_id))
        if not res:
            raise ValueError("NIR variant must be 'alpha', 'gamma', 0, or 1")

        self.nir_variant = variant_id
        self.nir_variant_name = 'gamma' if variant_id == self.NIR_GAMMA else 'alpha'
        return bool(res)

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
        

    def set_frame(self, frame):
        """Set a specific frame to display."""
        if not isinstance(frame, int) or frame < 0:
            raise ValueError("frame must be a non-negative integer")
        
        self.lib.SetPLMFrame(frame)

    def set_phase_map(self, phase_map):
        """Set the VIS phase map (16 levels x 4 cells = 64 ints)."""
        if not isinstance(phase_map, np.ndarray) or not np.issubdtype(phase_map.dtype, np.integer) or phase_map.ndim != 2:
            raise ValueError("phase_map must be a 2D numpy array of integers")

        if not phase_map.flags['C_CONTIGUOUS']:
            phase_map = np.ascontiguousarray(phase_map)

        phase_map_ptr = phase_map.astype(np.int32).ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
        res = self.lib.SetPhaseMap(phase_map_ptr)
        return res

    def set_phase_map_nir(self, phase_map):
        """Set the NIR phase map (32 levels x 6 cells = 192 ints)."""
        if not isinstance(phase_map, np.ndarray) or not np.issubdtype(phase_map.dtype, np.integer) or phase_map.ndim != 2:
            raise ValueError("phase_map must be a 2D numpy array of integers")

        if not phase_map.flags['C_CONTIGUOUS']:
            phase_map = np.ascontiguousarray(phase_map)

        phase_map_ptr = phase_map.astype(np.int32).ctypes.data_as(ctypes.POINTER(ctypes.c_int32))
        res = self.lib.SetPhaseMapNIR(phase_map_ptr)
        return res

    def bitpack_holograms(self, phase):
        """Create and bit-pack holograms from phase data (CPU). Dispatches VIS/NIR by self.is_nir."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32 or phase.ndim != 3:
            raise ValueError("phase must be a 3D numpy array of float32")
        # Tolerate small fp drift from upstream normalization (e.g. mod(x, 2*pi)/(2*pi))
        phase = np.clip(phase, 0.0, 1.0)

        num_patterns = phase.shape[0]
        frame = np.zeros(self.frame_shape, dtype=np.uint8)

        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        frame_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))

        fn = self.lib.BitpackHologramsNIR if self.is_nir else self.lib.BitpackHolograms
        fn(phase_ptr, frame_ptr, self.N, self.M, num_patterns)
        return frame

    def bitpack_holograms_gpu(self, phase, same_phase=False):
        """Create and bit-pack holograms on the GPU. Dispatches VIS/NIR by self.is_nir.

        If same_phase=True, `phase` is a single N*M frame shared across all 24 holograms.
        Otherwise it is a (num_patterns, M, N) stack."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32:
            raise ValueError("phase must be a numpy array of float32")
        # Tolerate small fp drift from upstream normalization (e.g. mod(x, 2*pi)/(2*pi))
        phase = np.clip(phase, 0.0, 1.0)

        if same_phase:
            if phase.ndim != 2:
                raise ValueError("with same_phase=True, phase must be a 2D (M, N) array")
            num_patterns = 24
        else:
            if phase.ndim != 3:
                raise ValueError("phase must be a 3D numpy array of float32")
            num_patterns = phase.shape[0]

        frame = np.zeros(self.frame_shape, dtype=np.uint8)

        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        frame_ptr = frame.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))

        fn = self.lib.BitpackHologramsNIRGPU if self.is_nir else self.lib.BitpackHologramsGPU
        fn(phase_ptr, frame_ptr, self.N, self.M, num_patterns, same_phase)
        return frame

    def bitpack_holograms_gpu_ptr(self, phase_ptr, frame_ptr, num_patterns, same_phase=False):
        """Create and bit-pack holograms on the GPU using user-provided pointers. Dispatches VIS/NIR by self.is_nir."""
        if not isinstance(phase_ptr, ctypes.POINTER(ctypes.c_float)):
            raise ValueError("phase_ptr must be a pointer to a float32 array")
        if not isinstance(frame_ptr, ctypes.POINTER(ctypes.c_uint8)):
            raise ValueError("frame_ptr must be a pointer to a uint8 array")
        if not isinstance(num_patterns, int) or num_patterns <= 0:
            raise ValueError("num_patterns must be a positive integer")

        fn = self.lib.BitpackHologramsNIRGPU if self.is_nir else self.lib.BitpackHologramsGPU
        return fn(phase_ptr, frame_ptr, self.N, self.M, num_patterns, same_phase)

    def bitpack_and_insert_gpu(self, phase, offset, same_phase=False):
        """Bitpack on GPU and insert directly into the PLM frame buffer at `offset`. Dispatches VIS/NIR by self.is_nir."""
        if not isinstance(phase, np.ndarray) or phase.dtype != np.float32:
            raise ValueError("phase must be a numpy array of float32")
        # Tolerate small fp drift from upstream normalization (e.g. mod(x, 2*pi)/(2*pi))
        phase = np.clip(phase, 0.0, 1.0)
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a non-negative integer")

        if same_phase:
            if phase.ndim != 2:
                raise ValueError("with same_phase=True, phase must be a 2D (M, N) array")
            num_patterns = 24
        else:
            if phase.ndim != 3:
                raise ValueError("phase must be a 3D numpy array of float32")
            num_patterns = phase.shape[0]

        phase_ptr = phase.ctypes.data_as(ctypes.POINTER(ctypes.c_float))

        fn = self.lib.BitpackAndInsertNIRGPU if self.is_nir else self.lib.BitpackAndInsertGPU
        return fn(phase_ptr, self.N, self.M, num_patterns, offset, same_phase)

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
