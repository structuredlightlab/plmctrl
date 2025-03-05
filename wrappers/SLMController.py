import ctypes
import numpy as np

class SLMController:
    def __init__(self, MAX_HOLOGRAMS, width, height, dll_path='slmctrl.dll'):
        """
        Initialize the SLMController to manage a spatial light modulator.

        Args:
            MAX_HOLOGRAMS (int): Maximum number of holograms the SLM can handle.
            width (int): Width of the SLM display in pixels.
            height (int): Height of the SLM display in pixels.
        """
        self.MAX_HOLOGRAMS = MAX_HOLOGRAMS
        self.N = width
        self.M = height

        # Load the 'SLMctrl' library (assuming it's the Python equivalent of 'slmctrl')
        self.lib = ctypes.CDLL(dll_path)  # Adjust path as needed

        # Define function prototypes to match expected library calls
        self.lib.SetSLMWindowPos.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_bool]
        self.lib.StartUI.argtypes = [ctypes.c_int]
        self.lib.InsertSLMHologram.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int]
        self.lib.SetHologramSequence.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
        self.lib.StartSequence.argtypes = [ctypes.c_int]
        self.lib.StopUI.argtypes = []
        self.lib.SetSLMHologram.argtypes = [ctypes.c_int]
        self.lib.ResetUI.argtypes = []

    def StartUI(self, monitor_id, windowed = False):
        """Start the SLM UI on a specified monitor."""
        if not isinstance(monitor_id, int) or monitor_id < 0:
            raise ValueError("monitor_id must be a non-negative integer")
        self.lib.SetSLMWindowPos(self.N, self.M, monitor_id, windowed)
        self.lib.StartUI(self.MAX_HOLOGRAMS)

    def InsertHolograms(self, holograms, offset):
        """Insert holograms into the SLM memory."""
        if not isinstance(holograms, np.ndarray) or holograms.dtype != np.uint8:
            raise ValueError("holograms must be a numpy array of uint8")
        if holograms.ndim not in [2, 3]:
            raise ValueError("holograms must be 2D or 3D")
        if not isinstance(offset, int) or offset < 0:
            raise ValueError("offset must be a non-negative integer")
        
        holograms_ptr = holograms.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        num_holograms = holograms.shape[2] if holograms.ndim == 3 else 1
        return self.lib.InsertSLMHologram(holograms_ptr, num_holograms, offset)

    def SetHologramSequence(self, sequence):
        """Set the sequence of holograms to display."""
        if not isinstance(sequence, np.ndarray) or not np.issubdtype(sequence.dtype, np.integer) or np.any(sequence < 0):
            raise ValueError("sequence must be a numpy array of non-negative integers")
        sequence_ptr = sequence.ctypes.data_as(ctypes.POINTER(ctypes.c_uint64))
        self.lib.SetHologramSequence(sequence_ptr, len(sequence))

    def StartSequence(self, holograms_to_display):
        """Start displaying the hologram sequence."""
        if not isinstance(holograms_to_display, int) or holograms_to_display <= 0:
            raise ValueError("holograms_to_display must be a positive integer")
        return self.lib.StartSequence(holograms_to_display)

    def StopUI(self):
        """Stop the SLM UI."""
        self.lib.StopUI()

    def SetHologram(self, hologram_index):
        """Set a specific hologram to display."""
        if not isinstance(hologram_index, int) or hologram_index < 0:
            raise ValueError("hologram_index must be a non-negative integer")
        return self.lib.SetSLMHologram(hologram_index)

    def ResetUI(self):
        """Reset the SLM UI."""
        self.lib.ResetUI()

    def Cleanup(self):
        """Stop the SLM UI and cleanup."""
        self.lib.StopUI()
        # Library unloading is automatic in Python when the process exits