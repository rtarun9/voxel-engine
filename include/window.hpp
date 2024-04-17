#pragma once

// Simple abstraction over a win32 window.

#include "common.hpp"
#include "types.hpp"

struct Window
{
    Window(const u16 width, const u16 height);

    u16 m_width{};
    u16 m_height{};

    HWND m_handle{nullptr};
};