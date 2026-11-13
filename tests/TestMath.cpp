#include <doctest.h>
#include <Engine/Math/Vec2.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Vec4.h>
#include <Engine/Math/IVec.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Quat.h>
#include <cmath>

using namespace Zero;

static bool ApproxEqual(float a, float b, float eps = 1e-4f)
{
	return std::abs(a - b) <= eps;
}

static bool Vec2ApproxEqual(const Vec2& a, const Vec2& b, float eps = 1e-4f)
{
	return ApproxEqual(a.x, b.x, eps) && ApproxEqual(a.y, b.y, eps);
}

static bool Vec3ApproxEqual(const Vec3& a, const Vec3& b, float eps = 1e-4f)
{
	return ApproxEqual(a.x, b.x, eps) &&
	       ApproxEqual(a.y, b.y, eps) &&
	       ApproxEqual(a.z, b.z, eps);
}

static bool Vec4ApproxEqual(const Vec4& a, const Vec4& b, float eps = 1e-4f)
{
	return ApproxEqual(a.x, b.x, eps) &&
	       ApproxEqual(a.y, b.y, eps) &&
	       ApproxEqual(a.z, b.z, eps) &&
	       ApproxEqual(a.w, b.w, eps);
}

static bool Mat4ApproxEqual(const Mat4& a, const Mat4& b, float eps = 1e-4f)
{
	for (int i = 0; i < 16; ++i)
	{
		if (std::abs(a.m[i] - b.m[i]) > eps)
			return false;
	}
	return true;
}

TEST_CASE("Vec2 Operations")
{
	Vec2 zero;
	CHECK(zero.x == 0.0f);
	CHECK(zero.y == 0.0f);

	Vec2 a(3.0f, 4.0f);
	CHECK(a.x == 3.0f);
	CHECK(a.y == 4.0f);

	Vec2 s(5.0f);
	CHECK(s.x == 5.0f);
	CHECK(s.y == 5.0f);

	Vec2 b(1.0f, 2.0f);
	Vec2 add = a + b;
	CHECK(add.x == 4.0f);
	CHECK(add.y == 6.0f);

	Vec2 sub = a - b;
	CHECK(sub.x == 2.0f);
	CHECK(sub.y == 2.0f);

	Vec2 mul = a * b;
	CHECK(mul.x == 3.0f);
	CHECK(mul.y == 8.0f);

	Vec2 smul = a * 2.0f;
	CHECK(smul.x == 6.0f);
	CHECK(smul.y == 8.0f);

	Vec2 sdiv = a / 2.0f;
	CHECK(sdiv.x == 1.5f);
	CHECK(sdiv.y == 2.0f);

	Vec2 neg = -a;
	CHECK(neg.x == -3.0f);
	CHECK(neg.y == -4.0f);

	CHECK(a.LengthSquared() == 25.0f);
	CHECK(a.Length() == 5.0f);

	Vec2 norm = a.Normalized();
	CHECK(ApproxEqual(norm.Length(), 1.0f));
	CHECK(ApproxEqual(norm.x, 0.6f));
	CHECK(ApproxEqual(norm.y, 0.8f));

	CHECK(Vec2::Dot(a, b) == 11.0f);
	CHECK(a.Dot(b) == 11.0f);
	CHECK(Dot(a, b) == 11.0f);
}

TEST_CASE("IVec Types")
{
	SUBCASE("IVec2")
	{
		IVec2 a(10, 20);
		IVec2 b(3, 4);
		IVec2 c = a + b;
		CHECK(c.x == 13);
		CHECK(c.y == 24);

		IVec2 d = a - b;
		CHECK(d.x == 7);
		CHECK(d.y == 16);

		IVec2 m = a * 2;
		CHECK(m.x == 20);
		CHECK(m.y == 40);

		IVec2 div = a / 2;
		CHECK(div.x == 5);
		CHECK(div.y == 10);
	}

	SUBCASE("IVec3")
	{
		IVec3 a(1, 2, 3);
		IVec3 b(4, 5, 6);
		IVec3 c = a + b;
		CHECK(c.x == 5);
		CHECK(c.y == 7);
		CHECK(c.z == 9);

		IVec3 d = a * 3;
		CHECK(d.x == 3);
		CHECK(d.y == 6);
		CHECK(d.z == 9);
	}

	SUBCASE("IVec4")
	{
		IVec4 a(1, 2, 3, 4);
		IVec4 b(10, 20, 30, 40);
		IVec4 c = a + b;
		CHECK(c.x == 11);
		CHECK(c.y == 22);
		CHECK(c.z == 33);
		CHECK(c.w == 44);
	}
}

