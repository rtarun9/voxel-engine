#pragma once

#include "types.hpp"

// Simple abstraction class over win32 window.
struct Window
{
    // For now window is only created in full screen mode.
    Window();

    ~Window();

    static inline constexpr const char WINDOW_CLASS_NAME[] = "Base Window Class";

    HWND m_handle{nullptr};

    u16 m_width{};
    u16 m_height{};
};