#include <doctest.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Frustum.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Graphics/FrameData.h>
#include <vector>
#include <cmath>

using namespace Zero;

TEST_CASE("Mat4 Operations")
{
	SUBCASE("Identity")
	{
		Mat4 id = Mat4::Identity();
		CHECK(id.m[0] == 1.0f);
		CHECK(id.m[5] == 1.0f);
		CHECK(id.m[10] == 1.0f);
		CHECK(id.m[15] == 1.0f);
		CHECK(id.m[1] == 0.0f);
		CHECK(id.m[4] == 0.0f);
	}

	SUBCASE("Multiplication by Identity")
	{
		Mat4 id = Mat4::Identity();
		Mat4 a{};
		for (int i = 0; i < 16; ++i)
			a.m[i] = static_cast<float>(i + 1);

		Mat4 r1 = a * id;
		Mat4 r2 = id * a;
		CHECK(r1 == a);
		CHECK(r2 == a);
	}

	SUBCASE("Addition")
	{
		Mat4 a = Mat4::Identity();
		Mat4 b = Mat4::Identity();
		Mat4 c = a + b;
		CHECK(c.m[0] == 2.0f);
		CHECK(c.m[5] == 2.0f);
		CHECK(c.m[10] == 2.0f);
		CHECK(c.m[15] == 2.0f);
	}

	SUBCASE("Batch Multiplication Equivalence")
	{
		Mat4 transform = Mat4::Identity();
		transform.m[0] = 2.0f;
		transform.m[5] = 3.0f;
		transform.m[10] = 4.0f;
		transform.m[12] = 10.0f;

		constexpr size_t count = 37; // Non power of 2
		std::vector<Mat4> in(count);
		std::vector<Mat4> outBatch(count);

		for (size_t i = 0; i < count; ++i)
		{
			in[i] = Mat4::Identity();
			in[i].m[12] = static_cast<float>(i);
			in[i].m[13] = static_cast<float>(i * 2);
			in[i].m[14] = static_cast<float>(i * 3);
		}

		Mat4::MultiplyBatch(transform, in.data(), outBatch.data(), count);

		for (size_t i = 0; i < count; ++i)
		{
			Mat4 expected = transform * in[i];
			CHECK(outBatch[i] == expected);
		}
	}
}

TEST_CASE("Frustum Plane Extraction and AABB Culling")
{
	// Symmetric perspective frustum:
	// fov = 90 deg (tanHalfFov = 1.0), aspect = 1.0, near = 1.0, far = 100.0
	// Vulkan clip space (Z in [0, 1], Y down)
	Mat4 proj{};
	const float aspect = 1.0f;
	const float tanHalfFov = 1.0f;
	const float zNear = 1.0f;
	const float zFar = 100.0f;

	proj.m[0] = 1.0f / (aspect * tanHalfFov); // 1.0
	proj.m[5] = -1.0f / tanHalfFov;           // -1.0
	proj.m[10] = -zFar / (zFar - zNear);      // -100 / 99
	proj.m[11] = -1.0f;
	proj.m[14] = -(zFar * zNear) / (zFar - zNear); // -100 / 99

	// Camera at (0, 0, 0) looking down -Z, up is +Y, right is +X
	Mat4 view = Mat4::Identity();

	Mat4 vp = proj * view;
	Frustum frustum = Frustum::FromViewProj(vp);

	SUBCASE("Box directly in front of camera (visible)")
	{
		// Centered at (0, 0, -10), size 2x2x2
		CHECK(frustum.TestAABB(-1.0f, -1.0f, -11.0f, 1.0f, 1.0f, -9.0f));
	}

	SUBCASE("Box behind camera (culled)")
	{
		// Behind at (0, 0, 10)
		CHECK_FALSE(frustum.TestAABB(-1.0f, -1.0f, 9.0f, 1.0f, 1.0f, 11.0f));
	}

	SUBCASE("Box beyond far plane (culled)")
	{
		// Beyond far plane (Z < -100)
		CHECK_FALSE(frustum.TestAABB(-1.0f, -1.0f, -120.0f, 1.0f, 1.0f, -110.0f));
	}

	SUBCASE("Box before near plane (culled)")
	{
		// Closer than near plane (Z = -0.5)
		CHECK_FALSE(frustum.TestAABB(-0.1f, -0.1f, -0.6f, 0.1f, 0.1f, -0.4f));
	}

	SUBCASE("Box far to the right (culled)")
	{
		// At Z = -10, frustum half-width is 10. Box at X = 50 is outside.
		CHECK_FALSE(frustum.TestAABB(45.0f, -1.0f, -11.0f, 55.0f, 1.0f, -9.0f));
	}

	SUBCASE("Box far to the left (culled)")
	{
		CHECK_FALSE(frustum.TestAABB(-55.0f, -1.0f, -11.0f, -45.0f, 1.0f, -9.0f));
	}

	SUBCASE("Box straddling left boundary (visible)")
	{
		// At Z = -10, left plane is at X = -10. Box from X = -12 to X = -8 straddles.
		CHECK(frustum.TestAABB(-12.0f, -1.0f, -11.0f, -8.0f, 1.0f, -9.0f));
	}
}

