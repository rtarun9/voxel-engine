#pragma once

// A simple camera class.
struct Camera
{
    float camera_movement_speed = 10.0f;
    float camera_rotation_speed = 0.5f;

    float pitch = 0.0f;
    float yaw = 0.0f;

    // From the following vectors, camera right can be computed on the fly.
    DirectX::XMVECTOR camera_up_direction = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    DirectX::XMVECTOR camera_position = DirectX::XMVectorSet(0.0f, 0.0f, -5.0f, 1.0f);
    DirectX::XMVECTOR camera_front = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);

    DirectX::XMMATRIX update_and_get_view_matrix(const float delta_time);
};
