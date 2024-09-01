addpath('x64/Debug');
% addpath('../');

clc
clear all;

%%

MAX_FRAMES = 12;

% Set monitor size
% PLM is N = 1358 by M = 800
N = 1920/8;
M = 1080/8;

plm = PLMController(MAX_FRAMES, N, M);

monitorId = 1;
plm.StartUI(monitorId);

%% Unload the library from MATLAB's memory

plm.Cleanup();


%% Modify the Look-Up Table (LUT)
% By default, it is set to TI's LUT (Texas Instruments)
phase_levels = [0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1];
% phase_levels = linspace(0,1,17); % linear LUT
plm.SetLookupTable(phase_levels);

%% Simple test: Inserts a random hologram into the sequence
% Create a random frame (bitpacked holograms)
frame =  randi(255,3*2*N, 2*M, 'uint8');
% Set the hologram to be displayed
offset = 0;
plm.InsertFrames(frame, offset)

%% Inserts a set of frames into the sequence
% Create a random frame (bitpacked holograms)
num_frames = 10;
frames =  randi(255,3*2*N, 2*M, num_frames, 'uint8');
% Set the hologram to be displayed
offset = 0;
plm.InsertFrames(frames, offset)

%% Sets the frame sequence to be displayed
% Each frame contains 24 bitpacked PLM holograms
% sequence = (0:47);
% sequence = repmat([0,1,2,3,],[1,6]);
% % sequence = repmat([0,1],[1,12]);
% sequence = repmat([0,1,2,3],[1,6]);
sequence = [0,1,2,3];
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

%% More involved test: Create multiple holograms
[x, y] = meshgrid(linspace(-1,1,M), linspace(-M/N,M/N,N));
wedge = @(alpha, beta) alpha*x + beta*y;


% Generate multiple holograms
numHolograms = 24;
phase = zeros(N, M, numHolograms);
frame_set = zeros(3*2*N, 2*M, MAX_FRAMES, 'uint8');

for j = 1:MAX_FRAMES
    fprintf("MATLAB: Generating bitpacked hologram #%d\n",j);
    for i = 1:numHolograms
        alpha = 2*(rand() - 0.5);
        beta = 2*(rand() - 0.5);
        phase(:,:,i) = mod(wedge(alpha, beta), 2*pi)/(2*pi);
    end
    frame = plm.BitpackHolograms(phase);
    frame_set(:,:,j) = frame;
end

% Uploads a bunch of frames to the PLM memory starting at index 0 (=offset)
offset = 0;
plm.InsertFrames(frame_set, offset);

sequence = (0:MAX_FRAMES-1);
plm.SetFrameSequence(sequence);

plm.SetFrame(0); % First frame

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

frame = plm.BitpackHolograms(phase);

offset = 0;
plm.InsertFrames(frame, offset);
plm.SetFrame(0);


%% Visualize the hologram. 
% MATLAB figure and the second screen should match or I've done something wrong
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

