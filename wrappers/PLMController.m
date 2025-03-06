function plm = PLMController(MAX_FRAMES, width, height)
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
    %     0.67" PLM is 1358 x 800 (w,h)
    % Outputs:
    %   - plm: A structure containing PLM control methods and properties.

    % Assign the maximum number of frames, width, and height to the `plm` structure
    plm.MAX_FRAMES = MAX_FRAMES;
    plm.N = width;
    plm.M = height;
    
    % Load the 'plmctrl' library if it is not already loaded
    if ~libisloaded('plmctrl')
        [~, ~] = loadlibrary('plmctrl', 'plmctrl.h');
    end
    
    % Assign functions to the `plm` structure for controlling the PLM
    plm.StartUI = @StartUI;                % Setup the PLM window and UI
    plm.InsertFrames = @InsertFrames;        % Insert hologram frames into the PLM
    plm.SetFrameSequence = @SetFrameSequence; % Set the sequence of frames for display
    plm.StartSequence = @StartSequence;      % Start displaying the sequence of frames
    plm.StopUI = @StopUI;                    % Stop the PLM UI
    plm.PauseUI = @PauseUI;                    % Pause the PLM UI
    plm.ResumeUI = @ResumeUI;                    % Resume the PLM UI
    plm.SetLookupTable = @SetLookupTable;    % Set the lookup table for phase levels
    plm.SetFrame = @SetFrame;                % Set a specific frame to display
    plm.SetPhaseMap = @SetPhaseMap;          % Set the phase map for holograms
    plm.BitpackHolograms = @BitpackHolograms;  % Create and bit-pack holograms from phase data
    plm.BitpackHologramsGPU = @BitpackHologramsGPU;
    plm.BitpackAndInsertGPU = @BitpackAndInsertGPU;
    plm.SetWindowedMode = @SetWindowed;
    plm.Cleanup = @cleanup;                  % Unload the library and cleanup resources

    % Function to setup the PLM window on a specified monitor
    function StartUI(monitor_id)
        % Validate that the monitor ID is a positive integer
        validateattributes(monitor_id, {'numeric'}, {'scalar', 'positive', 'integer'});
        
        calllib('plmctrl', 'SetPLMWindowPos', plm.N, plm.M, monitor_id);
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

    % Function to set the lookup table for phase levels
    function SetLookupTable(phase_levels)
        validateattributes(phase_levels, {'single'}, {'vector', 'numel', 17});
        calllib('plmctrl', 'SetLookupTable', libpointer('singlePtr', phase_levels));
    end

    function SetFrame(frame)
        validateattributes(frame, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        calllib('plmctrl', 'SetPLMFrame', frame);
    end

    function res = SetPhaseMap(phase_map)
        validateattributes(phase_map, {'numeric'}, {'2d', 'integer'});
        res = calllib('plmctrl', 'SetPhaseMap', libpointer('int32Ptr', transpose(phase_map)));
    end

    % Function to create and bit-pack holograms from phase data
    function frame = BitpackHolograms(phase)
        validateattributes(phase, {'single'}, {'3d', '>=', 0, '<=', 1'});
        % Initialize an empty array to hold the bit-packed hologram
        numPatterns = size(phase, 3);
        frame = zeros(4*2*plm.N, 2*plm.M, 'uint8');
        
        % Prepare pointers to the phase data and the hologram array
        phasePtr = libpointer('singlePtr', phase);
        hologramPtr = libpointer('uint8Ptr', frame);
        
        % Bit-pack the holograms using the library function
        calllib('plmctrl', 'BitpackHolograms', phasePtr, hologramPtr, plm.N, plm.M, numPatterns);
        
        % Retrieve the bit-packed hologram
        frame = hologramPtr.Value;
    end

    % Function to create and bit-pack holograms from phase data
    function frame = BitpackHologramsGPU(phase)
%         validateattributes(phase, {'single'}, {'3d', '>=', 0, '<=', 1'});
        % Initialize an empty array to hold the bit-packed hologram
        numHolograms = size(phase, 3);
        frame = zeros(4*2*plm.N, 2*plm.M, 'uint8');
        
        % Prepare pointers to the phase data and the hologram array
        phasePtr = libpointer('singlePtr', phase);
        framePtr = libpointer('uint8Ptr', frame);
        
        % Bit-pack the holograms using the library function
        res = calllib('plmctrl', 'BitpackHologramsGPU', phasePtr, framePtr, plm.N, plm.M, numHolograms);
        fprintf("Bitpacked: %d\n", res);
        
        % Retrieve the bit-packed hologram
        frame = framePtr.Value;
    end

    % Function to create and bit-pack holograms from phase data
    function res = BitpackAndInsertGPU(phase, offset)
%         validateattributes(phase, {'single'}, {'3d', '>=', 0, '<=', 1'});
%         validateattributes(offset, {'numeric'}, {'scalar', 'nonnegative', 'integer'});

        % Initialize an empty array to hold the bit-packed hologram
        numPatterns = size(phase, 3);
        
        % Prepare pointers to the phase data and the hologram array
        phasePtr = libpointer('singlePtr', phase);
        
        % Bit-pack the holograms using the library function
        res = calllib('plmctrl', 'BitpackAndInsertGPU', phasePtr, plm.N, plm.M, numPatterns, offset);
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
