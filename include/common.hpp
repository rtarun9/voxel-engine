#pragma once

#ifdef VX_DEBUG
constexpr bool VX_DEBUG_MODE = true;
#else
constexpr bool VX_DEBUG_MODE = false;
#endif

// Helper function to print to console in debug mode if the passed Hresult fails.
inline void throw_if_failed(const HRESULT hr, const std::source_location src_loc = std::source_location::current())
{
    if constexpr (VX_DEBUG_MODE)
	{
		if (FAILED(hr))
		{
			printf("Hresult Failed!\nFile name :: %s\nLine number :: %u\nColumn number :: %u\nFunction name :: %s\n", src_loc.file_name(), src_loc.line(), src_loc.column(), src_loc.function_name());
		}
	}
}
