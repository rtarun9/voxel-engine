#include "voxel-engine/camera.hpp"

DirectX::XMMATRIX Camera::update_and_get_view_matrix(const float delta_time)
{
    // For operator overloads.
    using namespace DirectX;

    const float movement_speed = m_movement_speed * delta_time;
    const float rotation_speed = m_rotation_speed * delta_time;

    // For making the camera 'smooth', the yaw / pitch / position vector values are not set based on the players input
    // at a particular instance. Instead, these values lerp to the new values. The static variables help persist that
    // data between multiple frames / instances.
    static DirectX::XMVECTOR move_to_position_vector = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    static float pitch_to = 0.0f;
    static float yaw_to = 0.0f;

    // First load data into SIMD datatypes.
    DirectX::XMVECTOR position_vector = DirectX::XMLoadFloat4(&m_position);

    DirectX::XMVECTOR front_vector = DirectX::XMLoadFloat4(&m_front);
    DirectX::XMVECTOR right_vector = DirectX::XMLoadFloat4(&m_right);

    // Index is the virutal key code.
    // If higher order bit is 1 (0x8000), the key is down.
    if (GetKeyState((int)'A') & 0x8000)
    {
        move_to_position_vector -= right_vector * movement_speed;
    }

    if (GetKeyState((int)'D') & 0x8000)
    {
        move_to_position_vector += right_vector * movement_speed;
    }

    if (GetKeyState((int)'W') & 0x8000)
    {
        move_to_position_vector += front_vector * movement_speed;
    }

    if (GetKeyState((int)'S') & 0x8000)
    {
        move_to_position_vector -= front_vector * movement_speed;
    }

    if (GetKeyState(VK_UP) & 0x8000)
    {
        pitch_to -= rotation_speed;
    }
    else if (GetKeyState(VK_DOWN) & 0x8000)
    {
        pitch_to += rotation_speed;
    }

    if (GetKeyState(VK_LEFT) & 0x8000)
    {
        yaw_to -= rotation_speed;
    }
    else if (GetKeyState(VK_RIGHT) & 0x8000)
    {
        yaw_to += rotation_speed;
    }

    // The static variables are lerped (between current value and 0, with the 'lerp factor' being the friction member
    // variable).
    pitch_to = std::lerp(pitch_to, 0.0f, m_friction);
    yaw_to = std::lerp(yaw_to, 0.0f, m_friction);
    move_to_position_vector =
        DirectX::XMVectorLerp(move_to_position_vector, DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), m_friction);

    position_vector += move_to_position_vector * movement_speed;
    m_pitch += pitch_to;
    m_yaw += yaw_to;

    // Compute rotation matrix.
    static const DirectX::XMVECTOR world_up_vector = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    static const DirectX::XMVECTOR world_right_vector = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    static const DirectX::XMVECTOR world_front_vector = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    const DirectX::XMMATRIX rotation_matrix = DirectX::XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0);

    right_vector = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_right_vector, rotation_matrix));
    front_vector = DirectX::XMVector3Normalize(DirectX::XMVector3Transform(world_front_vector, rotation_matrix));

    DirectX::XMVECTOR up_vector = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(front_vector, right_vector));

    // Store results back to the member variables.
    DirectX::XMStoreFloat4(&m_right, right_vector);
    DirectX::XMStoreFloat4(&m_front, front_vector);

    DirectX::XMStoreFloat4(&m_position, position_vector);

    // The 'camera position' in the view matrix is a zero vector. In the shader, the vertex position subtracts the
    // position vector so that the camera is ALWAYS at the origin.
    // This should help a lot with precision.
    return DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),
                                     DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f) + front_vector, up_vector);
}
