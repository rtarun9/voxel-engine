#pragma once

class Camera
{
  public:
    DirectX::XMMATRIX update_and_get_view_matrix(const float delta_time);

  public:
    DirectX::XMFLOAT4 m_position{0.0f, 0.0f, -5.0f, 1.0f};

    // The up vector can be calculated using right and front vector.
    DirectX::XMFLOAT4 m_right{1.0f, 0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 m_front{0.0f, 0.0f, 1.0f, 0.0f};

    float m_movement_speed{50.0f};
    float m_rotation_speed{1.0f};

    float m_pitch{};
    float m_yaw{};
};