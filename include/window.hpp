#pragma once

#include "types.hpp"

// Simple abstraction class over win32 window.
struct Window
{
    // The width and height is based on percentage of the screen width and height.
    Window(const float width_percentage, const float height_percentage);

    ~Window();

    static inline constexpr const char WINDOW_CLASS_NAME[] = "Base Window Class";

    HWND m_handle{nullptr};

    u16 m_width{};
    u16 m_height{};
};