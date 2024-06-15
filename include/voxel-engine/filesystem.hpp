#pragma once

// A simple class that can get the relative path of a file / folder with respect to the executable.
// NOTE : Makes the assumption that between the executable and the project root directory, there is NO OTHER folder with
// the name "voxel-engine".
class FileSystem
{
  public:
    static FileSystem &instance()
    {
        static FileSystem fs{};
        return fs;
    }

    inline std::string get_relative_path(const std::string_view path) const
    {
        return m_root_directory + std::string(path);
    }

    inline std::string executable_path() const
    {
        return std::filesystem::current_path().string();
    }

  private:
    explicit FileSystem();

  private:
    std::string m_root_directory{};
};