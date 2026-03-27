#pragma once
#include <cstdint>
#include <Engine/Core.h>

namespace Zero::RHI
{
	template<typename Tag>
	struct Handle
	{
		uint32_t idx : 20 = 0;  // 0 == invalid (pools start at index 1)
		uint32_t gen : 12 = 0;

		[[nodiscard]] constexpr bool IsValid() const { return idx != 0; }
		constexpr bool operator==(const Handle&) const = default;
		constexpr bool operator!=(const Handle&) const = default;
	};
	static_assert(sizeof(Handle<void>) == 4);

	struct BufferTag {};
	struct TextureTag {};
	struct PipelineTag {};
	struct ShaderTag {};

	using BufferHandle = Handle<BufferTag>;
	using TextureHandle = Handle<TextureTag>;
	using PipelineHandle = Handle<PipelineTag>;
	using ShaderHandle = Handle<ShaderTag>;

	enum class API { Vulkan, D3D12, OpenGL, Metal, COUNT };

	union DrawSortKey 
	{
		uint64_t value = 0;
		struct {
			uint64_t depth : 24; // quantised linear depth
			uint64_t materialId : 16; // minimise texture/uniform rebinds
			uint64_t pipelineId : 12; // minimise PSO switches (most expensive)
			uint64_t pass : 4; // 0=depth_prepass 1=opaque 2=transparent 3=ui
			uint64_t _pad : 8;
		};

		[[nodiscard]] static constexpr uint32_t QuantizeDepth(float ndcDepth)
		{
			// ndcDepth in [0,1]. Multiply to 24-bit range.
			// For transparent objects: pass ~quantized to reverse sort.
			uint32_t q = static_cast<uint32_t>(ndcDepth * 0x00FFFFFFu);
			return q & 0x00FFFFFFu;
		}
	};
	static_assert(sizeof(DrawSortKey) == 8);

	// DrawPacket
	//   Flat POD struct produced by the visibility system.
	//   Sorted by sortKey before submission. No pointers — all handles.
	//   Fits in two cache lines (128 bytes max, currently well under).
	struct DrawPacket
	{
		DrawSortKey     sortKey;

		BufferHandle    vertexBuffer;
		BufferHandle    indexBuffer;
		PipelineHandle  pipeline;

		uint32_t        indexCount = 0;
		uint32_t        firstIndex = 0;
		uint32_t        vertexOffset = 0;    // base vertex for indexed draw
		uint32_t        instanceCount = 1;

		// Offset into the per-frame per-instance SSBO.
		// Shader reads transform, material params, etc. from this slot.
		uint32_t        instanceDataOffset = 0;
	};
	static_assert(sizeof(DrawPacket) <= 64);
}
