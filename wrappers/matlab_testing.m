clearvars;

addpath('../');
addpath('../bin/')

MAX_FRAMES = 64;

% Set monitor size
% PLM is N = 1358 by M = 800
N = 1358; M = 800;

plm = PLMController(MAX_FRAMES, N, M);


plm.SetWindowedMode(true);
monitorId = 1; % This parameter is not currently working.
plm.StartUI(monitorId);
% 
%% Modify the Look-Up Table (LUT)
% By default, it is set to TI's LUT (Texas Instruments)
% phase_levels = single([0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1]);
phase_levels = single([0.004, 0.017, 0.036, 0.058, 0.085, 0.117, 0.157, 0.217, 0.296, 0.4, 0.5, 0.605, 0.713, 0.82, 0.922, 0.981, 1]);
% phase_levels = single([0.0, 0.0148, 0.0285, 0.0450, 0.0924, 0.1558, 0.2719, 0.3001, 0.3546, 0.4244, 0.5187, 0.5828, 0.7195, 0.8603, 0.9722, 0.9818, 1.0]);
% phase_levels = single(linspace(0,1,17)); % linear LUT
plm.SetLookupTable(phase_levels);

%% Set phase_map
% Define phase map
phase_map = [
    0 0 0 0;
    1 0 0 0;
    0 1 0 0;
    1 1 0 0;
    0 0 1 0;
    1 0 1 0;
    0 1 1 0;
    1 1 1 0;
    0 0 0 1;
    1 0 0 1;
    0 1 0 1;
    1 1 0 1;
    0 0 1 1;
    1 0 1 1;
    0 1 1 1;
    1 1 1 1;
];

% phase_map = phase_map(randperm(16),:);
phase_map = phase_map([14 1 10 6 2 15 11 7 3 16 12 8 4 13 9 5],:);

plm.SetPhaseMap(phase_map);
%% Simple test: Inserts a random hologram into the sequence

phaseTest = zeros(N, M, 24,'single');
phaseTest(1:N/4,1:M/4, :) = 0;
phaseTest(N/4 + 1: end, 1:M/4, :) = 0.4*rand();
phaseTest(1:N/4, M/4+1:end, :) = 0.2*rand();
phaseTest(N/4+1:end, M/4+1:end, :) = 0.6*rand();

% for i = 1:30
tic
% frame = plm.BitpackHolograms(phaseTest);
frame = plm.BitpackHologramsGPU(phaseTest);
toc

% negligible time
offset = 0;
format = 1;
plm.InsertFrames(frame, offset, format);
plm.SetFrame(offset);

%% test with 1x1x24 phase array
phaseVal = 0.1;
phaseTest = zeros(1, 1, 24,'single') + phaseVal;
numPatterns = size(phaseTest, 3);
        frame = zeros(4*2*size(phaseTest,1), 2*size(phaseTest,2), 'uint8');
        % Prepare pointers to the phase data and the hologram array
        phasePtr = libpointer('singlePtr', phaseTest);
        hologramPtr = libpointer('uint8Ptr', frame);
        % Bit-pack the holograms using the library function
        calllib('plmctrl', 'BitpackHolograms', phasePtr, hologramPtr, 1, 1, numPatterns);
        % Retrieve the bit-packed hologram
        frame = hologramPtr.Value



%% Close the UI and Unload the library from MATLAB's memory 
plm.Cleanup();