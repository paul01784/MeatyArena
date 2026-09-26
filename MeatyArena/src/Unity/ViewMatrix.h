#pragma once

#include "Types.h"

namespace Unity
{
    class ViewMatrix
    {
    public:
        void Update(const Matrix4x4& matrix);
        Vector3 GetWorldPosition() const;
        bool WorldToScreen(const Vector3& world, Vector2& screen, float width, float height, float verticalFov, float aspect) const;
        const Vector3& Translation() const
        {
            return translation_;
        }

    private:
        Vector3 right_{}, up_{}, forward_{}, translation_{};
    };
}
