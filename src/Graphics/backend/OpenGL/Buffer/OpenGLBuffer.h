#pragma once
#include "Graphics/core/Buffer.h"
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>

namespace Zero 
{
    typedef unsigned int GLuint;

    class OpenGLBuffer : public Buffer
    {
    public:
        OpenGLBuffer(GLuint id, size_t size, BufferUsage usage, MemoryDomain domain);
        virtual ~OpenGLBuffer() override;

        virtual void* Map() override;
        virtual void Unmap() override;

        virtual size_t GetSize() const override { return m_size; }
        virtual BufferUsage GetUsage() const override { return m_usage; }
        virtual MemoryDomain GetMemoryDomain() const override { return m_domain; }

        virtual bool IsMappable() const override;

        GLuint GetHandle() const { return m_id; }

    private:
        GLuint m_id = 0;
        size_t m_size = 0;
        BufferUsage m_usage;
        MemoryDomain m_domain;
    };
}
