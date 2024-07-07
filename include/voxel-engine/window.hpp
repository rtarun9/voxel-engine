#pragma once

// Simple abstraction class over win32 window.
class Window
{
  public:
    // For now window is only created in full screen mode.
    explicit Window();
    virtual ~Window();

    inline u16 get_width() const
    {
        return m_width;
    }

    inline u16 get_height() const
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

    u16 m_width{};
    u16 m_height{};
};