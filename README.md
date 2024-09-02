# <img src="https://github.com/user-attachments/assets/38bb9f3a-5c2b-4155-9335-3d829c6316a8" alt="logo" width="24"/> PLMCtrl 
![Warning](https://img.shields.io/badge/under%20development%20-yellow)
[![arXiv](https://img.shields.io/badge/arXiv-<INDEX>-<COLOR>.svg)](https://arxiv.org/abs/<INDEX>)
![Warning](https://img.shields.io/badge/version-0.1a-red)
<div style="display: flex; align-items: center;">
    <div>
        <p align="center">
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Getting-Started">🚀 Getting Started</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Docs">📚 Docs</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Code-and-Compiling">🔄 Compiling</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki">🌐 Wiki </a>
        </p>
        <p align="center">
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/Experiment">🧪 Doing an experiment</a> · 
            <a href="https://github.com/structuredlightlab/plmctrl/wiki/LightCrafterDLP900-configuration">⚙️ LightCrafterDLP900 configuration</a> ·  
            <a href="https://github.com/structuredlightlab/plmctrl?tab=readme-ov-file#contact"> 📧 Contact </a>
        </p>
    </div>
</div>
<img src="https://github.com/user-attachments/assets/983ca1fc-07a2-4e17-a60a-57ec77a5ee70" alt="mirrors_low" style="width: 100%; margin-right: 20px;">

```plmctrl``` is an open source library for controlling the 0.67" (DLP6750 EVM) Texas-Instruments Phase-only Light Modulator (PLM). This library is a C++/DirectX code that handles the whole process from a matrix of ***continous phase values*** → ***prepare the hologram*** → ***display the hologram on the screen***. When displaying different holograms in a sequence, frame-pacing is also ensured.

If you have used ```plmctrl``` in a scientific publication, we would appreciate citation to the following reference:
```bibitex
@article{joserocha,
 title         = {Fast and light-efficient wavefront shaping with a MEMS phase-only light modulator},
 author        = {J. C. A. Rocha, ...},
 journal       = {...},
 volume        = {...},
 number        = {...},
 pages         = {...},
 year          = {...},
 doi           = {...},
}
```

The Texas Instruments Phase-only Light Modulator is a MEMS-based Spatial Light Modulator. It's an array of µ-mirrors that electrostatically piston up and down and can change the phase of the reflected light on a pixel-by-pixel basis. 
There are three PLM models out there, with diagonal screen sizes of 0.47", 0.67", and 0.98". Here I'm providing supporting code for the 0.67" PLM Evaluation Module (EVM).

The PLM builds on top of existing DLP technology, and the 0.67" PLM (DLP6750 EVM) is driven by TI's [DLP670S](https://www.ti.com/product/DLP670S) board and contains an array of 1358 x 800 µ-mirrors that pistons at 16 different heights. The PLM can reconfigure the heights of all mirrors at rates up to 1440 Hz.
While DLP670S has some internal flash memory to store and display a few holograms, it's usually not enough for a whole optics experiment, so, the main way of using this PLM is through a video interface. Here, we provide code for using the PLM through its video interface. We provide functions for creating the holograms, bitpacking 24 holograms into a single frame, displaying that frame to the PLM screen, and changing that frame without frame-drops to ensure maximum hologram-rate.

The PLM offers two mode of connections, HDMI and DisplayPort. Connected through the HDMI, the PLM can display different holograms at a rate of 720 Hz, and, through the DisplayPort 1440 Hz. It is reported that storing patterns in the DLP670S's internal flash memory can acheive hologram rates up to 5.76 kHz, but this operation mode is not documented.

How does the code look? In MATLAB, operation is

```MATLAB
MAX_FRAMES = 90; % Each contains 24 holograms
N = 1358;
M = 800;
% Create a PLMController instance
plm = PLMController(MAX_FRAMES, N, M);

% Setup the PLM
plm.StartUI(1);  % First monitor = 1 (adjust if needed)

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
frame = plm.BipackHolograms(phase);

% ---- Uploads a bitpacked hologram to the PLM ----
offset = 0;
plm.InsertFrames(frame, offset);

% If you need to set a sequence or start it, you can do so:
plm.SetHologramSequence(0:MAX_FRAMES-1);  % Set to display the bitpacked holograms in a sequence
plm.StartSequence(MAX_FRAMES);  % Display MAX_HOLOGRAMS hologram in a sequence following the sequence

% When you're done:
plm.Cleanup();
```

## External Code/Libraries/used by PLMCtrl
* [Dear ImGui](https://github.com/ocornut/imgui) for GUI handling and wrapping graphics API
* [hidapi](https://github.com/libusb/hidapi) for USB communication with the PLM
* Microsoft's DirectX 11 as Graphics API

## Contact
* PLMCtrl is developed and maintained by José C. A. Rocha
* This library is actively being developed, so users may encounter some rough edges
* For any feedback, questions or other enquiries, feel free to contact me at jd964@exeter.ac.uk, or open an issue here on GitHub










