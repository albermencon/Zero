#include "pch.h"
#include <Engine/Math/Frustum.h>
#include <cmath>

#define HWY_DISABLED_TARGETS (HWY_AVX3 | (HWY_AVX3 - 1))
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "Math/FrustumHighway-inl.h"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include "Math/FrustumHighway-inl.h"

namespace Zero
{
	void FrustumPlane::Normalize() noexcept
	{
		float lenSq = a * a + b * b + c * c;
		if (lenSq > 1e-8f)
		{
			float invLen = 1.0f / std::sqrt(lenSq);
			a *= invLen;
			b *= invLen;
			c *= invLen;
			d *= invLen;
		}
	}

	Frustum Frustum::FromViewProj(const Mat4& vp) noexcept
	{
		Frustum f;
		const float m00 = vp.m[0],  m01 = vp.m[4],  m02 = vp.m[8],  m03 = vp.m[12];
		const float m10 = vp.m[1],  m11 = vp.m[5],  m12 = vp.m[9],  m13 = vp.m[13];
		const float m20 = vp.m[2],  m21 = vp.m[6],  m22 = vp.m[10], m23 = vp.m[14];
		const float m30 = vp.m[3],  m31 = vp.m[7],  m32 = vp.m[11], m33 = vp.m[15];

		// Left plane: r3 + r0
		f.planes[0] = { m30 + m00, m31 + m01, m32 + m02, m33 + m03 };
		// Right plane: r3 - r0
		f.planes[1] = { m30 - m00, m31 - m01, m32 - m02, m33 - m03 };
		// Bottom plane: r3 + r1
		f.planes[2] = { m30 + m10, m31 + m11, m32 + m12, m33 + m13 };
		// Top plane: r3 - r1
		f.planes[3] = { m30 - m10, m31 - m11, m32 - m12, m33 - m13 };
		// Near plane: r2 (Vulkan depth [0, 1])
		f.planes[4] = { m20,       m21,       m22,       m23 };
		// Far plane: r3 - r2
		f.planes[5] = { m30 - m20, m31 - m21, m32 - m22, m33 - m23 };

		for (int i = 0; i < 6; ++i)
		{
			f.planes[i].Normalize();
		}
		return f;
	}

	bool Frustum::TestAABB(float minX, float minY, float minZ,
	                       float maxX, float maxY, float maxZ,
	                       float margin) const noexcept
	{
		for (int i = 0; i < 6; ++i)
		{
			const float px = (planes[i].a > 0.0f) ? maxX : minX;
			const float py = (planes[i].b > 0.0f) ? maxY : minY;
			const float pz = (planes[i].c > 0.0f) ? maxZ : minZ;
			if (planes[i].DistanceTo(px, py, pz) < margin)
			{
				return false;
			}
		}
		return true;
	}

#if HWY_ONCE
	HWY_EXPORT(CullAABBBatchHighway);

	uint32_t Frustum::CullAABBBatch(
		const float* minX, const float* minY, const float* minZ,
		const float* maxX, const float* maxY, const float* maxZ,
		uint32_t count,
		uint32_t* outVisibleIndices,
		float margin) const noexcept
	{
		return HWY_DYNAMIC_DISPATCH(CullAABBBatchHighway)(
			*this, minX, minY, minZ, maxX, maxY, maxZ, count, outVisibleIndices, margin);
	}
#endif
}
