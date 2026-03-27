#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Plane.h>
#include <Engine/Math/AABB.h>

namespace Zero
{
	struct ENGINE_API Fustrum
	{
		Plane planes[6];

		bool ContainsPoint(const Vec3& p) const;
		bool IntersectsAABB(const AABB& box) const;
	};
}
