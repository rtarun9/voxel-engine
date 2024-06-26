-- Configure the workspace (i.e a VS solution that can contain multiple projects).
workspace "voxel-engine"
    configurations { "Debug", "Release" }

-- Setup compiler flags (used in all configurations).
flags {
    "FatalCompileWarnings", -- Treat warnings as errors
    "FatalLinkWarnings",    -- Treat linker warnings as errors
}

-- Define the projects
project("voxel-engine")
    architecture "x64"
	kind "ConsoleApp"
	toolset ("clang")
	language "C++"
	cppdialect "C++20"
	includedirs { "include", "external" }

	pchheader "include/pch.hpp"
	pchsource "src/pch.cpp"

	files { "src/**.cpp" }

	links { "d3d12", "dxgi", "dxcompiler.lib", "dxcompiler.dll", "dxil.dll" }

	-- In debug mode, the VX_DEBUG #define must be set.
	-- Also, in debug mode optimization is set to Debug, while in release it is for speed.
	filter "configurations:Debug"
		defines { "VX_DEBUG" }
		optimize "Debug"

	filter "configurations:Release"
		undefines { "VX_DEBUG" }
		optimize "Speed"