TEST_CASE("Vec3 Operations")
{
	SUBCASE("Constructors")
	{
		Vec3 def;
		CHECK(def.x == 0.0f);
		CHECK(def.y == 0.0f);
		CHECK(def.z == 0.0f);

		Vec3 val(1.0f, 2.0f, 3.0f);
		CHECK(val.x == 1.0f);
		CHECK(val.y == 2.0f);
		CHECK(val.z == 3.0f);

		Vec3 scalar(7.0f);
		CHECK(scalar.x == 7.0f);
		CHECK(scalar.y == 7.0f);
		CHECK(scalar.z == 7.0f);
	}

	SUBCASE("Arithmetic Operators")
	{
		Vec3 a(1.0f, 2.0f, 3.0f);
		Vec3 b(4.0f, 5.0f, 6.0f);

		Vec3 add = a + b;
		CHECK(add.x == 5.0f);
		CHECK(add.y == 7.0f);
		CHECK(add.z == 9.0f);

		Vec3 sub = b - a;
		CHECK(sub.x == 3.0f);
		CHECK(sub.y == 3.0f);
		CHECK(sub.z == 3.0f);

		Vec3 mulComp = a * b;
		CHECK(mulComp.x == 4.0f);
		CHECK(mulComp.y == 10.0f);
		CHECK(mulComp.z == 18.0f);

		Vec3 mulScalar = a * 2.0f;
		CHECK(mulScalar.x == 2.0f);
		CHECK(mulScalar.y == 4.0f);
		CHECK(mulScalar.z == 6.0f);

		Vec3 scalarMul = 2.0f * a;
		CHECK(scalarMul.x == 2.0f);
		CHECK(scalarMul.y == 4.0f);
		CHECK(scalarMul.z == 6.0f);

		Vec3 divScalar = b / 2.0f;
		CHECK(divScalar.x == 2.0f);
		CHECK(divScalar.y == 2.5f);
		CHECK(divScalar.z == 3.0f);

		Vec3 neg = -a;
		CHECK(neg.x == -1.0f);
		CHECK(neg.y == -2.0f);
		CHECK(neg.z == -3.0f);

		Vec3 c = a;
		c += b;
		CHECK(c == add);

		c -= b;
		CHECK(c == a);

		c *= 3.0f;
		CHECK(c.x == 3.0f);
		CHECK(c.y == 6.0f);
		CHECK(c.z == 9.0f);

		c /= 3.0f;
		CHECK(c == a);

		CHECK(a == a);
		CHECK(a != b);
	}

	SUBCASE("Length and Normalization")
	{
		Vec3 v(2.0f, 3.0f, 6.0f);
		CHECK(v.LengthSquared() == 49.0f);
		CHECK(v.Length() == 7.0f);

		Vec3 norm = v.Normalized();
		CHECK(ApproxEqual(norm.Length(), 1.0f));
		CHECK(ApproxEqual(norm.x, 2.0f / 7.0f));
		CHECK(ApproxEqual(norm.y, 3.0f / 7.0f));
		CHECK(ApproxEqual(norm.z, 6.0f / 7.0f));

		v.Normalize();
		CHECK(ApproxEqual(v.Length(), 1.0f));
		CHECK(v == norm);
	}

	SUBCASE("Dot and Cross Products")
	{
		Vec3 x(1.0f, 0.0f, 0.0f);
		Vec3 y(0.0f, 1.0f, 0.0f);
		Vec3 z(0.0f, 0.0f, 1.0f);

		CHECK(Vec3::Dot(x, y) == 0.0f);
		CHECK(Vec3::Dot(x, x) == 1.0f);
		CHECK(x.Dot(y) == 0.0f);
		CHECK(Dot(x, y) == 0.0f);

		Vec3 crossXY = Vec3::Cross(x, y);
		CHECK(crossXY == z);

		Vec3 crossYZ = Vec3::Cross(y, z);
		CHECK(crossYZ == x);

		Vec3 crossZX = Vec3::Cross(z, x);
		CHECK(crossZX == y);

		Vec3 crossYX = x.Cross(y);
		CHECK(crossYX == z);
		CHECK(Cross(x, y) == z);
	}
}

