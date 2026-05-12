// Inverse of BitpackHologramsNIR_CS.hlsl — recovers 24 quantised phase planes
// from one NIR bitpacked frame ((3N+4) × 2M, R32_UINT, RGBA8 packed). Returns
// the per-column-parity LUT phase value for each recovered level — i.e. the
// phase the device actually produces, not the user's pre-quantised float.
//
// Per pixel (i, j) we sample the 6 cells of the 3×2 superpixel (skipping the
// 2 zero-padded leading columns), peel out bit n from byte n/8 of each cell,
// pack into a 6-bit code, and look up level_from_code[code]. Final phase is
// `phases[col_parity*32 + level]` (odd / even column LUT halves, 64 floats
// total — same buffer the bitpacker uses).
cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
    uint phase_stride;   // unused; matches bitpacker cbuffer layout
};

Texture2D<uint>           bitpacked       : register(t0);   // R32_UINT, (3N+4) × 2M
StructuredBuffer<float>   phases          : register(t1);   // 64 floats: odd[0..31] | even[32..63]
StructuredBuffer<int>     level_from_code : register(t2);   // 64 entries; -1 marks invalid

RWStructuredBuffer<float> phase_out       : register(u0);   // size N*M*num_holograms

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    uint j = DTid.y;
    if (i >= N || j >= M) return;

    uint col_parity = i % 2u;        // matches bitpacker: 0 = odd col (1-indexed), 1 = even col
    uint base_p     = 2u + 3u * i;   // skip 2 padding columns at the start

    // Sample 6 superpixel cells. Cell index follows the bitpacker:
    //   k = dx * 2 + (1 - dy)  for dx ∈ {0,1,2}, dy ∈ {0,1}
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
