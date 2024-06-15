#include "voxel-engine/filesystem.hpp"
#include "voxel-engine/timer.hpp"
#include "voxel-engine/window.hpp"

int main()
{
    const Window window{};
    const FileSystem file_system{};
    Timer timer{};

    bool quit{false};

    while (!quit)
    {
        timer.start();

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

        timer.stop();
    }

    return 0;
}