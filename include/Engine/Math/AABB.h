#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>

namespace Zero
{
	struct ENGINE_API AABB
	{
		Vec3 min;
		Vec3 max;

		bool Contains(const Vec3& p) const;
		bool Intersects(const AABB& other) const;
		Vec3 GetCenter() const;
		Vec3 GetExtents() const;
		float SurfaceArea() const;
	};

	struct AABBExtends
	{
		Vec3 center;
		Vec3 halfExtents;
	};
}