TEST_CASE("Vec4 Operations")
{
	Vec4 def;
	CHECK(def.x == 0.0f);
	CHECK(def.y == 0.0f);
	CHECK(def.z == 0.0f);
	CHECK(def.w == 0.0f);

	Vec4 val(1.0f, 2.0f, 3.0f, 4.0f);
	Vec4 fromVec3(Vec3(1.0f, 2.0f, 3.0f), 4.0f);
	CHECK(val == fromVec3);

	Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
	Vec4 b(5.0f, 6.0f, 7.0f, 8.0f);
	Vec4 add = a + b;
	CHECK(add.x == 6.0f);
	CHECK(add.y == 8.0f);
	CHECK(add.z == 10.0f);
	CHECK(add.w == 12.0f);

	CHECK(Vec4::Dot(a, b) == 70.0f);
	CHECK(a.Dot(b) == 70.0f);

	Vec4 v(0.0f, 3.0f, 4.0f, 0.0f);
	CHECK(v.Length() == 5.0f);
	Vec4 norm = v.Normalized();
	CHECK(ApproxEqual(norm.Length(), 1.0f));
}

TEST_CASE("Mat4 Operations")
{
	SUBCASE("Identity")
	{
		Mat4 id = Mat4::Identity();
		CHECK(id.m[0] == 1.0f);
		CHECK(id.m[5] == 1.0f);
		CHECK(id.m[10] == 1.0f);
		CHECK(id.m[15] == 1.0f);
		CHECK(id.Determinant() == 1.0f);
	}

	SUBCASE("Translate")
	{
		Mat4 t = Mat4::Translate(Vec3(10.0f, -5.0f, 2.0f));
		Vec4 p(1.0f, 2.0f, 3.0f, 1.0f);
		Vec4 tp = t * p;
		CHECK(tp.x == 11.0f);
		CHECK(tp.y == -3.0f);
		CHECK(tp.z == 5.0f);
		CHECK(tp.w == 1.0f);

		Vec3 p3(1.0f, 2.0f, 3.0f);
		Vec3 tp3 = t * p3;
		CHECK(tp3.x == 11.0f);
		CHECK(tp3.y == -3.0f);
		CHECK(tp3.z == 5.0f);
	}

	SUBCASE("Scale")
	{
		Mat4 s = Mat4::Scale(Vec3(2.0f, 3.0f, 4.0f));
		Vec4 p(1.0f, 2.0f, 3.0f, 1.0f);
		Vec4 sp = s * p;
		CHECK(sp.x == 2.0f);
		CHECK(sp.y == 6.0f);
		CHECK(sp.z == 12.0f);
		CHECK(sp.w == 1.0f);
		CHECK(ApproxEqual(s.Determinant(), 24.0f));
	}

	SUBCASE("Rotate")
	{
		constexpr float halfPi = 1.57079632679f;
		Mat4 rotZ = Mat4::Rotate(halfPi, Vec3(0.0f, 0.0f, 1.0f));
		Vec4 p(1.0f, 0.0f, 0.0f, 1.0f);
		Vec4 rp = rotZ * p;
		CHECK(ApproxEqual(rp.x, 0.0f));
		CHECK(ApproxEqual(rp.y, 1.0f));
		CHECK(ApproxEqual(rp.z, 0.0f));
		CHECK(ApproxEqual(rp.w, 1.0f));
	}

	SUBCASE("Transpose")
	{
		Mat4 t = Mat4::Translate(Vec3(1.0f, 2.0f, 3.0f));
		Mat4 tt = t.Transpose();
		CHECK(tt.m[3] == 1.0f);
		CHECK(tt.m[7] == 2.0f);
		CHECK(tt.m[11] == 3.0f);
		CHECK(tt.m[12] == 0.0f);

		Mat4 ttt = tt.Transpose();
		CHECK(ttt == t);

		Mat4 freeT = Transpose(t);
		CHECK(freeT == tt);
	}

	SUBCASE("Inverse")
	{
		Mat4 id = Mat4::Identity();
		CHECK(id.Inverse() == id);

		Mat4 t = Mat4::Translate(Vec3(3.0f, 7.0f, -2.0f));
		Mat4 r = Mat4::Rotate(0.785398f, Vec3(0.0f, 1.0f, 0.0f));
		Mat4 s = Mat4::Scale(Vec3(2.0f, 0.5f, 3.0f));
		Mat4 m = t * r * s;

		Mat4 inv = m.Inverse();
		Mat4 shouldBeId1 = m * inv;
		Mat4 shouldBeId2 = inv * m;

		CHECK(Mat4ApproxEqual(shouldBeId1, Mat4::Identity(), 1e-4f));
		CHECK(Mat4ApproxEqual(shouldBeId2, Mat4::Identity(), 1e-4f));

		Mat4 freeInv = Inverse(m);
		CHECK(Mat4ApproxEqual(freeInv, inv, 1e-5f));

		Mat4 singular{};
		CHECK(singular.Inverse() == Mat4::Identity());
	}

	SUBCASE("LookAt")
	{
		Vec3 eye(0.0f, 0.0f, 5.0f);
		Vec3 target(0.0f, 0.0f, 0.0f);
		Vec3 up(0.0f, 1.0f, 0.0f);

		Mat4 v = Mat4::LookAt(eye, target, up);

		Vec4 eyeTransformed = v * Vec4(eye, 1.0f);
		CHECK(ApproxEqual(eyeTransformed.x, 0.0f));
		CHECK(ApproxEqual(eyeTransformed.y, 0.0f));
		CHECK(ApproxEqual(eyeTransformed.z, 0.0f));

		Vec4 targetTransformed = v * Vec4(target, 1.0f);
		CHECK(ApproxEqual(targetTransformed.x, 0.0f));
		CHECK(ApproxEqual(targetTransformed.y, 0.0f));
		CHECK(ApproxEqual(targetTransformed.z, -5.0f));

		Vec4 rightTransformed = v * Vec4(1.0f, 0.0f, 5.0f, 1.0f);
		CHECK(ApproxEqual(rightTransformed.x, 1.0f));
		CHECK(ApproxEqual(rightTransformed.y, 0.0f));
		CHECK(ApproxEqual(rightTransformed.z, 0.0f));
	}

	SUBCASE("Perspective (Vulkan NDC [0, 1], Y inverted)")
	{
		constexpr float fov = 1.04719755f; // 60 degrees
		constexpr float aspect = 16.0f / 9.0f;
		constexpr float zNear = 0.1f;
		constexpr float zFar = 1000.0f;

		Mat4 p = Mat4::Perspective(fov, aspect, zNear, zFar);

		float tanHalfFov = std::tan(fov * 0.5f);
		CHECK(ApproxEqual(p.m[5], -1.0f / tanHalfFov));

		Vec4 nearPoint(0.0f, 0.0f, -zNear, 1.0f);
		Vec4 nearClip = p * nearPoint;
		float nearNdcZ = nearClip.z / nearClip.w;
		CHECK(ApproxEqual(nearNdcZ, 0.0f, 1e-4f));

		Vec4 farPoint(0.0f, 0.0f, -zFar, 1.0f);
		Vec4 farClip = p * farPoint;
		float farNdcZ = farClip.z / farClip.w;
		CHECK(ApproxEqual(farNdcZ, 1.0f, 1e-4f));
	}

	SUBCASE("Orthographic (Vulkan NDC [0, 1] depth)")
	{
		constexpr float left = -10.0f;
		constexpr float right = 10.0f;
		constexpr float bottom = -5.0f;
		constexpr float top = 5.0f;
		constexpr float zNear = 1.0f;
		constexpr float zFar = 100.0f;

		Mat4 ortho = Mat4::Orthographic(left, right, bottom, top, zNear, zFar);

		Vec4 nearCenter(0.0f, 0.0f, -zNear, 1.0f);
		Vec4 nearClip = ortho * nearCenter;
		CHECK(ApproxEqual(nearClip.z, 0.0f, 1e-4f));

		Vec4 farCenter(0.0f, 0.0f, -zFar, 1.0f);
		Vec4 farClip = ortho * farCenter;
		CHECK(ApproxEqual(farClip.z, 1.0f, 1e-4f));

		Vec4 rightPoint(right, 0.0f, -zNear, 1.0f);
		Vec4 rightClip = ortho * rightPoint;
		CHECK(ApproxEqual(rightClip.x, 1.0f, 1e-4f));

		Vec4 leftPoint(left, 0.0f, -zNear, 1.0f);
		Vec4 leftClip = ortho * leftPoint;
		CHECK(ApproxEqual(leftClip.x, -1.0f, 1e-4f));

		// In Vulkan top is -1, bottom is +1
		Vec4 topPoint(0.0f, top, -zNear, 1.0f);
		Vec4 topClip = ortho * topPoint;
		CHECK(ApproxEqual(topClip.y, -1.0f, 1e-4f));

		Vec4 bottomPoint(0.0f, bottom, -zNear, 1.0f);
		Vec4 bottomClip = ortho * bottomPoint;
		CHECK(ApproxEqual(bottomClip.y, 1.0f, 1e-4f));
	}

	SUBCASE("Batch Point and Vector Transformations")
	{
		Mat4 t = Mat4::Translate(Vec3(10.0f, 20.0f, 30.0f)) * Mat4::Scale(Vec3(2.0f, 3.0f, 4.0f));

		constexpr size_t count = 10;
		Vec3 inPoints[count];
		Vec3 outPoints[count];
		Vec3 outVectors[count];

		for (size_t i = 0; i < count; ++i)
		{
			inPoints[i] = Vec3(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
		}

		Mat4::TransformPointsBatch(t, inPoints, outPoints, count);
		Mat4::TransformVectorsBatch(t, inPoints, outVectors, count);

		for (size_t i = 0; i < count; ++i)
		{
			Vec3 expectedPoint = t * inPoints[i];
			CHECK(ApproxEqual(outPoints[i].x, expectedPoint.x, 1e-4f));
			CHECK(ApproxEqual(outPoints[i].y, expectedPoint.y, 1e-4f));
			CHECK(ApproxEqual(outPoints[i].z, expectedPoint.z, 1e-4f));

			// Vector transform ignores translation
			Vec3 expectedVector(inPoints[i].x * 2.0f, inPoints[i].y * 3.0f, inPoints[i].z * 4.0f);
			CHECK(ApproxEqual(outVectors[i].x, expectedVector.x, 1e-4f));
			CHECK(ApproxEqual(outVectors[i].y, expectedVector.y, 1e-4f));
			CHECK(ApproxEqual(outVectors[i].z, expectedVector.z, 1e-4f));
		}
	}
}

TEST_CASE("Quat Operations")
{
	SUBCASE("Identity")
	{
		Quat q = Quat::Identity();
		CHECK(q.x == 0.0f);
		CHECK(q.y == 0.0f);
		CHECK(q.z == 0.0f);
		CHECK(q.w == 1.0f);
		CHECK(q.ToMat4() == Mat4::Identity());
	}

	SUBCASE("FromAxisAngle and Vector Rotation")
	{
		constexpr float halfPi = 1.57079632679f;
		Quat qz = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), halfPi);

		Vec3 v(1.0f, 0.0f, 0.0f);
		Vec3 rotated = qz * v;
		CHECK(ApproxEqual(rotated.x, 0.0f));
		CHECK(ApproxEqual(rotated.y, 1.0f));
		CHECK(ApproxEqual(rotated.z, 0.0f));

		Quat qx = Quat::FromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), halfPi);
		Vec3 rotatedX = qx * Vec3(0.0f, 1.0f, 0.0f);
		CHECK(ApproxEqual(rotatedX.x, 0.0f));
		CHECK(ApproxEqual(rotatedX.y, 0.0f));
		CHECK(ApproxEqual(rotatedX.z, 1.0f));
	}

	SUBCASE("ToMat4 equivalence with Mat4::Rotate")
	{
		constexpr float angle = 0.85f;
		Vec3 axis(1.0f, 2.0f, 3.0f);
		Quat q = Quat::FromAxisAngle(axis, angle);
		Mat4 matFromQuat = q.ToMat4();
		Mat4 matRotate = Mat4::Rotate(angle, axis);

		CHECK(Mat4ApproxEqual(matFromQuat, matRotate, 1e-4f));
	}

	SUBCASE("Quaternion Multiplication")
	{
		constexpr float halfPi = 1.57079632679f;
		Quat q1 = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), halfPi);
		Quat q2 = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), halfPi);
		Quat combined = q2 * q1;

		Vec3 v(1.0f, 0.0f, 0.0f);
		Vec3 rotated = combined * v;
		CHECK(ApproxEqual(rotated.x, -1.0f));
		CHECK(ApproxEqual(rotated.y, 0.0f));
		CHECK(ApproxEqual(rotated.z, 0.0f));
	}

	SUBCASE("Normalization")
	{
		Quat q(1.0f, 2.0f, 3.0f, 4.0f);
		Quat norm = q.Normalized();
		CHECK(ApproxEqual(norm.Length(), 1.0f));

		q.Normalize();
		CHECK(ApproxEqual(q.Length(), 1.0f));
		CHECK(q == norm);
	}
}

