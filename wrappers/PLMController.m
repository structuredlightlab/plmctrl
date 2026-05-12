function plm = PLMController(MAX_FRAMES, width, height, x0, y0)
% PLMController: Initializes and manages a controller for PLM hardware.
%
% This function creates a structure `plm` with various fields and methods
% to control a phase light modulator (PLM). It manages loading the necessary
% library, setting up the PLM, inserting frames, setting sequences, and
% handling cleanup.
%
% Inputs:
%   - MAX_FRAMES: The maximum number of frames that the PLM can handle.
%   - width: Width of the PLM display in pixels.
%   - height: Height of the PLM display in pixels.
%   - x0 (int): X-coordinate of the PLM window position (default: 1920). -- Top left corner of your main monitor is ( 0, 0 ), x0 and y0 are relative to that
%   - y0 (int): Y-coordinate of the PLM window position (default: 0).
%     0.67" PLM is 1358 x 800 (width,height)
% Outputs:
%   - plm: A structure containing PLM control methods and properties.

% Assign the maximum number of frames, width, and height to the `plm` structure
plm.MAX_FRAMES = MAX_FRAMES;
plm.N = width;
plm.M = height;
plm.x0 = x0;
plm.y0 = y0;

% Load the 'plmctrl' library if it is not already loaded
if ~libisloaded('plmctrl')
    [~, ~] = loadlibrary('plmctrl', 'plmctrl.h');
end

% Assign functions to the `plm` structure for controlling the PLM
plm.Open = @Open;                     % Opens the USB communication with the PLM
plm.StartUI = @StartUI;                % Setup the PLM window and UI
plm.InsertFrames = @InsertFrames;        % Insert hologram frames into the PLM
plm.SetFrameSequence = @SetFrameSequence; % Set the sequence of frames for display
plm.StartSequence = @StartSequence;      % Start displaying the sequence of frames
plm.Play = @Play;                    % PLM starts reading from the screen
plm.Stop = @Stop;                    % PLM stops reading from the screen
plm.StopUI = @StopUI;                    % Stop the PLM debug UI
plm.PauseUI = @PauseUI;                    % Pause the PLM debug UI
plm.ResumeUI = @ResumeUI;                    % Resume the PLM UI
plm.SetFrame = @SetFrame;                % Set a specific frame to display
plm.SetPhaseMap = @SetPhaseMap;          % Set the VIS phase map (16 levels x 4 cells)
plm.SetPhaseMapNIR = @SetPhaseMapNIR;    % Set the NIR phase map (32 levels x 6 cells)
plm.BitpackHolograms = @BitpackHolograms;  % Create and bit-pack holograms from phase data (VIS, CPU)
plm.BitpackHologramsNIR = @BitpackHologramsNIR;  % VIS variant for NIR PLM (CPU)
plm.BitpackHologramsGPU = @BitpackHologramsGPU;
plm.BitpackHologramsNIRGPU = @BitpackHologramsNIRGPU;
plm.BitpackHologramsGPUPtr = @BitpackHologramsGPUPtr;
plm.BitpackAndInsertGPU = @BitpackAndInsertGPU;
plm.BitpackAndInsertNIRGPU = @BitpackAndInsertNIRGPU;
plm.SetWindowedMode = @SetWindowed;
plm.Cleanup = @cleanup;                  % Unload the library and cleanup resources

% PLM configuring functions
plm.SetSource = @SetSource;
plm.SetPortSwap = @SetPortSwap;
plm.SetConnectionType = @SetConnectionType;
plm.GetConnectionType = @GetConnectionType;
plm.SetVideoPatternMode = @SetVideoPatternMode;
plm.GetVideoPatternMode = @GetVideoPatternMode;
plm.UpdateLUT = @UpdateLUT;
plm.SetPixelMode = @SetPixelMode;  % New method added

plm.Configure = @Configure;

    function out = GetVideoPatternMode()
        out = calllib('plmctrl', 'GetVideoPatternMode');
    end

    function out = GetConnectionType()
        out = calllib('plmctrl', 'GetConnectionType');
    end

    function out = Open()
        out = calllib('plmctrl', 'Open');
    end

    function out = Play()
        out = calllib('plmctrl', 'Play');
    end

    function out = Stop()
        out = calllib('plmctrl', 'Stop');
    end

