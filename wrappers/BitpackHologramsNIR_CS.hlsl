cbuffer Constants : register(b0)
{
    uint N;              // Physical pixel width (904 for NIR)
    uint M;              // Physical pixel height (800 for NIR)
    uint num_holograms;
    uint phase_stride;   // N*M for distinct phases per hologram, 0 for shared phase
};

StructuredBuffer<float> phase : register(t0);
StructuredBuffer<float> phases : register(t1);     // [0..31] odd-col LUT, [32..63] even-col LUT
StructuredBuffer<int> phase_map : register(t2);    // 32 levels x 6 cells per 3x2 superpixel

RWTexture2D<uint> hologram : register(u0);

// Min-distance quantization using per-column-parity empirical LUT
uint QuantisePhaseNIR(float phaseVal, uint col_parity)
{
    uint base = col_parity * 32;
    float min_dist = 2.0;
    uint best_level = 0;

    for (uint l = 0; l < 32; l++)
    {
        float dist = abs(phaseVal - phases[base + l]);
        if (dist < min_dist)
        {
            min_dist = dist;
            best_level = l;
        }
    }

    return best_level;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pos = DTid.xy;
    uint holo_width = 3 * N + 4;
    uint holo_height = 2 * M;

    if (pos.x >= holo_width || pos.y >= holo_height)
        return;

    uint p = pos.x;
    uint q = pos.y;

    // Zero-padded columns: first 2 and last 2
    if (p < 2 || p >= holo_width - 2)
    {
        hologram[pos] = 255u << 24; // Alpha only
        return;
    }

    // Physical pixel coordinates
    uint p_data = p - 2;
    uint i = p_data / 3;
    uint j = q / 2;
    uint dx = p_data % 3;  // Sub-pixel column (0, 1, 2)
    uint dy = q % 2;       // Sub-pixel row (0, 1)

    // Cell index matching CPU convention (column-major, bottom then top):
    // k=0: dx=0,dy=1  k=1: dx=0,dy=0  k=2: dx=1,dy=1
    // k=3: dx=1,dy=0  k=4: dx=2,dy=1  k=5: dx=2,dy=0
    uint k = dx * 2 + (1 - dy);

    // Odd/even column parity (0 = odd in 1-indexed, 1 = even in 1-indexed)
    uint col_parity = i % 2;

    uint4 color = {0, 0, 0, 255};

    for (uint n = 0; n < num_holograms; n++)
    {
        uint color_id = n / 8;
        uint offset = n % 8;
        float phase_val = phase[i + j * N + n * phase_stride];
        uint level = QuantisePhaseNIR(phase_val, col_parity);
        uint bit = phase_map[level * 6 + k];

        color[color_id] |= (bit << offset);
    };

    hologram[pos] = color.r | color.g << 8 | color.b << 16 | color.a << 24;
}
