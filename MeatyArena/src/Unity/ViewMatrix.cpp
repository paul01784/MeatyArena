#include "ViewMatrix.h"

#include <cmath>

void Unity::ViewMatrix::Update(const Matrix4x4& matrix)
{
    right_ = {matrix.m[0][0], matrix.m[1][0], matrix.m[2][0]};
    up_ = {matrix.m[0][1], matrix.m[1][1], matrix.m[2][1]};
    forward_ = {matrix.m[0][2], matrix.m[1][2], matrix.m[2][2]};
    translation_ = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

Unity::Vector3 Unity::ViewMatrix::GetWorldPosition() const
{
    return (right_ * translation_.x + up_ * translation_.y + forward_ * translation_.z) * -1.0f;
}

bool Unity::ViewMatrix::WorldToScreen(const Vector3& world, Vector2& screen, float width, float height, float verticalFov, float aspect) const
{
    const float vx = right_.x * world.x + right_.y * world.y + right_.z * world.z + translation_.x;
    const float vy = up_.x * world.x + up_.y * world.y + up_.z * world.z + translation_.y;
    const float vz = forward_.x * world.x + forward_.y * world.y + forward_.z * world.z + translation_.z;
    if (vz >= -0.01f || width <= 0.0f || height <= 0.0f || verticalFov <= 1.0f || verticalFov >= 179.0f || aspect <= 0.0f)
        return false;

    const float tanHalfVertical = std::tan(verticalFov * 0.00872664625997f);
    const float normalizedX = vx / (-vz * tanHalfVertical * aspect);
    const float normalizedY = vy / (-vz * tanHalfVertical);
    screen = {(normalizedX + 1.0f) * width * 0.5f, (1.0f - normalizedY) * height * 0.5f};
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}
