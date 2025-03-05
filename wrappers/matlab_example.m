clearvars;

addpath('../');
addpath('../bin/')

%% Start the UI
MAX_HOLOGRAMS = 12;

% Set monitor size
N = 800;
M = 600;

windowed = false;
slm = SLMController(MAX_HOLOGRAMS, N, M, windowed);

monitorId = 0; % This parameter is not currently working.
slm.StartUI(monitorId);

% Simple test: Inserts a random hologram into the sequence
% Create a random frame (bitpacked holograms)
hologram =  randi(255,N, M, 'uint8');

offset = 0;
slm.InsertHolograms(hologram, offset)
slm.SetHologram(0);

%% Sets the frame sequence to be displayed
% Each frame contains 24 bitpacked PLM holograms
% sequence = (0:47);
% sequence = repmat([0,1,2,3,],[1,6]);
% % sequence = repmat([0,1],[1,12]);
% sequence = repmat([0,1,2,3],[1,6]);
sequence = 0:(MAX_HOLOGRAMS-1);
slm.SetHologramSequence(sequence);

%% Start Sequence
holograms_to_display = 4;
slm.StartSequence(holograms_to_display);

%% Sets the order to display single frame
hologram = 0;
slm.SetFrame(hologram);

%% More involved test: Create multiple holograms

linspace_holoeye_X = linspace(-1,1,M);

linspace_holoeye_Y = linspace(-M/N,M/N,N);

[x_holoeye, y_holoeye] = meshgrid(linspace_holoeye_X, linspace_holoeye_Y);
wedge_holoeye = @(alpha, beta) alpha*x_holoeye + beta* y_holoeye;

holograms = zeros(N, M, MAX_HOLOGRAMS, 'uint8');

for id = 1:MAX_HOLOGRAMS
    alpha = 850*(rand() - 0.5);
    beta = 850*(rand() - 0.5);
    holograms(:,:,id) = uint8(255*(mod(wedge_holoeye(alpha, beta), 2*pi)/(2*pi)));
end

% Uploads a bunch of frames to the PLM memory starting at index 0 (=offset)
offset = 0;
slm.InsertHolograms(holograms, offset);

% sequence = (0:MAX_FRAMES-1);
% slm.SetFrameSequence(sequence);

% plm.SetFrame(0); % First frame

%%
sequence = (0:MAX_HOLOGRAMS-1);
slm.SetHologramSequence(sequence);
slm.StartSequence(MAX_HOLOGRAMS);


%% Visualize the hologram. 
% This MATLAB figure and the second screen should match or I've done something wrong
% Assemble the RGB matrix from the hologram matrix
A = hologram';
RGB = zeros(M, N, 3);
RGB(:,:,1) = A(:,1:end);

% Display the hologram
figure(1);
imshow(RGB/255);
set(gca, 'Position', [0.1, 0.1, 0.8, 0.8])


%% Close the UI and Unload the library from MATLAB's memory 
slm.Cleanup();