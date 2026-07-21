#pragma once
#include <glad/glad.h>

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
    );
}
