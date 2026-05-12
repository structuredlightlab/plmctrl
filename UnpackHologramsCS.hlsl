// Inverse of BitpackHologramsCS.hlsl — recovers the 24 quantised phase planes
// from a single VIS bitpacked frame (2N × 2M, R32_UINT carrying RGBA8 packed
// as R | G<<8 | B<<16 | A<<24).
//
// For each pixel (i, j) of the phase grid we read the 2×2 superpixel and, for
// each hologram n in [0, num_holograms):
//   1. Pull bit n out of the colour byte n/8 at each of the 4 cells.
//   2. Pack those 4 bits into a 4-bit code [k0 .. k3].
//   3. Look up `level_from_code[code]` to recover the quantisation level.
//   4. Emit phase ≈ (level + 0.5) / 16 — bin centre.
cbuffer Constants : register(b0)
{
    uint N;
    uint M;
    uint num_holograms;
    uint phase_stride;   // unused here; kept so the constant buffer matches the bitpacker layout
};

Texture2D<uint>           bitpacked       : register(t0);   // R32_UINT, 2N × 2M
StructuredBuffer<int>     level_from_code : register(t1);   // 16 entries; -1 marks an invalid code

RWStructuredBuffer<float> phase_out       : register(u0);   // size N*M*num_holograms

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint i = DTid.x;
    uint j = DTid.y;
    if (i >= N || j >= M) return;

    // Cell coordinates per the VIS bitpacker's k convention:
    //   k=0: (2i,   2j+1)   k=1: (2i,   2j)
    //   k=2: (2i+1, 2j+1)   k=3: (2i+1, 2j)
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
