#include "voxel-engine/filesystem.hpp"

FileSystem::FileSystem()
{
    // Start from the executable directory, and keep moving up until you find the project root directory.
    std::filesystem::path current_path = std::filesystem::current_path();
    while (!std::filesystem::exists(current_path / "voxel-engine"))
    {
        if (current_path.has_parent_path())
        {
            current_path = current_path.parent_path();
        }
        else
        {
            printf("Failed to find root directory voxel-engine.");
        }
    }

    m_root_directory = (current_path / "voxel-engine/").string();

    printf("Root directory :: %s\n", m_root_directory.c_str());
}