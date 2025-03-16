#pragma once

class timer_t
{
  public:
    explicit timer_t();

    // To be called at end of each frame.
    // Computed correct value of start and end time.
    f32 tick_and_get_delta_time_seconds();

  private:
    LARGE_INTEGER m_performance_frequency{};
    f32 m_seconds_per_count{};

    LARGE_INTEGER m_start_time{};
    LARGE_INTEGER m_end_time{};
};
