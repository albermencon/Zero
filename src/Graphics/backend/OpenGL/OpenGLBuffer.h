#pragma once
#include "Graphics/core/Buffer.h"
#include <glad/glad.h>
#include <Engine/Graphics/BufferUsage.h>
#include <Engine/Graphics/MemoryDomain.h>

namespace Zero 
{
    class OpenGLBuffer : public Buffer
    {
    public:
        OpenGLBuffer(GLuint id, size_t size, BufferUsage usage, MemoryDomain domain)
            : m_id(id)
            , m_size(size)
            , m_usage(usage)
            , m_domain(domain)
        {
        }

        virtual ~OpenGLBuffer() override
        {
            if (m_id != 0)
            {
                glDeleteBuffers(1, &m_id);
            }
        }

        virtual void* Map() override
        {
            if (!IsMappable())
            {
                return nullptr;
            }
            
            GLbitfield access = GL_MAP_WRITE_BIT;
            if (m_domain == MemoryDomain::GPUtoCPU)
            {
                access = GL_MAP_READ_BIT;
            }
            else if (m_domain == MemoryDomain::CPUtoGPU_Coherent)
            {
                access |= GL_MAP_COHERENT_BIT | GL_MAP_PERSISTENT_BIT;
            }

            glBindBuffer(GL_COPY_WRITE_BUFFER, m_id);
            void* ptr = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, m_size, access);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            return ptr;
        }

        virtual void Unmap() override
        {
            if (!IsMappable())
            {
                return;
            }
            glBindBuffer(GL_COPY_WRITE_BUFFER, m_id);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        }

        virtual size_t GetSize() const override { return m_size; }
        virtual BufferUsage GetUsage() const override { return m_usage; }
        virtual MemoryDomain GetMemoryDomain() const override { return m_domain; }

        virtual bool IsMappable() const override
        {
            return m_domain != MemoryDomain::GPU;
        }

        GLuint GetHandle() const { return m_id; }

    private:
        GLuint m_id = 0;
        size_t m_size = 0;
        BufferUsage m_usage;
        MemoryDomain m_domain;
    };
}
