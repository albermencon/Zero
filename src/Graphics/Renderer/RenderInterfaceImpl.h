#pragma once
#include <Engine/Graphics/RenderInterface.h>
#include <Engine/Graphics/RenderResources.h>
#include "Graphics/core/ResourceRegistry.h"
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/ScopedLock.h>
#include <string>
#include <vector>

namespace Zero
{
    struct BufferCreateRequest
    {
        BufferHandle handle;
        BufferDesc desc;
        std::vector<uint8_t> ownedInitialData;
    };

    struct BufferUpdateRequest
    {
        BufferHandle handle;
        uint64_t dstOffset = 0;
        std::vector<uint8_t> data;
    };
    
    struct TextureCreateRequest
    {
        TextureHandle handle;
        ImageDesc desc;
    };
    
    struct PipelineCreateRequest
    {
        PipelineHandle handle;
        GraphicsPipelineDesc desc;
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
        std::string ownedDebugName;
#endif
    };

    struct ComputePipelineCreateRequest
    {
        PipelineHandle handle;
        ComputePipelineDesc desc;
#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
        std::string ownedDebugName;
#endif
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

        Zero::Mutex queueMutex;
        std::vector<BufferCreateRequest> pendingBuffers;
        std::vector<BufferUpdateRequest> pendingBufferUpdates;
        std::vector<TextureCreateRequest> pendingTextures;
        std::vector<PipelineCreateRequest> pendingPipelines;
        std::vector<ComputePipelineCreateRequest> pendingComputePipelines;
        std::vector<ResourceDestroyRequest> pendingDestroys;

        void Clear()
        {
            Zero::ScopedLock lock(queueMutex);
            pendingBuffers.clear();
            pendingBufferUpdates.clear();
            pendingTextures.clear();
            pendingPipelines.clear();
            pendingComputePipelines.clear();
            pendingDestroys.clear();
        }
    };
}
