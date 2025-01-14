#pragma once

class camera_t
{
  public:
    DirectX::XMMATRIX update_and_get_view_matrix(const f32 delta_time);

  public:
    DirectX::XMFLOAT4 m_position{0.0f, 0.0f, -5.0f, 1.0f};

    // The up vector can be calculated using right and front vector.
    DirectX::XMFLOAT4 m_right{1.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 m_front{0.0f, 0.0f, 1.0f, 0.0f};

    f32 m_movement_speed{500.0f};
    f32 m_rotation_speed{1.0f};

    // Used to determine how 'smooth' the camera behaves.
    // For now, both rotation and movement use the same friction value, purely for simplicity.
    f32 m_friction{0.30f};

    f32 m_pitch{};
    f32 m_yaw{};
};