TEST_CASE("Frustum SIMD Batch Culling Equivalence")
{
	Mat4 proj{};
	const float aspect = 1.333f;
	const float tanHalfFov = 0.577f; // ~60 deg
	const float zNear = 0.5f;
	const float zFar = 500.0f;

	proj.m[0] = 1.0f / (aspect * tanHalfFov);
	proj.m[5] = -1.0f / tanHalfFov;
	proj.m[10] = -zFar / (zFar - zNear);
	proj.m[11] = -1.0f;
	proj.m[14] = -(zFar * zNear) / (zFar - zNear);

	Mat4 view = Mat4::Identity();
	Mat4 vp = proj * view;
	Frustum frustum = Frustum::FromViewProj(vp);

	// Generate 1024 test AABBs across a wide grid
	const uint32_t count = 1024;
	std::vector<float> minX(count), minY(count), minZ(count);
	std::vector<float> maxX(count), maxY(count), maxZ(count);

	for (uint32_t i = 0; i < count; ++i)
	{
		float x = static_cast<float>((static_cast<int>(i % 32) - 16) * 20);
		float y = static_cast<float>((static_cast<int>((i / 32) % 32) - 16) * 20);
		float z = -static_cast<float>((i % 64) * 10);

		minX[i] = x - 5.0f;
		maxX[i] = x + 5.0f;
		minY[i] = y - 5.0f;
		maxY[i] = y + 5.0f;
		minZ[i] = z - 5.0f;
		maxZ[i] = z + 5.0f;
	}

	std::vector<uint32_t> batchVisible(count);
	uint32_t batchVisibleCount = frustum.CullAABBBatch(
		minX.data(), minY.data(), minZ.data(),
		maxX.data(), maxY.data(), maxZ.data(),
		count,
		batchVisible.data()
	);

	// Test each item individually with scalar TestAABB
	std::vector<uint32_t> scalarVisible;
	for (uint32_t i = 0; i < count; ++i)
	{
		if (frustum.TestAABB(minX[i], minY[i], minZ[i], maxX[i], maxY[i], maxZ[i]))
		{
			scalarVisible.push_back(i);
		}
	}

	REQUIRE(batchVisibleCount == scalarVisible.size());
	for (uint32_t v = 0; v < batchVisibleCount; ++v)
	{
		CHECK(batchVisible[v] == scalarVisible[v]);
	}
}

TEST_CASE("GPUDriven Batch Emission and DrawPacket Generation")
{
	SceneManager::Get().Init();

	SceneId gpuSceneId = SceneManager::Get().CreateScene("TerrainChunks", RenderPath::GPUDriven);
	SceneManager::Get().SetSceneActive(gpuSceneId, true);
	Scene* scene = SceneManager::Get().GetScene(gpuSceneId);
	REQUIRE(scene != nullptr);

	GPUDrivenBatch batch{};
	batch.objectDataBuffer = BufferHandle{ 10 };
	batch.indirectBuffer = BufferHandle{ 11 };
	batch.countBuffer = BufferHandle{ 12 };
	batch.vertexBuffer = BufferHandle{ 13 };
	batch.indexBuffer = BufferHandle{ 14 };
	batch.renderPipeline = PipelineHandle{ 5 };
	batch.cullPipeline = PipelineHandle{ 6 };
	batch.maxObjects = 5000;
	batch.enabled = true;

	scene->SetGPUBatch(batch);
	SceneManager::Get().FlushCommands();

	FrameData* frame = SceneManager::Get().BuildFrame(1, 0.016f);
	REQUIRE(frame != nullptr);

	CHECK(frame->gpuDriven.enabled == true);
	CHECK(frame->gpuDriven.maxObjects == 5000);
	CHECK(frame->gpuDriven.indirectBuffer.GetIndex() == 11);
	CHECK(frame->gpuDriven.countBuffer.GetIndex() == 12);

	bool foundMdiCount = false;
	for (const auto& packet : frame->drawPackets)
	{
		if (static_cast<RHI::DrawCmdType>(packet.cmdType) == RHI::DrawCmdType::MultiDrawIndirectCount)
		{
			foundMdiCount = true;
			CHECK(packet.mdiCount.indirectBufferId == 11);
			CHECK(packet.mdiCount.countBufferId == 12);
			CHECK(packet.mdiCount.maxDrawCount == 5000);

			REQUIRE(packet.geometryId < frame->geometryTable.size());
			CHECK(frame->geometryTable[packet.geometryId].vertexBuffer == BufferHandle{ 13 });
			CHECK(frame->geometryTable[packet.geometryId].indexBuffer == BufferHandle{ 14 });

			uint16_t pipelineId = RHI::GetPipelineIdFromSortKey(packet.sortKey);
			CHECK(pipelineId == 5);
		}
	}
	CHECK(foundMdiCount);

	SceneManager::Get().DestroyScene(gpuSceneId);
}
