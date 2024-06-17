#pragma once

class Camera
{
  public:
    DirectX::XMMATRIX update_and_get_view_matrix(const float delta_time);

  private:
    DirectX::XMFLOAT3 m_position{0.0f, 0.0f, -5.0f};

    DirectX::XMFLOAT3 m_right{1.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 m_up{0.0f, 1.0f, 0.0f};
    DirectX::XMFLOAT3 m_front{0.0f, 0.0f, 1.0f};

  public:
    float m_movement_speed{1.0f};
    float m_rotation_speed{1.0f};

    float m_pitch{};
    float m_yaw{};
    float m_roll{};
};