#pragma once

#ifdef _DEBUG
constexpr bool VX_DEBUG_MODE = true;
#else
constexpr bool VX_DEBUG_MODE = false;
#endif

// Windows includes.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Dx12 / Com headers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Shader compiler related headers.
#include <d3d12shader.h>
#include <dxcapi.h>

// Simd - math library.
#include <DirectXMath.h>

// stdlib headers.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <array>
#include <filesystem>
#include <queue>
#include <set>
#include <source_location>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Custom includes.
#include "common.hpp"
#include "types.hpp"