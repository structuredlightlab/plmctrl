function slm = SLMController(MAX_HOLOGRAMS, width, height)
    % SLMController: Initializes and manages a controller for SLM hardware.
    %
    % This function creates a structure `slm` with various fields and methods
    % to control a spatial light modulator (SLM). It manages loading the necessary
    % library, setting up the SLM, inserting holograms, setting sequences, and
    % handling cleanup.
    %
    % Inputs:
    %   - MAX_HOLOGRAMS: The maximum number of frames that the SLM can handle.
    %   - width: Width of the SLM display in pixels.
    %   - height: Height of the SLM display in pixels.
    %
    % Outputs:
    %   - slm: A structure containing SLM control methods and properties.

    % Assign the maximum number of frames, width, and height to the `slm` structure
    slm.MAX_HOLOGRAMS = MAX_HOLOGRAMS;
    slm.N = width;
    slm.M = height;
    
    % Load the 'slmctrl' library if it is not already loaded
    if ~libisloaded('slmctrl')
        [~, ~] = loadlibrary('slmctrl', 'slmctrl.h');
    end
    
    % Assign functions to the `slm` structure for controlling the SLM
    slm.StartUI = @StartUI;
    slm.InsertHolograms = @InsertHolograms;
    slm.SetHologramSequence = @SetHologramSequence;
    slm.StartSequence = @StartSequence;
    slm.StopUI = @StopUI;
    slm.SetLookupTable = @SetLookupTable;
    slm.SetHologram = @SetHologram;
    slm.CreateHolograms = @CreateHolograms;
    slm.QuerySLMMode = @QuerySLMMode;
    slm.QueryCameraTrigger = @QueryCameraTrigger;
    slm.QueryBufferIndex = @QueryBufferIndex;
    slm.GetT0 = @GetT0;
    slm.ResetUI = @ResetUI;
    slm.Cleanup = @cleanup;

    function StartUI(monitor_id)
        validateattributes(monitor_id, {'numeric'}, {'scalar', 'positive', 'integer'});
        
        calllib('slmctrl', 'SetSLMWindowPos', slm.N, slm.M, monitor_id);
        calllib('slmctrl', 'StartUI', slm.MAX_HOLOGRAMS);
    end

    function res = InsertHolograms(holograms, offset)
        validateattributes(holograms, {'uint8'}, {'3d'});
        validateattributes(offset, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        
        res = calllib('slmctrl', 'InsertSLMHologram', libpointer('uint8Ptr', holograms), size(holograms, 3), offset);
    end

    function SetHologramSequence(sequence)
        validateattributes(sequence, {'numeric'}, {'vector', 'integer', 'nonnegative'});
        calllib('slmctrl', 'SetHologramSequence', libpointer('uint64Ptr', sequence), length(sequence));
    end

    function res = StartSequence(holograms_to_display)
        validateattributes(holograms_to_display, {'numeric'}, {'scalar', 'positive', 'integer'});
        res = calllib('slmctrl', 'StartSequence', holograms_to_display);
    end

    function StopUI()
        calllib('slmctrl', 'StopUI');
    end

    function SetLookupTable(phase_levels)
        validateattributes(phase_levels, {'double'}, {'vector', '>=', 0, '<=', 1'});
        calllib('slmctrl', 'SetLookupTable', libpointer('doublePtr', phase_levels));
    end

    function res = SetHologram(hologram_index)
        validateattributes(hologram_index, {'numeric'}, {'scalar', 'nonnegative', 'integer'});
        res = calllib('slmctrl', 'SetSLMHologram', hologram_index);
    end

    function holograms = CreateHolograms(phase)
        validateattributes(phase, {'double'}, {'3d', '>=', 0, '<=', 1'});
        numHolograms = size(phase, 3);
        holograms = zeros(slm.N, slm.M, numHolograms, 'uint8');
        
        phasePtr = libpointer('doublePtr', phase);
        hologramsPtr = libpointer('uint8Ptr', holograms);
        
        calllib('slmctrl', 'CreateHolograms', phasePtr, hologramsPtr, slm.N, slm.M, numHolograms);
        
        holograms = hologramsPtr.Value;
    end

    function mode = QuerySLMMode()
        mode = calllib('slmctrl', 'QueryPLMMode');
    end

    function trigger = QueryCameraTrigger()
        trigger = calllib('slmctrl', 'QueryCameraTrigger');
    end

    function index = QueryBufferIndex()
        index = calllib('slmctrl', 'QueryBufferIndex');
    end

    function t0 = GetT0()
        t0 = calllib('slmctrl', 'GetT0');
    end

    function ResetUI()
        calllib('slmctrl', 'ResetUI');
    end

    function cleanup()
        calllib('slmctrl', 'StopUI');
        if libisloaded('slmctrl')
            unloadlibrary('slmctrl');
        end
    end
end