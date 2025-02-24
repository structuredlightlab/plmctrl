cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
};

StructuredBuffer<float> phase : register(t0);
StructuredBuffer<float> phases : register(t1);
StructuredBuffer<int> phase_map : register(t2);

RWTexture2D<uint> hologram : register(u0);

// Quantize phase value to a level between 0 and 15
uint QuantisePhase(float phaseVal)
{
    for (uint level = 0; level < 16; level++)
    {
        if (phaseVal > phases[level] && phaseVal < phases[level + 1])
        {
            float diff1 = phaseVal - phases[level];
            float diff2 = phases[level + 1] - phaseVal;
            return (diff1 < diff2) ? level : (level + 1) % 16;
        }
    }
    return 0; // Default if outside range
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pos = DTid.xy;
    if (pos.x >= 2 * N || pos.y >= 2 * M)
        return;

    uint p = pos.x; // 2M
    uint q = pos.y; // 2N

    // Compute corresponding phase pixel indices
    uint i = p / 2;
    uint j = q / 2;
    uint dx = p % 2;
    uint dy = q % 2;

    // Determine which of the four phase_map entries to use
    uint k;
    if (dx == 0 && dy == 1)
        k = 0; // (2i, 2j+1)
    else if (dx == 0 && dy == 0)
        k = 1; // (2i, 2j)
    else if (dx == 1 && dy == 1)
        k = 2; // (2i+1, 2j+1)
    else if (dx == 1 && dy == 0)
        k = 3; // (2i+1, 2j)
    
    uint4 color = {0, 0, 0, 255};

    // Single loop over all holograms
    for (uint n = 0; n < num_holograms; n++)
    {
        uint color_id = n / 8; // 0 for R (n=0-7), 1 for G (n=8-15), 2 for B (n=16-23)
        uint offset = n % 8; // Bit position within the byte (0-7)
        float phase_val = phase[i + j * N + n * N * M];
        uint level = QuantisePhase(phase_val);
        uint bit = phase_map[level * 4 + k];
      
        color[color_id] |= (bit << offset);
    };

    hologram[pos] = color.r | color.g << 8 | color.b << 16 | color.a << 24;

}
