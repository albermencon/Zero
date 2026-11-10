#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <cstdint>

namespace Zero
{
	struct ZERO_API alignas(16) FrustumPlane
	{
		float a = 0.0f;
		float b = 0.0f;
		float c = 0.0f;
		float d = 0.0f;

		void Normalize() noexcept;

		[[nodiscard]] float DistanceTo(float x, float y, float z) const noexcept
		{
			return a * x + b * y + c * z + d;
		}
	};

	struct ZERO_API alignas(16) Frustum
	{
		FrustumPlane planes[6];

		static Frustum FromViewProj(const Mat4& vp) noexcept;

		[[nodiscard]] bool TestAABB(float minX, float minY, float minZ,
		                            float maxX, float maxY, float maxZ,
		                            float margin = 0.0f) const noexcept;

		[[nodiscard]] bool TestAABB(const Vec3& min, const Vec3& max, float margin = 0.0f) const noexcept
		{
			return TestAABB(min.x, min.y, min.z, max.x, max.y, max.z, margin);
		}

		uint32_t CullAABBBatch(
			const float* minX, const float* minY, const float* minZ,
			const float* maxX, const float* maxY, const float* maxZ,
			uint32_t count,
			uint32_t* outVisibleIndices,
			float margin = 0.0f) const noexcept;
	};
}

