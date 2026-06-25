% Written by J. C. A. Rocha
% Date: 25/Jun/2025
% Queries: jd964@exeter.ac.uk
%
% MATLAB counterpart of python_example_vis.ipynb / python_example_nir.ipynb.
% Run this code section by section.

clearvars;

addpath('../');
addpath('../bin/')

MAX_FRAMES = 120; % Max RGB frames stored in plmctrl's memory. Each frame holds 24 holograms -- limited by your RAM.

% Set monitor size (PLM display dimensions, in pixels)
%   0.67" VIS : N = 1358, M = 800
%   0.67" NIR : N = 904,  M = 800
% For an NIR PLM, swap N below and use plm.SetPhaseMapNIR with a 32x6 map (see the NIR section at the bottom).
N = 1358;
M = 800;

% Offset of the PLM virtual monitor. (0, 0) is the top-left corner of your main screen.
% ( x0 = 2560, y0 = 0 ) puts the PLM monitor to the right of a QHD main screen.
x0 = 2560;
y0 = 0;

plm = PLMController(MAX_FRAMES, N, M, x0, y0);

% plm.Open(); % Opens USB comms with the PLM. After this you can call functions that change PLM settings.

plm.SetWindowedMode(true); % Debug only -- suggested while testing. Use false for real experiments.

%% Start the UI
% If you're using DisplayPort you must configure the PLM before starting the UI;
% with HDMI you can do it either before or after.
plm.StartUI();

plm.Play(); % PLM starts reading from the screen continuously (or once, depending on play_mode)

%% Configure the PLM for HDMI
% Configure the PLM only once per boot. Run these sections one by one, leaving a
% short pause between commands.
HDMI = 1;
DisplayPort = 2;

PlayOnce = 0;
Continuous = 1;

play_mode = Continuous;
connection_type = HDMI;

%% Set source to Parallel RGB (0) and port width to 24 bits (1)
plm.SetSource(0, 1);
%% Set port swap for ports 0 and 1 to ABC -> ABC
plm.SetPortSwap(0, 0);
plm.SetPortSwap(1, 0);
%% Set Pixel Mode. 1 for HDMI (Single Pixel), 2 for DisplayPort (Dual Pixel)
plm.SetPixelMode(connection_type);
%% Lock the PLM to the video stream -- Wait ~3 sec after this
plm.SetConnectionType(connection_type);
%% Set video pattern mode (used for reading the video stream) -- Wait ~3 sec after this
plm.SetVideoPatternMode();
%% Update the bit lookup-table with the play mode and connection type
plm.UpdateLUT(play_mode, connection_type);

%% Modify the Look-Up Table (LUT)  [optional]
% By default it is set to TI's LUT (Texas Instruments).
% phase_levels = single([0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1]);
phase_levels = single([0.004, 0.017, 0.036, 0.058, 0.085, 0.117, 0.157, 0.217, 0.296, 0.4, 0.5, 0.605, 0.713, 0.82, 0.922, 0.981, 1]);
% phase_levels = single(linspace(0, 1, 17)); % linear LUT
plm.SetLookupTable(phase_levels);

%% Bitpack and insert a single frame (quadrant phase)
% MATLAB phase convention is (N, M, numHolograms) -- column-major, matching the
% (numHolograms, M, N) row-major layout used by the Python examples.
phase = zeros(N, M, 24, 'single');
phase(1:N/2,     1:M/2,     :) = 0.0;
phase(N/2+1:end, 1:M/2,     :) = 0.3;
phase(1:N/2,     M/2+1:end, :) = 0.2;
phase(N/2+1:end, M/2+1:end, :) = 0.9;

frame = plm.BitpackHologramsGPU(phase);
offset = 0;
format = 1; % 0 = RGB, 1 = RGBA. BitpackHologramsGPU outputs an RGBA frame.
plm.InsertFrames(frame, offset, format);
plm.SetFrame(offset);

