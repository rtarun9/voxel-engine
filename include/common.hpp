#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdio.h>

#ifdef VX_DEBUG
constexpr bool VX_DEBUG_MODE = true;
#else
constexpr bool VX_DEBUG_MODE = false;
#endif
