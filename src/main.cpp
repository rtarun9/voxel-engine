#include <stdio.h>

#include "types.hpp"
#include "window.hpp"

LRESULT CALLBACK window_proc(HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param);

int main()
{
    Window window = Window(1080u, 720u);
    if (window.m_handle == nullptr)
    {
        return -1;
    }

    bool quit = false;
    while (!quit)
    {
        MSG message = {};
        if (PeekMessageA(&message, NULL, 0u, 0u, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }

        if (message.message == WM_QUIT)
        {
            quit = true;
        }
    }

    return 0;
}

