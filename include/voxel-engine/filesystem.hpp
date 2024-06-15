#pragma once

// A simple class that can get the relative path of a file / folder with respect to the executable.
// NOTE : Makes the assumption that between the executable and the project root directory, there is NO OTHER folder with
// the name "voxel-engine".
class FileSystem
{
  public:
    explicit FileSystem();

    inline std::string get_relative_path(const std::string_view path) const
    {
        return m_root_directory + std::string(path);
    }

  private:
    std::string m_root_directory{};
};