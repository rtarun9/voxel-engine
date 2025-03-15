workspace  "voxel-engine"
    architecture "x86_64"
    configurations { "Debug", "Release" }

    project "voxel-engine"
        kind "ConsoleApp"
        language "C++"
        cppdialect "C++20"

        includedirs {
            "include",
            "include/voxel-engine",
            "include/voxel-engine/pch.hpp"
        }
        
        files {
            "include/voxel-engine/rhi/buffers.hpp",
            "include/voxel-engine/rhi/command_queue.hpp",
            "include/voxel-engine/rhi/common.hpp",
            "include/voxel-engine/rhi/descriptor_heap.hpp",
            "include/voxel-engine/rhi/renderer.hpp",

            "include/voxel-engine/camera.hpp",
            "include/voxel-engine/common.hpp",
            "include/voxel-engine/filesystem.hpp",
            "include/voxel-engine/shader_compiler.hpp",
            "include/voxel-engine/thread_pool.hpp",
            "include/voxel-engine/timer.hpp",
            "include/voxel-engine/types.hpp",
            "include/voxel-engine/voxel.hpp",
            "include/voxel-engine/window.hpp",

            "src/main.cpp",
            "src/window.cpp",
            "src/timer.cpp",
            "src/filesystem.cpp",
            "src/camera.cpp",
            "src/rhi/renderer.cpp",
            "src/rhi/descriptor_heap.cpp",
            "src/rhi/command_queue.cpp",
            "src/shader_compiler.cpp",
            "src/voxel.cpp",

            "src/pch.cpp"
        } 

        pchheader "pch.hpp"
        pchsource "src/pch.cpp"

        links {
            "d3d12", "dxgi", "dxguid", "dxcompiler"
        }

        filter "configurations:Debug"
            targetdir "build/debug/bin"
            objdir "build/debug/obj"
            symbols "On"
            warnings "Extra"
            defines { "DEF_VX_DEBUG" }
            optimize "Debug"

        filter "configurations:Release"
            targetdir "build/release/bin"
            objdir "build/release/obj"
            optimize "On"
