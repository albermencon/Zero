#pragma once
#include <cstdint>
#include <cstddef>
#include <Engine/Core.h>
#include <Engine/Graphics/ResourceHandle.h>
#include <Engine/Window.h>

namespace Zero::RHI
{
	using API = Zero::BackendType;
	using DrawSortKey = uint64_t;

	enum class RenderPass : uint64_t 
	{
        DepthPrepass = 0,
        Opaque       = 1,
        Transparent  = 2,
        UI           = 3
    };

	enum class DrawCmdType : uint8_t 
	{
		Arrays = 0,
		Indexed = 1,
		Instanced = 2,
		MultiDrawIndirect = 3,
		MultiDrawIndirectCount = 4
	};

	[[nodiscard]] static constexpr uint32_t QuantizeDepth(float ndcDepth)
    {
        return static_cast<uint32_t>(ndcDepth * 0x00FFFFFFu) & 0x00FFFFFFu;
    }

	[[nodiscard]] static constexpr DrawSortKey CreateSortKey(
    RenderPass pass, uint64_t subLayer, uint64_t pipelineId, uint64_t materialId, float depth)
	{
		uint64_t key = static_cast<uint64_t>(pass) << 60;
		key |= (subLayer & 0x0Full) << 56;

		if (pass == RenderPass::Transparent || pass == RenderPass::UI)
		{
			uint64_t invertedDepth = (~QuantizeDepth(depth)) & 0x00FFFFFFu;
			key |= (invertedDepth << 32);
			key |= (pipelineId << 16);
			key |= materialId;
		}
		else
		{
			uint64_t qDepth = QuantizeDepth(depth);
			key |= (pipelineId << 40);
			key |= (materialId << 24);
			key |= qDepth;
		}

		return key;
	}

	[[nodiscard]] static constexpr uint16_t GetPipelineIdFromSortKey(DrawSortKey key) noexcept
	{
		uint64_t pass = (key >> 60) & 0xFull;
		if (pass == static_cast<uint64_t>(RenderPass::Transparent) || pass == static_cast<uint64_t>(RenderPass::UI))
		{
			return static_cast<uint16_t>((key >> 16) & 0xFFFFu);
		}
		return static_cast<uint16_t>((key >> 40) & 0xFFFFu);
	}

	struct GeometryBinding
	{
		BufferHandle vertexBuffer;
		BufferHandle indexBuffer;
	};

	struct DrawPacket
	{
		DrawSortKey     sortKey;
		union {
			uint32_t packedHeader;
			struct {
				uint32_t transformIndex : 20;
				uint32_t geometryId     : 8;
				uint32_t cmdType        : 4; 
			};
		};

		union {
			struct {
				uint32_t vertexCount;
				uint32_t firstVertex;
			} arrays;

			struct {
				uint32_t indexCount;
				uint32_t firstIndex;
				int32_t vertexOffset;
			} indexed;

			struct {
				uint32_t indexCount;
				uint32_t instanceCount;
				uint32_t firstIndex;
				int32_t vertexOffset;
				uint32_t firstInstance;
			} instanced;

			struct {
				uint32_t indirectBufferId;
				uint32_t offset;
				uint32_t drawCount;
			} mdi;

			struct {
				uint32_t indirectBufferId;
				uint32_t countBufferId;
				uint32_t indirectOffset;
				uint32_t countOffset;
				uint32_t maxDrawCount;
			} mdiCount;
		};
	};

	static_assert(sizeof(DrawPacket) == 32, "DrawPacket must fit exactly in 32 bytes");

	struct DrawIndexedIndirectCommand
	{
		uint32_t indexCount;
		uint32_t instanceCount;
		uint32_t firstIndex;
		int32_t  vertexOffset;
		uint32_t firstInstance;
	};
	static_assert(sizeof(DrawIndexedIndirectCommand) == 20);

	enum class ComputeBarrier : uint8_t
	{
		None = 0,
		ComputeToCompute = 1,
		ComputeToDrawIndirect = 2,
		ComputeToGraphics = 3
	};

	struct alignas(16) ComputeDispatch
	{
		PipelineHandle pipeline;
		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
		uint32_t pushConstantSize = 0;
		ComputeBarrier barrier = ComputeBarrier::None;
		uint8_t padding[11]{};
		alignas(16) uint8_t pushConstants[128]{};
	};
	static_assert(sizeof(ComputeDispatch) == 160);
	static_assert(alignof(ComputeDispatch) == 16);
	static_assert(offsetof(ComputeDispatch, pushConstants) == 32);

	struct CullPushConstants
	{
		float frustumPlanes[24];
		uint32_t maxObjects;
		uint32_t _pad[3];
	};
	static_assert(sizeof(CullPushConstants) == 112);
}
