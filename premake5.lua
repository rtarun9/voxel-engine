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
	includedirs { "include" }

	files { "src/**.cpp", "include/**.hpp" }

	-- In debug mode, the VX_DEBUG #define must be set.
	-- Also, in debug mode optimization is set to Debug, while in release it is for speed.
	filter "configurations:Debug"
		defines { "VX_DEBUG" }
		optimize "Debug"

	filter "configurations:Release"
		undefines { "VX_DEBUG" }
		optimize "Speed"
