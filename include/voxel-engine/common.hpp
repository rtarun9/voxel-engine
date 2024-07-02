#pragma once

#include "types.hpp"

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

// Helper functions to go from 1d to 3d and vice versa.
static inline size_t convert_to_1d(const DirectX::XMUINT3 index_3d, const size_t N)
{
    return index_3d.x + N * (index_3d.y + (index_3d.z * N));
}

static inline DirectX::XMUINT3 convert_to_3d(const size_t index, const size_t N)
{
    // For reference, index = x + y * N + z * N * N.
    const u32 z = static_cast<u32>(index / (N * N));
    const u32 index_2d = static_cast<u32>(index - z * N * N);
    const u32 y = static_cast<u32>(index_2d / N);
    const u32 x = static_cast<u32>(index_2d % N);

    return {x, y, z};
}

static inline void name_d3d12_object(ID3D12Object *const object, const std::wstring_view name)
{
    if constexpr (VX_DEBUG_MODE)
    {
        object->SetName(std::wstring(name).c_str());
    }
}

static inline size_t round_up_to_multiple(size_t a, size_t multiple)
{
    if (a % multiple == 0)
    {
        return a;
    }
    else
    {
        return a + multiple - (a % multiple);
    }
}
