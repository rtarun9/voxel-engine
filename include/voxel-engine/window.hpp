#pragma once

// Simple abstraction class over win32 window.
class window_t
{
  public:
    // For now window is only created in full screen mode.
    explicit window_t();
    virtual ~window_t();

    inline u32 get_width() const
    {
        return m_width;
    }

    inline u32 get_height() const
    {
        return m_height;
    }

    inline HWND get_handle() const
    {
        return m_handle;
    }

  private:
    static inline constexpr const char WINDOW_CLASS_NAME[] = "Base Window Class";

  private:
    HWND m_handle{nullptr};

    u32 m_width{};
    u32 m_height{};
};