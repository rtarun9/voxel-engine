#include "pch.hpp"

#include "window.hpp"

static LRESULT CALLBACK window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
        // Handle case when the close button / alt f4 is pressed.
    case WM_CLOSE: {
        DestroyWindow(window_handle);
    }
    break;

    // Handle case when destroy window is called.
    case WM_DESTROY: {
        // This adds a WM_QUIT message to the queue.
        PostQuitMessage(0);
        return 0;
    }
    break;

    case WM_KEYDOWN: {
        if (w_param == VK_ESCAPE)
        {
            PostQuitMessage(0);
            return 0;
        }
    }
    break;
    }

    return DefWindowProcA(window_handle, message, w_param, l_param);
}

Window::Window(const u16 width, const u16 height) : m_width(width), m_height(height)
{
    // Register the window class.
    // This represents a set of common behavious that several windows may have.
    const HINSTANCE instance_handle = GetModuleHandle(NULL);

    const WNDCLASSA window_class = {
        .lpfnWndProc = window_proc,
        .hInstance = instance_handle,
        .lpszClassName = WINDOW_CLASS_NAME,
    };

    RegisterClassA(&window_class);

    m_handle = CreateWindowExA(0, WINDOW_CLASS_NAME, "voxel-engine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               m_width, m_height, NULL, NULL, instance_handle, NULL);

    if (m_handle != 0)
    {
        ShowWindow(m_handle, SW_SHOW);
    }
    else
    {
        printf("Failed to create win32 window.");
        exit(EXIT_FAILURE);
    }
}

Window::~Window()
{
    UnregisterClassA(WINDOW_CLASS_NAME, GetModuleHandle(NULL));
}
