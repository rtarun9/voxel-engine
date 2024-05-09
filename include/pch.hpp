#pragma once

// Windows headers.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// Dx12 / Com headers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// note(rtarun9) : Use the dx shader compiler api in the future instead of d3dcompiler.h.
#include <d3dcompiler.h>

// Simd - math library.
#include <DirectXMath.h>

// stdlib includes.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <source_location>
#include <vector>
