#include "pch.hpp"

#include "types.hpp"
#include "window.hpp"

int main()
{
    Window window(1080u, 720u);

    if (window.m_handle == nullptr)
    {
        printf("Failed to create win32 window.");
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

