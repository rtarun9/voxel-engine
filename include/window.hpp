#pragma once

#include "types.hpp"

// Simple abstraction class over win32 window.
struct Window
{
    Window(const u16 width, const u16 height);
    ~Window();

    static inline constexpr const char WINDOW_CLASS_NAME[] = "Base Window Class";

    HWND m_handle{nullptr};

    u16 m_width{};
    u16 m_height{};
};