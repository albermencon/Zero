#pragma once
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>

namespace Zero 
{
    class Buffer
    {
    public:
        virtual void* Map() = 0;
        virtual void  Unmap() = 0;

        virtual size_t GetSize() const = 0;
        virtual BufferUsage GetUsage() const = 0;
        virtual MemoryDomain GetMemoryDomain() const = 0;

        virtual bool IsMappable() const = 0;

        virtual ~Buffer() = default;
    };
}
