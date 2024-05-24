#pragma once

#include "types.hpp"

#ifdef VX_DEBUG
constexpr bool VX_DEBUG_MODE = true;
#else
constexpr bool VX_DEBUG_MODE = false;
#endif

#define USE(x) (void)x

// Helper function to print to console in debug mode if the passed Hresult has failed.
inline void throw_if_failed(const HRESULT hr, const std::source_location src_loc = std::source_location::current())
{
    if constexpr (VX_DEBUG_MODE)
    {
        if (FAILED(hr))
        {
            printf("Hresult Failed!\nFile name :: %s\nLine number :: %u\nColumn number :: %u\nFunction name :: %s\n",
                   src_loc.file_name(), src_loc.line(), src_loc.column(), src_loc.function_name());
        }
    }
}

// Helper function for indexing.
// N is the max number of 'elements' in any direction.
// If the grid can have -ve values (such as the chunks in the grid), the calling code has to subtract N / 2 from the 3d
// index.
inline DirectX::XMUINT3 convert_index_to_3d(const u64 index, const u32 N)
{
    // Note that index = x + y * N + z * N * N;
    const u32 z = index / (N * N);
    const u32 index_2d = index - z * N * N;
    const u32 y = index_2d / N;
    const u32 x = index_2d % N;

    return DirectX::XMUINT3{x, y, z};
}

// The optional bias value, is added to each element of the index 3d vector.
inline u64 convert_index_to_1d(const DirectX::XMINT3 index, const u32 N, const u32 bias_value = 0u)
{
    return (u64)((i32)bias_value + index.x) + (u64)((i32)bias_value + index.y) * N +
           (u64)((i32)bias_value + index.z) * N * N;
}
