#pragma once
#include <cstdint>
#include <Engine/Core.h>
#include "RHITypes.h"

namespace Zero
{
	enum class UploadType : uint8_t { Buffer, Texture};

    // UploadRequest
    //   Describes a GPU copy that RenderThread must execute before draws.
    //   The staging buffer is already written by the caller (GameThread or an
    //   asset-loading thread) before this request is enqueued.
    //   RenderThread reads staging, copies to dst, then marks dst ready.
    struct UploadRequest
    {
        UploadType          type = UploadType::Buffer;

        // Source: CPU-visible staging buffer, already populated
        RHI::BufferHandle   staging;

        union
        {
            struct
            {
                RHI::BufferHandle   dst;
                uint64_t            size;
                uint64_t            dstOffset;
            } buffer;

            struct
            {
                RHI::TextureHandle  dst;
                uint32_t            mipLevel;
                uint32_t            arrayLayer;
                uint32_t            width;
                uint32_t            height;
            } texture;
        };

        // Optional: called by RenderThread on the render thread after copy.
        // Use to set an atomic "ready" flag on the resource.
        // Null = no notification needed.
        void (*onComplete)(void* userData) = nullptr;
        void* userData = nullptr;
    };
}
