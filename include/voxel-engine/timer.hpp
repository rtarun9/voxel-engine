#pragma once

class Timer
{
  public:
    explicit Timer();

    void start();
    void stop();

    float get_delta_time() const;

  private:
    LARGE_INTEGER m_performance_frequency{};
    float m_seconds_per_count{};

    LARGE_INTEGER m_start_time{};
    LARGE_INTEGER m_end_time{};
};
