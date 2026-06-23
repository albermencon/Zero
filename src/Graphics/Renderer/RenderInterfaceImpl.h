#pragma once

#include <Engine/Graphics/RenderInterface.h>
#include <Engine/Graphics/RenderResources.h>
#include "Graphics/core/ResourceRegistry.h"
#include "Graphics/core/RHITypes.h"
#include <mutex>
#include <vector>

namespace Zero
{
    struct BufferCreateRequest
    {
        BufferHandle handle;
        BufferDesc desc;
        std::vector<uint8_t> ownedInitialData;
    };
    
    struct TextureCreateRequest
    {
        TextureHandle handle;
        ImageDesc desc;
    };
    
    struct PipelineCreateRequest
    {
        PipelineHandle handle;
        PipelineDesc desc;
    };

    struct ResourceDestroyRequest
    {
        uint32_t handleValue; // Raw value, we extract generation and index
        enum class Type { Buffer, Texture, Pipeline } type;
    };

    class Buffer;
    class Texture;
    class Pipeline;

    class RenderInterfaceImpl
    {
    public:
        static RenderInterfaceImpl& Get()
        {
            static RenderInterfaceImpl instance;
            return instance;
        }

        ResourceRegistry<Buffer*, BufferHandle, 65536> bufferRegistry;
        ResourceRegistry<Texture*, TextureHandle, 16384> textureRegistry;
        ResourceRegistry<Pipeline*, PipelineHandle, 4096> pipelineRegistry;

        std::mutex queueMutex;
        std::vector<BufferCreateRequest> pendingBuffers;
        std::vector<TextureCreateRequest> pendingTextures;
        std::vector<PipelineCreateRequest> pendingPipelines;
        std::vector<ResourceDestroyRequest> pendingDestroys;
    };
}
