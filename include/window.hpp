#pragma once

// Simple abstraction over a win32 window.

#include "types.hpp"

struct Window
{
    Window(const u16 width, const u16 height);
    ~Window();

    u16 m_width{};
    u16 m_height{};

    HWND m_handle{nullptr};
};