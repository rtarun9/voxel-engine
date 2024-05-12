#include "pch.hpp"

#include "camera.hpp"

DirectX::XMMATRIX Camera::update_and_get_view_matrix(const float delta_time)
{
    const DirectX::XMVECTOR camera_right =
        DirectX::XMVector4Normalize(DirectX::XMVector3Cross(camera_front, camera_up_direction));

    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;

    // Note : Index is the virutal key code.
    // If higher order bit is 1 (0x8000), the key is down.
    if (auto key_status = GetKeyState((int)'A'); key_status & 0x8000)
    {
        camera_position += camera_right * camera_movement_speed * delta_time;
    }
    else if (auto key_status = GetKeyState((int)'D'); key_status & 0x8000)
    {
        camera_position -= camera_right * camera_movement_speed * delta_time;
    }

    if (auto key_status = GetKeyState((int)'W'); key_status & 0x8000)
    {
        camera_position += camera_front * camera_movement_speed * delta_time;
    }
    else if (auto key_status = GetKeyState((int)'S'); key_status & 0x8000)
    {
        camera_position -= camera_front * camera_movement_speed * delta_time;
    }

    if (auto key_status = GetKeyState(VK_RIGHT); key_status & 0x8000)
    {
        yaw += camera_rotation_speed * delta_time;
    }

    if (auto key_status = GetKeyState(VK_LEFT); key_status & 0x8000)
    {
        yaw -= camera_rotation_speed * delta_time;
    }

    if (auto key_status = GetKeyState(VK_UP); key_status & 0x8000)
    {
        pitch -= camera_rotation_speed * delta_time;
    }

    if (auto key_status = GetKeyState(VK_DOWN); key_status & 0x8000)
    {
        pitch += camera_rotation_speed * delta_time;
    }

    const DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, roll);

    camera_front = DirectX::XMVector3Normalize(DirectX::XMVector4Transform(camera_front, rotation_matrix));
    camera_up_direction =
        DirectX::XMVector3Normalize(DirectX::XMVector3Transform(camera_up_direction, rotation_matrix));

    // Setup of simple view projection matrix.
    return DirectX::XMMatrixLookAtLH(camera_position, DirectX::XMVectorAdd(camera_position, camera_front),
                                     camera_up_direction);
}
