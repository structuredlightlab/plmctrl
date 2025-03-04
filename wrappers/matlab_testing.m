clearvars;

addpath('../');
addpath('../bin/')

MAX_FRAMES = 64;

% Set monitor size
% PLM is N = 1358 by M = 800
N = 1920/2;
M = 1080/2;

plm = PLMController(MAX_FRAMES, N, M);

% %%
% plm.SetWindowedMode(true);
monitorId = 1; % This parameter is not currently working.
plm.StartUI(monitorId);
% 
%% Modify the Look-Up Table (LUT)
% By default, it is set to TI's LUT (Texas Instruments)
phase_levels = single([0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1]);
%phase_levels = single(linspace(0,1,17)); % linear LUT
plm.SetLookupTable(phase_levels);

%% Simple test: Inserts a random hologram into the sequence

phaseTest = zeros(N, M, 24,'single');
phaseTest(1:N/4,1:M/4, :) = 0;
phaseTest(N/4 + 1: end, 1:M/4, :) = 0.4;
phaseTest(1:N/4, M/4+1:end, :) = 0.2;
phaseTest(N/4+1:end, M/4+1:end, :) = 0.6;

% for i = 1:30
tic
frame = plm.BitpackHologramsGPU(phaseTest);
toc
% end

% negligible time
offset = 0;
format = 1;
plm.InsertFrames(frame, offset, format);
plm.SetFrame(offset);


%% Visualize the hologram. 
% This MATLAB figure and the second screen should match or I've done something wrong
% Assemble the RGB matrix from the hologram matrix
A = frame';
RGB = zeros(2*M, 2*N, 3);
RGB(:,:,1) = A(:,1:4:end);
RGB(:,:,2) = A(:,2:4:end);
RGB(:,:,3) = A(:,3:4:end);

% Display the hologram
figure(1);
imshow(RGB/255);
set(gca, 'Position', [0.1, 0.1, 0.8, 0.8])


%% Sets the frame sequence to be displayed
% Each frame contains 24 bitpacked PLM holograms
% sequence = (0:47);
% sequence = repmat([0,1,2,3,],[1,6]);
% % sequence = repmat([0,1],[1,12]);
% sequence = repmat([0,1,2,3],[1,6]);
sequence = 0:(MAX_FRAMES-1);
plm.SetFrameSequence(sequence);

%% Start Sequence
frames_to_display = 4;
plm.StartSequence(frames_to_display);

%% Sets the order to display single frame
frame = 0;
plm.SetFrame(frame);

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

phase_map = phase_map(randperm(16),:);
plm.SetPhaseMap(phase_map);

%%

tic
plm.BitpackAndInsertGPU(phase, offset);
toc

%%
sequence = (0:MAX_FRAMES-1);
plm.SetFrameSequence(sequence);
plm.StartSequence(MAX_FRAMES);

%% PLM LUT hologram
% By default, it is set to TI's LUT (Texas Instruments)
phase_levels = [0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1];
% phase_levels = linspace(0,1,17); % linear LUT
plm.SetLookupTable(phase_levels);

numHolograms = 16;
phase = zeros(N, M, numHolograms);
for i = 1:numHolograms
    phase(1:N/2,:, i) = phase_levels(i);
end
%%
frame = plm.BitpackHolograms(phase);

offset = 0;
format = 1; % RGBA
plm.InsertFrames(frame, offset, format);
plm.SetFrame(0);

% %% Command the PLM to start the sequencer 
% (Not yet implemented in the wrapper)
% plm.Play();
% %% Command the PLM to stop the sequencer
% (Not yet implemented in the wrapper)
% plm.Pause();




%% Close the UI and Unload the library from MATLAB's memory 
plm.Cleanup();