%% Bitpack and insert one frame at a time (phase ramps, GPU, fastest)
% BitpackAndInsertGPU bitpacks on the GPU and writes straight into the PLM buffer.
numHolograms = 24;

for i = 0:MAX_FRAMES-1
    fprintf("MATLAB: Generating bitpacked hologram #%d\n", i + 1);
    a = linspace(0, i * 2 + 1, N)';     % ramp along N (rows)
    b = linspace(0, 0,         M);      % flat along M (cols)
    ph = mod(a + b, 1);                 % (N, M)
    phase = repmat(single(ph), 1, 1, numHolograms);

    plm.BitpackAndInsertGPU(phase, i);
end

%% Bitpack every frame first, then insert them all at once (pointers, no realloc)
numHolograms = 24;
frame_set = zeros(4*2*N, 2*M, MAX_FRAMES, 'uint8');
frame = zeros(4*2*N, 2*M, 'uint8');

for j = 1:MAX_FRAMES
    fprintf("MATLAB: Generating bitpacked hologram #%d\n", j);
    a = linspace(0, 0,         N)';
    b = linspace(0, j * 2 + 1, M);
    ph = mod(a + b, 1);
    phase = repmat(single(ph), 1, 1, numHolograms);

    phasePtr = libpointer('singlePtr', phase);
    framePtr = libpointer('uint8Ptr', frame);
    plm.BitpackHologramsGPUPtr(phasePtr, framePtr, numHolograms);
    frame_set(:,:,j) = framePtr.Value;
end

% Upload all frames to the PLM memory starting at index 0 (= offset)
offset = 0;
format = 1; % 1 = RGBA, 0 = RGB
plm.InsertFrames(frame_set, offset, format);
plm.SetFrame(offset);

%% Multiple holograms with random wedge (blazed grating) phases
[x, y] = meshgrid(linspace(-1, 1, M), linspace(-M/N, M/N, N)); % x, y are (N, M)
wedge = @(alpha, beta) alpha*x + beta*y;

numHolograms = 24;
phase = zeros(N, M, numHolograms, 'single');

for j = 0:MAX_FRAMES-1
    fprintf("MATLAB: Generating bitpacked hologram #%d\n", j + 1);
    for i = 1:numHolograms
        alpha = 50.0 * (rand() - 0.5);
        beta  = 50.0 * (rand() - 0.5);
        phase(:,:,i) = mod(wedge(alpha, beta), 2*pi) / (2*pi);
    end

    plm.BitpackAndInsertGPU(phase, j);
end

%% Display the inserted frames as a looping sequence
sequence = 0:MAX_FRAMES-1;
plm.SetFrameSequence(sequence);
plm.StartSequence(MAX_FRAMES);

%% Visualize a hologram
% This MATLAB figure and the second screen should match (otherwise something is wrong).
% Reassemble the RGB image from the bitpacked frame.
A = frame';
RGB = zeros(2*M, 2*N, 3);
RGB(:,:,1) = A(:, 1:4:end);
RGB(:,:,2) = A(:, 2:4:end);
RGB(:,:,3) = A(:, 3:4:end);

figure(1);
imshow(RGB/255);
set(gca, 'Position', [0.1, 0.1, 0.8, 0.8])

%% NIR PLM notes
% For a 0.67" NIR PLM:
%   - Initialise with N = 904, M = 800.
%   - Use a 32 x 6 phase map via plm.SetPhaseMapNIR(...) instead of SetPhaseMap.
%   - Use plm.BitpackHologramsNIRGPU / plm.BitpackAndInsertNIRGPU instead of the VIS variants.
%     (The NIR bitpacked frame is 4*(3N+4) x 2M; the VIS frame is 4*2N x 2M.)
% Everything else (phase generation, insertion, sequencing) is identical.

%% Close the UI and unload the library from MATLAB's memory
plm.Cleanup();
