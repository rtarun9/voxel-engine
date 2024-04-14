#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "types.hpp"

int main()
{
    // Register the window class.
    // This represents a set of common behavious that several windows may have.
    const char WINDOW_CLASS_NAME[] = "Base Window Class";

    const WNDCLASSA window_class = {
        .lpfnWndProc = DefWindowProcA,
        .hInstance = GetModuleHandle(NULL),
        .lpszClassName = WINDOW_CLASS_NAME,
    };

    RegisterClassA(&window_class);

    const u16 window_width = 1920u;
    const u16 window_height = 1080u;

    const HWND window_handle =
        CreateWindowExA(0, WINDOW_CLASS_NAME, "voxel-engine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        window_width, window_height, NULL, NULL, GetModuleHandle(NULL), NULL);

    if (window_handle == 0)
    {
        printf("Failed to create win32 window.");
        return -1;
    }

    ShowWindow(window_handle, SW_SHOW);

    while (true)
    {
    }

    return 0;
}
