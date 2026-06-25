#pragma once

// Compute shaders for bitpacking and unpacking the bitpacked frames.

namespace plm_shaders
{

// VIS bitpacker: 2x2 superpixel, 16-level phase quantisation.
static const char* const BitpackHologramsCS = R"HLSL(
cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
    uint phase_stride;   // N*M for distinct phases per hologram, 0 for shared phase
};

StructuredBuffer<float> phases : register(t0);
StructuredBuffer<int> phase_map : register(t1);
StructuredBuffer<float> phase : register(t2);

RWTexture2D<uint> hologram : register(u0);

uint QuantisePhase(float phaseVal)
{
    for (uint level = 0; level < 16; level++)
    {
        if (phaseVal >= phases[level] && phaseVal < phases[level + 1])
        {
            float diff1 = phaseVal - phases[level];
            float diff2 = phases[level + 1] - phaseVal;
            return (diff1 < diff2) ? level : (level + 1) % 16;
        }
    }
    return 0;
}


[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pos = DTid.xy;
    if (pos.x >= 2 * N || pos.y >= 2 * M)
        return;

    uint p = pos.x;
    uint q = pos.y;

    uint i = p / 2;
    uint j = q / 2;
    uint dx = p % 2;
    uint dy = q % 2;

    // Which of the four phase_map entries to use
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

    for (uint n = 0; n < num_holograms; n++)
    {
        uint color_id = n / 8; // R (0-7), G (8-15), B (16-23)
        uint offset = n % 8;
        float phase_val = phase[i + j * N + n * phase_stride];
        uint level = QuantisePhase(phase_val);
        uint bit = phase_map[level * 4 + k];

        color[color_id] |= (bit << offset);
    };

    hologram[pos] = color.r | color.g << 8 | color.b << 16 | color.a << 24;
}
)HLSL";


// NIR bitpacker: 3x2 superpixel, 32-level per-column-parity quantisation.
static const char* const BitpackHologramsNIR_CS = R"HLSL(
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

    uint p_data = p - 2;
    uint i = p_data / 3;
    uint j = q / 2;
    uint dx = p_data % 3;  // Sub-pixel column (0, 1, 2)
    uint dy = q % 2;       // Sub-pixel row (0, 1)

    // Column-major, bottom then top: k = dx*2 + (1-dy)
    uint k = dx * 2 + (1 - dy);

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
)HLSL";


// VIS unpacker: inverse of the VIS bitpacker. Recovers level bin centres.
static const char* const UnpackHologramsCS = R"HLSL(
cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
    uint phase_stride;   // unused; matches bitpacker cbuffer layout
};

Texture2D<uint>           bitpacked       : register(t0);   // R32_UINT, 2N x 2M
StructuredBuffer<int>     level_from_code : register(t1);   // 16 entries; -1 marks invalid code

RWStructuredBuffer<float> phase_out       : register(u0);   // size N*M*num_holograms

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    uint j = DTid.y;
    if (i >= N || j >= M) return;

    // Cell k convention: k0=(2i,2j+1) k1=(2i,2j) k2=(2i+1,2j+1) k3=(2i+1,2j)
    uint c0 = bitpacked.Load(int3(2 * i + 0, 2 * j + 1, 0));
    uint c1 = bitpacked.Load(int3(2 * i + 0, 2 * j + 0, 0));
    uint c2 = bitpacked.Load(int3(2 * i + 1, 2 * j + 1, 0));
    uint c3 = bitpacked.Load(int3(2 * i + 1, 2 * j + 0, 0));

    for (uint n = 0; n < num_holograms; n++)
    {
        uint byte_shift = (n / 8u) * 8u;     // 0 (R), 8 (G), 16 (B)
        uint bit_shift  = n % 8u;

        uint b0 = (c0 >> byte_shift) & 0xFFu;
        uint b1 = (c1 >> byte_shift) & 0xFFu;
        uint b2 = (c2 >> byte_shift) & 0xFFu;
        uint b3 = (c3 >> byte_shift) & 0xFFu;

        uint code = ((b0 >> bit_shift) & 1u)
                  | (((b1 >> bit_shift) & 1u) << 1)
                  | (((b2 >> bit_shift) & 1u) << 2)
                  | (((b3 >> bit_shift) & 1u) << 3);

        int level = level_from_code[code];
        float phase = (level >= 0) ? ((float)level + 0.5f) / 16.0f : 0.0f;
        phase_out[i + j * N + n * N * M] = phase;
    }
}
)HLSL";


// NIR unpacker: inverse of the NIR bitpacker. Returns per-parity LUT phase.
static const char* const UnpackHologramsNIR_CS = R"HLSL(
cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
    uint phase_stride;   // unused; matches bitpacker cbuffer layout
};

Texture2D<uint>           bitpacked       : register(t0);   // R32_UINT, (3N+4) x 2M
StructuredBuffer<float>   phases          : register(t1);   // 64 floats: odd[0..31] | even[32..63]
StructuredBuffer<int>     level_from_code : register(t2);   // 64 entries; -1 marks invalid

RWStructuredBuffer<float> phase_out       : register(u0);   // size N*M*num_holograms

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    uint j = DTid.y;
    if (i >= N || j >= M) return;

    uint col_parity = i % 2u;
    uint base_p     = 2u + 3u * i;   // skip 2 padding columns

    // Cell index follows the bitpacker: k = dx*2 + (1-dy)
    uint c[6];
    [unroll]
    for (uint dx = 0u; dx < 3u; dx++)
    {
        [unroll]
        for (uint dy = 0u; dy < 2u; dy++)
        {
            uint k = dx * 2u + (1u - dy);
            c[k] = bitpacked.Load(int3(base_p + dx, 2u * j + dy, 0));
        }
    }

    for (uint n = 0u; n < num_holograms; n++)
    {
        uint byte_shift = (n / 8u) * 8u;
        uint bit_shift  = n % 8u;

        uint code = 0u;
        [unroll]
        for (uint k = 0u; k < 6u; k++)
        {
            uint bit = ((c[k] >> byte_shift) >> bit_shift) & 1u;
            code |= (bit << k);
        }

        int level = level_from_code[code];
        float phase = 0.0f;
        if (level >= 0)
            phase = phases[col_parity * 32u + (uint)level];

        phase_out[i + j * N + n * N * M] = phase;
    }
}
)HLSL";

} // namespace plm_shaders
