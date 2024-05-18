#pragma once

// A simple camera class.
struct Camera
{
    float m_camera_movement_speed{50.0f};
    float m_camera_rotation_speed{1.0f};

    float m_pitch{0.0f};
    float m_yaw{0.0f};

    // From the following vectors, camera right can be computed on the fly.
    DirectX::XMVECTOR m_camera_up_direction{DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)};
    DirectX::XMVECTOR m_camera_position{DirectX::XMVectorSet(0.0f, 0.0f, -5.0f, 1.0f)};
    DirectX::XMVECTOR m_camera_front{DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f)};

    DirectX::XMMATRIX update_and_get_view_matrix(const float delta_time);
};
