# <img src="https://github.com/user-attachments/assets/92c3cc2b-c4f1-4ed0-b876-7b01cac2bc67" alt="logo" width="24"/> PLMCtrl -- Updated on 20/May/2025

![Warning](https://img.shields.io/badge/under%20development%20-yellow)
[![arXiv](https://img.shields.io/badge/arXiv-2409.01289-green.svg)](https://arxiv.org/abs/2409.01289)
[![Journal](https://img.shields.io/badge/Optics-Express-green.svg)](https://opg.optica.org/oe/fulltext.cfm?uri=oe-32-24-43300&id=563432)
![Warning](https://img.shields.io/badge/version-0.5.0b-red)
<div style="display: flex; align-items: center;">
    <div>
        <p align="center">
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Getting-Started">🚀Getting Started</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Docs">📚Docs</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Code-and-Compiling">🔄Compiling</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki">🌐Wiki </a>
        </p>
        <p align="center">
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Known-issues">⚠️Known issues</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Experiment">🧪Experiment</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/LightCrafterDLP900-configuration">⚙️LightCrafterDLP900 configuration</a> ·  
            <a href="https://github.com/structuredlightlab/plmctrl?tab=readme-ov-file#contact"> 📧Contact </a>
        </p>
    </div>
</div>

<img src="https://github.com/user-attachments/assets/617a1f9a-7be8-4c00-a289-ed66f41fd31b" alt="mirrors_low" style="width: 100%; margin-right: 20px;">

```plmctrl``` is an open source library for controlling the 0.67" (DLP6750 EVM) Texas-Instruments Phase-only Light Modulator (PLM). This library is a C++/DirectX code that handles the whole process from a matrix of ***continuous phase values*** → ***prepare the hologram*** → ***display the hologram on the screen***. When displaying different holograms in a sequence, frame-pacing is also ensured.

If you have used ```plmctrl``` in a scientific publication, we would appreciate citation to the following reference:
```bibtex
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
```

The Texas Instruments Phase-only Light Modulator is a MEMS-based Spatial Light Modulator. It's an array of µ-mirrors that electrostatically piston up and down and can change the phase of the reflected light on a pixel-by-pixel basis. 
There are three PLM models out there, with diagonal screen sizes of 0.47", 0.67", and 0.98". Here we are providing supporting code for the 0.67" PLM Evaluation Module (EVM).

The PLM builds on top of existing DLP technology, and the 0.67" PLM (DLP6750 EVM) is driven by TI's [DLP670S](https://www.ti.com/product/DLP670S) board and contains an array of 1358 x 800 µ-mirrors that pistons at 16 different heights. The PLM can reconfigure the heights of all mirrors at rates up to 1440 Hz.
While DLP670S has some internal flash memory to store and display a few holograms, it's usually not enough for a whole optics experiment, so, the main way of using this PLM is through a video interface. Here, we provide code for using the PLM through its video interface. We provide functions for creating the holograms, bitpacking 24 holograms into a single frame, displaying that frame to the PLM screen, and changing that frame without frame-drops to ensure maximum hologram-rate.

The PLM offers two mode of connections, HDMI and DisplayPort. Connected through the HDMI, the PLM can display different holograms at a rate of 720 Hz, and, through the DisplayPort 1440 Hz. It is reported that storing patterns in the DLP670S's internal flash memory can acheive hologram rates up to 5.76 kHz, but this operation mode is not documented.

How does the code look? In MATLAB, operation is

```MATLAB
MAX_FRAMES = 90; % Each contains 24 holograms
N = 1358;
M = 800;
x0 = 1920; y0 = 0; (Indicates where your PLM virtual monitor is relative to your main monitor)
% Create a PLMController instance
plm = PLMController(MAX_FRAMES, N, M, x0, y0);

% Start the PLM screen
plm.StartUI(1);  % First monitor = 1 (adjust if needed)

% Configure the PLM (Run only once per boot sequence)
Continuous = 1;
HDMI = 1;
plm.Configure(Continuous, HDMI);

% ---- Stuff ---- 
[x, y] = meshgrid(linspace(-1,1,M), linspace(-M/N,M/N,N));
wedge = @(alpha, beta) alpha*x + beta*y;

% ---- Generates 24 normalized phase profiles ----
numHolograms = 24;
phase = zeros(N, M, numHolograms); 
for i = 1:numHolograms
    alpha = 10*(rand()-0.5);
    beta = 10*(rand()-0.5);
    phase(:,:,i) = mod(wedge(alpha, beta), 2*pi)/(2*pi);
end

% ---- Bitpacks 24 holograms into a single RGB frame ---- 
frame = plm.BitpackHolograms(phase);

% ---- Uploads a bitpacked hologram to the PLM ----
offset = 0;
format = 1; % RGBA = 1, RGB = 0
plm.InsertFrames(frame, offset, format);

% If you need to set a sequence or start it, you can do so:
plm.SetHologramSequence(0:MAX_FRAMES-1);  % Set to display the bitpacked holograms in a sequence
plm.StartSequence(MAX_FRAMES);  % Display MAX_HOLOGRAMS hologram in a sequence following the sequence order

% When you're done:
plm.Cleanup();
```

## External Code/Libraries/used by PLMCtrl
* [Dear ImGui](https://github.com/ocornut/imgui) for GUI handling and wrapping graphics API
* [hidapi](https://github.com/libusb/hidapi) for USB communication with the PLM
* Microsoft's DirectX 11 as Graphics API

## Contact
* PLMCtrl is developed and maintained by José C. A. Rocha and Terry Wright
* This library is actively being developed, so users may encounter some rough edges
* For any feedback, questions or other enquiries, feel free to contact us directly, or open an issue here on GitHub










