#include "pch.hpp"

#include "camera.hpp"

DirectX::XMMATRIX Camera::update_and_get_view_matrix(const float delta_time)
{
    const DirectX::XMVECTOR camera_right =
        DirectX::XMVector3Normalize(DirectX::XMVector3Cross(camera_front, camera_up_direction));

    // Index is the virutal key code.
    // If higher order bit is 1 (0x8000), the key is down.
    if (GetKeyState((int)'A') & 0x8000)
    {
        camera_position += camera_right * camera_movement_speed * delta_time;
    }
    else if (GetKeyState((int)'D') & 0x8000)
    {
        camera_position -= camera_right * camera_movement_speed * delta_time;
    }

    if (GetKeyState((int)'W') & 0x8000)
    {
        camera_position += camera_front * camera_movement_speed * delta_time;
    }
    else if (GetKeyState((int)'S') & 0x8000)
    {
        camera_position -= camera_front * camera_movement_speed * delta_time;
    }

    if (GetKeyState(VK_RIGHT) & 0x8000)
    {
        yaw += camera_rotation_speed * delta_time;
    }

    if (GetKeyState(VK_LEFT) & 0x8000)
    {
        yaw -= camera_rotation_speed * delta_time;
    }

    if (GetKeyState(VK_UP) & 0x8000)
    {
        pitch -= camera_rotation_speed * delta_time;
    }

    if (GetKeyState(VK_DOWN) & 0x8000)
    {
        pitch += camera_rotation_speed * delta_time;
    }

    static constexpr DirectX::XMVECTOR world_up = DirectX::XMVECTOR{0.0f, 1.0f, 0.0f, 0.0f};
    static constexpr DirectX::XMVECTOR world_front = DirectX::XMVECTOR{0.0f, 0.0f, 1.0f, 1.0f};

    const DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);

    camera_front = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_front, rotation_matrix));
    camera_up_direction = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_up, rotation_matrix));

    // Setup of simple view matrix.
    return DirectX::XMMatrixLookAtLH(camera_position, DirectX::XMVectorAdd(camera_position, camera_front),
                                     camera_up_direction);
}
