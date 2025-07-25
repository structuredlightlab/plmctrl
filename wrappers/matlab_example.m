% Written by J. C. A. Rocha
% Date: 8/Jun/2025
% Queries: jd964@exeter.ac.uk

% Run this code section by section.

clearvars;

addpath('../');
addpath('../bin/')

MAX_FRAMES = 16;

% Set monitor size
% PLM is N = 1358 by M = 800
N = 1358;
M = 800;

% This is the offset to the PLM virtual monitor. (0, 0) is the top left corner of your main screen. 
% The example below ( x0 = 2560, y0 = 0 ) is for the PLM monitor to be on the right of the main QHD screen.
x0 = 2560;
y0 = 0;

plm = PLMController(MAX_FRAMES, N, M, x0, y0);

plm.Open(); % This opens USB comms with the PLM, after this command, you can call certain functions that changes a few PLM settings.

plm.SetWindowedMode(true); % Only for debug purposes -- Suggested if you're testing how this library work

%% Configure the PLM for HDMI
% You should configure the PLM only once per boot. You should also run it
% section by section. Some wait period is necessary between commands.

HDMI = 1;
DisplayPort = 2;

PlayOnce = 0;
Continuous = 1;

play_mode = Continuous;
connection_type = DisplayPort;

%% Set source to Parallel RGB (0) and port width to 24 bits (1)
plm.SetSource(0, 1);
%% Set port swap to Port 0 and 1 to ABC -> ABC
plm.SetPortSwap(0, 0);
plm.SetPortSwap(1, 0);
%% Set Pixel Mode. 1 for HDMI (Single Pixel), 2 for DisplayPort (Dual Pixel)
plm.SetPixelMode(connection_type);
%% Set Connection Type (This will lock the PLM to the source (video stream)) -- Wait ~3 sec after this
plm.SetConnectionType(connection_type);
%% Set video pattern mode (This is the mode we use for reading from the video stream) -- Wait ~3 sec after this
plm.SetVideoPatternMode();
%%  Update LUT with play mode and connection type
plm.UpdateLUT(play_mode, connection_type)

%% Start the UI
% If you're using DisplayPort, you have to configure the PLM before
% starting the UI, but if using HDMI, you can do it before.
plm.StartUI();

plm.Play(); % PLM will start reading from the screen continuously (or once, depending on your play_mode)
%% Modify the Look-Up Table (LUT)
% By default, it is set to TI's LUT (Texas Instruments)
% phase_levels = single([0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1]);
phase_levels = single([0.004, 0.017, 0.036, 0.058, 0.085, 0.117, 0.157, 0.217, 0.296, 0.4, 0.5, 0.605, 0.713, 0.82, 0.922, 0.981, 1]);
% phase_levels = single(linspace(0,1,17)); % linear LUT
plm.SetLookupTable(phase_levels);

% Set phase_map
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

order = [14, 1, 10, 6, 2, 15, 11, 7, 3, 16, 12, 8, 4, 13, 9, 5];
phase_map = phase_map(order,:);

plm.SetPhaseMap(phase_map);
%% Simple test: Inserts a random hologram into the sequence

phaseTest = zeros(N, M, 24,'single');
phaseTest(1:N/4,1:M/4, :) = 0;
phaseTest(N/4 + 1: end, 1:M/4, :) = 0.4*rand();
phaseTest(1:N/4, M/4+1:end, :) = 0.2*rand();
phaseTest(N/4+1:end, M/4+1:end, :) = 0.6*rand();

frame = plm.BitpackHologramsGPU(phaseTest);
offset = 0;
format = 1; % 0 = RGB frame, 1 = RGBA frame. BitpackHologramsGPU outputs an RGBA frame.
plm.InsertFrames(frame, offset, format);
plm.SetFrame(offset);


%% Generate multiple holograms and send them to plmctrl (Fastest way)
% Here we use pointers to avoid unnecessary allocations. 

[x, y] = meshgrid(linspace(-1,1,M), linspace(-M/N,M/N,N));
wedge = @(alpha, beta) alpha*x + beta*y;

numHolograms = 24;
phase = zeros(N, M, numHolograms, 'single');
frame_set = zeros(4*2*N, 2*M, MAX_FRAMES, 'uint8');
frame = zeros(4*2*N, 2*M, 'uint8');

for j = 1:MAX_FRAMES
    fprintf("MATLAB: Generating bitpacked hologram #%d\n",j);
    for i = 1:numHolograms
        alpha = 2*(rand() - 0.5);
        beta = 2*(rand() - 0.5);
        phase(:,:,i) = mod(wedge(alpha, beta), 1);
    end
    
    phasePtr = libpointer('singlePtr', phase);
    framePtr = libpointer('uint8Ptr', frame);
    plm.BitpackHologramsGPUPtr(phasePtr, framePtr, numHolograms);
    frame_set(:,:,j) = framePtr.Value;

end

% Uploads a bunch of frames to the PLM memory starting at index 0 (=offset)
offset = 0;
format = 1; % 1 = RGBA, 0 = RGB

plm.InsertFrames(frame_set, offset, format);
plm.SetFrame(offset);


%% Generate multiple holograms and send them to plmctrl (Slow way)
[x, y] = meshgrid(linspace(-1,1,M), linspace(-M/N,M/N,N));
wedge = @(alpha, beta) alpha*x + beta*y;

numHolograms = 24;
phase = zeros(N, M, numHolograms, 'single');
frame_set = zeros(4*2*N, 2*M, MAX_FRAMES, 'uint8');

for j = 1:MAX_FRAMES
    fprintf("MATLAB: Generating bitpacked hologram #%d\n",j);
    for i = 1:numHolograms
        alpha = 2*(rand() - 0.5);
        beta = 2*(rand() - 0.5);
        phase(:,:,i) = mod(wedge(alpha, beta), 1);
    end
    frame = plm.BitpackHologramsGPU(phase);
    frame_set(:,:,j) = frame;

end

% Uploads a bunch of frames to the PLM memory starting at index 0 (=offset)
offset = 0;
format = 1; % 1 = RGBA, 0 = RGB

plm.InsertFrames(frame_set, offset, format);
plm.SetFrame(offset);

%% Creating and playing a sequence
sequence = (0:MAX_FRAMES-1);
plm.SetFrameSequence(sequence);
plm.StartSequence(MAX_FRAMES);


%% PLM LUT hologram
% By default, it is set to TI's LUT (Texas Instruments)
phase_levels = single([0, 0.0100, 0.0205, 0.0422, 0.0560, 0.0727, 0.1131, 0.1734, 0.3426, 0.3707, 0.4228, 0.4916, 0.5994, 0.6671, 0.7970, 0.9375, 1]);
plm.SetLookupTable(phase_levels);

numHolograms = 16;
phase = zeros(N, M, numHolograms);
for i = 1:numHolograms
    phase(1:N/2,:, i) = phase_levels(i);
end

frame = plm.BitpackHologramsGPU(phase);

offset = 0;
format = 1; % RGBA
plm.InsertFrames(frame, offset, format);
plm.SetFrame(0);



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


%% Close the UI and Unload the library from MATLAB's memory 
plm.Cleanup();