% Function to setup the PLM window on a specified monitor
    function StartUI()
        % Validate that the monitor ID is a positive integer
        calllib('plmctrl', 'SetPLMWindowPos', plm.N, plm.M, plm.x0, plm.y0);
        calllib('plmctrl', 'StartUI', plm.MAX_FRAMES);
    end

    function res = InsertFrames(frames, offset, format)
        validateattributes(frames, {'uint8'}, {'3d'});
        validateattributes(offset, {'numeric'}, {'scalar', 'nonnegative', 'integer'});

        % Insert the holograms into the PLM at the specified offset
        res = calllib('plmctrl', 'InsertPLMFrame', libpointer('uint8Ptr', frames), size(frames, 3), offset, format);
    end

    function SetWindowed(windowed_mode)
        % Insert the holograms into the PLM at the specified offset
        calllib('plmctrl', 'SetWindowed', windowed_mode);
    end

    function SetFrameSequence(sequence)
        validateattributes(sequence, {'numeric'}, {'vector', 'integer', 'nonnegative'});
        calllib('plmctrl', 'SetFrameSequence', libpointer('uint64Ptr', sequence), length(sequence));
    end

    function StartSequence(holograms_to_display)
        validateattributes(holograms_to_display, {'numeric'}, {'scalar', 'positive', 'integer'});
        calllib('plmctrl', 'StartSequence', holograms_to_display);
    end

    function StopUI()
        calllib('plmctrl', 'StopUI');
    end

    function PauseUI()
        calllib('plmctrl', 'PauseUI');
    end

    function ResumeUI()
        calllib('plmctrl', 'ResumeUI');
    end

    function SetFrame(frame)
        validateattributes(frame, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        calllib('plmctrl', 'SetPLMFrame', frame);
    end

    function res = SetPhaseMap(phase_map)
        % VIS phase map: 16 levels x 4 cells = 64 ints
        validateattributes(phase_map, {'numeric'}, {'2d', 'integer'});
        res = calllib('plmctrl', 'SetPhaseMap', libpointer('int32Ptr', transpose(phase_map)));
    end

    function res = SetPhaseMapNIR(phase_map)
        % NIR phase map: 32 levels x 6 cells = 192 ints
        validateattributes(phase_map, {'numeric'}, {'2d', 'integer'});
        res = calllib('plmctrl', 'SetPhaseMapNIR', libpointer('int32Ptr', transpose(phase_map)));
    end

% Function to create and bit-pack holograms from phase data (VIS, CPU)
    function frame = BitpackHolograms(phase)
        validateattributes(phase, {'single'}, {'3d', '>=', 0, '<=', 1'});
        numPatterns = size(phase, 3);
        frame = zeros(4*2*plm.N, 2*plm.M, 'uint8');

        phasePtr = libpointer('singlePtr', phase);
        hologramPtr = libpointer('uint8Ptr', frame);

        calllib('plmctrl', 'BitpackHolograms', phasePtr, hologramPtr, plm.N, plm.M, numPatterns);

        frame = hologramPtr.Value;
    end

% Function to create and bit-pack holograms from phase data (NIR, CPU)
    function frame = BitpackHologramsNIR(phase)
        validateattributes(phase, {'single'}, {'3d', '>=', 0, '<=', 1'});
        numPatterns = size(phase, 3);
        frame = zeros(4*(3*plm.N + 4), 2*plm.M, 'uint8');

        phasePtr = libpointer('singlePtr', phase);
        hologramPtr = libpointer('uint8Ptr', frame);

        calllib('plmctrl', 'BitpackHologramsNIR', phasePtr, hologramPtr, plm.N, plm.M, numPatterns);

        frame = hologramPtr.Value;
    end

% Function to create and bit-pack holograms (VIS, GPU)
% same_phase = true: phase is a single 2D plane shared across all 24 holograms
    function frame = BitpackHologramsGPU(phase, same_phase)
        if nargin < 2, same_phase = false; end
        if same_phase
            numHolograms = 24;
        else
            numHolograms = size(phase, 3);
        end
        frame = zeros(4*2*plm.N, 2*plm.M, 'uint8');

        phasePtr = libpointer('singlePtr', phase);
        framePtr = libpointer('uint8Ptr', frame);

        res = calllib('plmctrl', 'BitpackHologramsGPU', phasePtr, framePtr, plm.N, plm.M, numHolograms, logical(same_phase));
        fprintf("Bitpacked: %d\n", res);

        frame = framePtr.Value;
    end

% Function to create and bit-pack holograms (NIR, GPU)
    function frame = BitpackHologramsNIRGPU(phase, same_phase)
        if nargin < 2, same_phase = false; end
        if same_phase
            numHolograms = 24;
        else
            numHolograms = size(phase, 3);
        end
        frame = zeros(4*(3*plm.N + 4), 2*plm.M, 'uint8');

        phasePtr = libpointer('singlePtr', phase);
        framePtr = libpointer('uint8Ptr', frame);

        res = calllib('plmctrl', 'BitpackHologramsNIRGPU', phasePtr, framePtr, plm.N, plm.M, numHolograms, logical(same_phase));
        fprintf("Bitpacked: %d\n", res);

        frame = framePtr.Value;
    end

% Function to create and bit-pack holograms using user-provided pointers (VIS, GPU)
    function res = BitpackHologramsGPUPtr(phasePtr, framePtr, numHolograms, same_phase)
        if nargin < 4, same_phase = false; end
        res = calllib('plmctrl', 'BitpackHologramsGPU', phasePtr, framePtr, plm.N, plm.M, numHolograms, logical(same_phase));
        fprintf("Bitpacked: %d\n", res);
    end

% Bitpack on GPU and insert directly into the PLM frame buffer at `offset` (VIS)
    function res = BitpackAndInsertGPU(phase, offset, same_phase)
        if nargin < 3, same_phase = false; end
        if same_phase
            numPatterns = 24;
        else
            numPatterns = size(phase, 3);
        end

        phasePtr = libpointer('singlePtr', phase);

        res = calllib('plmctrl', 'BitpackAndInsertGPU', phasePtr, plm.N, plm.M, numPatterns, offset, logical(same_phase));
    end

% Bitpack on GPU and insert directly into the PLM frame buffer at `offset` (NIR)
    function res = BitpackAndInsertNIRGPU(phase, offset, same_phase)
        if nargin < 3, same_phase = false; end
        if same_phase
            numPatterns = 24;
        else
            numPatterns = size(phase, 3);
        end

        phasePtr = libpointer('singlePtr', phase);

        res = calllib('plmctrl', 'BitpackAndInsertNIRGPU', phasePtr, plm.N, plm.M, numPatterns, offset, logical(same_phase));
    end

    function res = SetSource(source, portWidth)
        validateattributes(source, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        validateattributes(portWidth, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        res = calllib('plmctrl', 'SetSource', uint32(source), uint32(portWidth));
        if res == -1
            error('SetSource failed');
        end
    end

    function res = SetPortSwap(port, swap)
        validateattributes(port, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        validateattributes(swap, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        res = calllib('plmctrl', 'SetPortSwap', uint32(port), uint32(swap));
        if res == -1
            error('SetPortSwap failed');
        end
    end

    function res = SetConnectionType(connection_type)
        validateattributes(connection_type, {'numeric'}, {'scalar', 'integer'});
        fprintf("Setting connection to %d\n", connection_type);
        res = calllib('plmctrl', 'SetConnectionType', int32(connection_type));
        if res == -1
            error('SetConnectionType failed');
        end
    end

    function res = SetVideoPatternMode()
        fprintf("Setting Video Pattern Mode\n");
        res = calllib('plmctrl', 'SetVideoPatternMode');
        if res == -1
            error('SetVideoPatternMode failed');
        end
    end

    function res = UpdateLUT(play_mode, connection_type)

        validateattributes(play_mode, {'numeric'}, {'scalar', 'integer', '>=', 0, '<=', 1});
        validateattributes(connection_type, {'numeric'}, {'scalar', 'integer', '>', 0, '<=', 2});

        fprintf("Updating bit lookup-table\n");
        res = calllib('plmctrl', 'UpdateLUT', int32(play_mode), int32(connection_type));
        if res == -1
            error('UpdateLUT failed');
        end
    end

% New function to set pixel mode
    function SetPixelMode(connection_type)
        validateattributes(connection_type, {'numeric'}, {'scalar', 'integer'});
        if connection_type == 1
            fprintf('Setting pixel mode to HDMI\n');
        else
            fprintf('Setting pixel mode to DisplayPort\n');
        end
        res = calllib('plmctrl', 'SetPortConfig', int32(connection_type));
        if res == -1
            error('SetPortConfig failed');
        end
    end

    function res = Configure(play_mode, connection_type)
        validateattributes(play_mode, {'numeric'}, {'scalar', 'integer', '>=', 0, '<=', 1});
        validateattributes(connection_type, {'numeric'}, {'scalar', 'integer', '>', 0, '<=', 2});

        % Set source to Parallel RGB (0) and port width to 24 bits (1)
        SetSource(0, 1);

        % Set port swap to Port 1 (0) and ABC -> ABC (0)
        SetPortSwap(0, 0);
        SetPortSwap(1, 0);

        SetPixelMode(connection_type);

        if (GetConnectionType() ~= 1)
            % Set connection type (0 to disable. 1 for HDMI, 1 for DisplayPort)
            SetConnectionType(connection_type);
            pause(5.5);
        end


        % Set video pattern mode
        SetVideoPatternMode();
        pause(2.0)

        % Update LUT with play mode and connection type
        UpdateLUT(play_mode, connection_type)

        res = 0; % Success
    end

% Function to cleanup and unload the PLM library
    function cleanup()
        calllib('plmctrl', 'StopUI');
        % If the 'plmctrl' library is loaded, unload it
        if libisloaded('plmctrl')
            unloadlibrary('plmctrl');
        end
    end
end
