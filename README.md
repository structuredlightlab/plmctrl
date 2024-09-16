# <img src="https://github.com/user-attachments/assets/92c3cc2b-c4f1-4ed0-b876-7b01cac2bc67" alt="logo" width="24"/> SLMCtrl 

![Warning](https://img.shields.io/badge/under%20development%20-yellow)
[![arXiv](https://img.shields.io/badge/arXiv-2409.01289-<COLOR>.svg)](https://arxiv.org/abs/2409.01289)
![Warning](https://img.shields.io/badge/version-0.1.2a-red)


[//]: <img src="https://github.com/user-attachments/assets/617a1f9a-7be8-4c00-a289-ed66f41fd31b" alt="mirrors_low" style="width: 100%; margin-right: 20px;">

```slmctrl``` is an open source library for controlling LCOs-SLMs that are driven by a video interface. This library is a C++/DirectX code that handles the whole process from a matrix of ***continous phase values*** → ***prepare the hologram*** → ***display the hologram on the screen***. When displaying different holograms in a sequence, frame-pacing is also ensured.

If you have used ```slmctrl``` in a scientific publication, we would appreciate citation to the following reference:
```bibtex
@misc{rocha2024fastlightefficientwavefrontshaping,
      title={Fast and light-efficient wavefront shaping with a MEMS phase-only light modulator}, 
      author={José C. A. Rocha and Terry Wright and Unė G Būtaitė and Joel Carpenter and George S. D. Gordon and David B. Phillips},
      year={2024},
      eprint={2409.01289},
      archivePrefix={arXiv},
      primaryClass={physics.optics},
      url={https://arxiv.org/abs/2409.01289}, 
}
```

How does the code look? In MATLAB, operation is

```MATLAB
MAX_HOLOGRAMS = 90; 
N = 1280; % SLM Width
M = 1024; % SLM Height
% Create a SLMController instance
slm = SLMController(MAX_FRAMES, N, M);

% Setup the SLM
slm.StartUI(1);  % First monitor = 1 (adjust if needed)

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

% ---- Creates 24 holograms and stores them in the pages a 3D matrix (holograms) ---- 
holograms = slm.CreateHolograms(phase);

% ---- Uploads a holograms to the SLMCtrl's memory space ----
offset = 0;
slm.InsertHolograms(holograms, offset);

% If you need to set a sequence or start it, you can do so:
slm.SetHologramSequence(0:MAX_HOLOGRAMS-1);  
slm.StartSequence(MAX_HOLOGRAMS); 

% When you're done:
slm.Cleanup();
```

## External Code/Libraries/used by PLMCtrl
* [Dear ImGui](https://github.com/ocornut/imgui) for GUI handling and wrapping graphics API
* Microsoft's DirectX 11 as Graphics API

## Contact
* SLMCtrl is developed and maintained by José C. A. Rocha
* This library is actively being developed, so users may encounter some rough edges
* For any feedback, questions or other enquiries, feel free to contact me directly at (jd964@exeter.ac.uk), or open an issue here on GitHub










