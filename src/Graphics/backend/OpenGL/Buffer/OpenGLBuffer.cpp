#include "pch.h"
#include "OpenGLBuffer.h"
#include <glad/glad.h>
#include <Engine/Core.h>
#include <Engine/Log.h>

namespace Zero
{
    OpenGLBuffer::OpenGLBuffer(GLuint id, size_t size, BufferUsage usage, MemoryDomain domain)
        : m_id(id)
        , m_size(size)
        , m_usage(usage)
        , m_domain(domain)
    {
    }

    OpenGLBuffer::~OpenGLBuffer()
    {
        if (m_id != 0)
        {
            glDeleteBuffers(1, &m_id);
        }
    }

    void* OpenGLBuffer::Map()
    {
        if (!IsMappable())
        {
            ZERO_CORE_WARN("Attempting to map unmappable buffer");
            return nullptr;
        }
        
        GLbitfield access = 0;

        if (m_domain == MemoryDomain::GPUtoCPU)
        {
            access = GL_MAP_READ_BIT;
        }
        else if (m_domain == MemoryDomain::CPUtoGPU_Coherent)
        {
            access = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        }
        else if (m_domain == MemoryDomain::CPUtoGPU)
        {
            // Non-coherent persistent mapping must have the explicit flush bit
            access = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT;
        }
        else
        {
            access = GL_MAP_WRITE_BIT;
        }

        void* ptr = glMapNamedBufferRange(m_id, 0, m_size, access);

        if (ptr == nullptr)
        {
            ZERO_CORE_ERROR("Failed to map OpenGL buffer memory");
        }

        return ptr;
    }

    void OpenGLBuffer::Unmap()
    {
        if (!IsMappable()) return;
        
        glUnmapNamedBuffer(m_id);
    }

    bool OpenGLBuffer::IsMappable() const
    {
        return m_domain != MemoryDomain::GPU;
    }
}
