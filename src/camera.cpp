#include "pch.hpp"

#include "camera.hpp"

DirectX::XMMATRIX Camera::update_and_get_view_matrix(const float delta_time) noexcept
{
    const DirectX::XMVECTOR camera_right =
        DirectX::XMVector3Normalize(DirectX::XMVector3Cross(m_camera_front, m_camera_up_direction));

    // Index is the virutal key code.
    // If higher order bit is 1 (0x8000), the key is down.
    if (GetKeyState((int)'A') & 0x8000)
    {
        m_camera_position += camera_right * m_camera_movement_speed * delta_time;
    }
    else if (GetKeyState((int)'D') & 0x8000)
    {
        m_camera_position -= camera_right * m_camera_movement_speed * delta_time;
    }

    if (GetKeyState((int)'W') & 0x8000)
    {
        m_camera_position += m_camera_front * m_camera_movement_speed * delta_time;
    }
    else if (GetKeyState((int)'S') & 0x8000)
    {
        m_camera_position -= m_camera_front * m_camera_movement_speed * delta_time;
    }

    if (GetKeyState(VK_RIGHT) & 0x8000)
    {
        m_yaw += m_camera_rotation_speed * delta_time;
    }
    else if (GetKeyState(VK_LEFT) & 0x8000)
    {
        m_yaw -= m_camera_rotation_speed * delta_time;
    }

    if (GetKeyState(VK_UP) & 0x8000)
    {
        m_pitch -= m_camera_rotation_speed * delta_time;
    }
    else if (GetKeyState(VK_DOWN) & 0x8000)
    {
        m_pitch += m_camera_rotation_speed * delta_time;
    }

    static constexpr DirectX::XMVECTOR world_up = DirectX::XMVECTOR{0.0f, 1.0f, 0.0f, 0.0f};
    static constexpr DirectX::XMVECTOR world_front = DirectX::XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f};

    const DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);

    m_camera_front = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_front, rotation_matrix));
    m_camera_up_direction = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_up, rotation_matrix));

    // Setup of simple view matrix.
    return DirectX::XMMatrixLookAtLH(m_camera_position, DirectX::XMVectorAdd(m_camera_position, m_camera_front),
                                     m_camera_up_direction);
}
