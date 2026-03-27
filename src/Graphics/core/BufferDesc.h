#pragma once
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>

namespace Zero 
{
	struct BufferDesc
	{
		size_t size = 0;
		BufferUsage usage = BufferUsage::None;
		MemoryDomain memory = MemoryDomain::GPU;

		const void* initialData = nullptr;
	};
}
