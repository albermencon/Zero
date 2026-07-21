#include "pch.h"
#include "OpenGLDebug.h"
#include "Engine/Log.h"

namespace Zero
{
    void APIENTRY OpenGLDebugCallback(
        GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const void* userParam
    )
    {
        // Ignore non-significant messages
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:         ZERO_CORE_ERROR("OpenGL: {0}", message); break;
            case GL_DEBUG_SEVERITY_MEDIUM:       ZERO_CORE_WARN("OpenGL: {0}", message); break;
            case GL_DEBUG_SEVERITY_LOW:          ZERO_CORE_INFO("OpenGL: {0}", message); break;
            default:                             ZERO_CORE_TRACE("OpenGL: {0}", message); break;
        }
        
        if (severity == GL_DEBUG_SEVERITY_HIGH)
        {
            ZERO_CORE_ASSERT(false, "OpenGL High Severity Error!");
        }
    }
}