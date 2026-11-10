#include "pch.h"
#include <Engine/Graphics/RenderInterface.h>
#include "Graphics/Renderer/RenderInterfaceImpl.h"
#include "Graphics/core/Buffer.h"
#include <Engine/Thread/ScopedLock.h>
#include <utility>

namespace Zero
{
    RenderInterface& RenderInterface::Get()
    {
        static RenderInterface instance;
        return instance;
    }

    BufferHandle RenderInterface::CreateBuffer(const BufferDesc& desc)
    {
        auto& impl = RenderInterfaceImpl::Get();
        BufferHandle handle = impl.bufferRegistry.Allocate();
        
        if (handle.IsValid())
        {
#ifdef ZERO_DEBUG
            if (desc.debugName)
                impl.bufferRegistry.SetDebugName(handle.GetIndex(), desc.debugName);
#endif

            BufferCreateRequest req{ handle, desc, {} };
            if (desc.initialData && desc.initialDataSize > 0)
            {
                auto* src = static_cast<const uint8_t*>(desc.initialData);
                req.ownedInitialData.assign(src, src + desc.initialDataSize);
            }
            req.desc.initialData = nullptr;

            Zero::ScopedLock lock(impl.queueMutex);
            impl.pendingBuffers.push_back(std::move(req));
        }
        return handle;
    }

    void RenderInterface::UpdateBuffer(BufferHandle handle, const BufferUpdate& update)
    {
        if (!handle.IsValid() || !update.data || update.size == 0) return;

        auto& impl = RenderInterfaceImpl::Get();
        BufferUpdateRequest req;
        req.handle = handle;
        req.dstOffset = update.dstOffset;
        const auto* src = static_cast<const uint8_t*>(update.data);
        req.data.assign(src, src + update.size);

        Zero::ScopedLock lock(impl.queueMutex);
        impl.pendingBufferUpdates.push_back(std::move(req));
    }

    void RenderInterface::DestroyBuffer(BufferHandle handle)
    {
        auto& impl = RenderInterfaceImpl::Get();
        impl.bufferRegistry.Deallocate(handle);
        
        Zero::ScopedLock lock(impl.queueMutex);
        impl.pendingDestroys.push_back({ handle.value, ResourceDestroyRequest::Type::Buffer });
    }

    MeshHandle RenderInterface::CreateMesh(const MeshDesc& desc)
    {
        MeshHandle mesh;
        if (desc.vertexData)
        {
            BufferDesc vdesc = BufferDesc::Vertex(desc.vertexBytes, desc.debugName);
            vdesc.initialData = desc.vertexData;
            vdesc.initialDataSize = desc.vertexBytes;
            mesh.vertexBuffer = CreateBuffer(vdesc);
        }
        if (desc.indexData)
        {
            BufferDesc idesc = BufferDesc::Index(desc.indexBytes, desc.debugName);
            idesc.initialData = desc.indexData;
            idesc.initialDataSize = desc.indexBytes;
            mesh.indexBuffer = CreateBuffer(idesc);
        }
        return mesh;
    }

    void RenderInterface::DestroyMesh(MeshHandle handle)
    {
        if (handle.vertexBuffer.IsValid()) DestroyBuffer(handle.vertexBuffer);
        if (handle.indexBuffer.IsValid()) DestroyBuffer(handle.indexBuffer);
    }

    TextureHandle RenderInterface::CreateTexture(const ImageDesc& desc)
    {
        auto& impl = RenderInterfaceImpl::Get();
        TextureHandle handle = impl.textureRegistry.Allocate();
        
        if (handle.IsValid())
        {
#ifdef ZERO_DEBUG
            if (desc.debugName)
                impl.textureRegistry.SetDebugName(handle.GetIndex(), desc.debugName);
#endif

            Zero::ScopedLock lock(impl.queueMutex);
            impl.pendingTextures.push_back({ handle, desc });
        }
        return handle;
    }

    void RenderInterface::UpdateTexture(TextureHandle handle, const TextureUpdate& update)
    {
    }

    void RenderInterface::DestroyTexture(TextureHandle handle)
    {
        auto& impl = RenderInterfaceImpl::Get();
        impl.textureRegistry.Deallocate(handle);
        
        Zero::ScopedLock lock(impl.queueMutex);
        impl.pendingDestroys.push_back({ handle.value, ResourceDestroyRequest::Type::Texture });
    }

    PipelineHandle RenderInterface::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
    {
        auto& impl = RenderInterfaceImpl::Get();
        PipelineHandle handle = impl.pipelineRegistry.Allocate();
        
        if (handle.IsValid())
        {
#ifdef ZERO_DEBUG
            if (desc.debugName)
                impl.pipelineRegistry.SetDebugName(handle.GetIndex(), desc.debugName);
#endif

            Zero::ScopedLock lock(impl.queueMutex);
            impl.pendingPipelines.push_back({ handle, desc });
        }
        return handle;
    }

    PipelineHandle RenderInterface::CreateComputePipeline(const ComputePipelineDesc& desc)
    {
        auto& impl = RenderInterfaceImpl::Get();
        PipelineHandle handle = impl.pipelineRegistry.Allocate();
        
        if (handle.IsValid())
        {
#ifdef ZERO_DEBUG
            if (desc.debugName)
                impl.pipelineRegistry.SetDebugName(handle.GetIndex(), desc.debugName);
#endif

            Zero::ScopedLock lock(impl.queueMutex);
            impl.pendingComputePipelines.push_back({ handle, desc });
        }
        return handle;
    }

    void RenderInterface::DestroyPipeline(PipelineHandle handle)
    {
        auto& impl = RenderInterfaceImpl::Get();
        impl.pipelineRegistry.Deallocate(handle);
        
        Zero::ScopedLock lock(impl.queueMutex);
        impl.pendingDestroys.push_back({ handle.value, ResourceDestroyRequest::Type::Pipeline });
    }

    bool RenderInterface::IsReady(BufferHandle handle) const noexcept
    {
        auto& impl = RenderInterfaceImpl::Get();
        return impl.bufferRegistry.GetState(handle) == ResourceState::Ready;
    }

    bool RenderInterface::IsReady(TextureHandle handle) const noexcept
    {
        auto& impl = RenderInterfaceImpl::Get();
        return impl.textureRegistry.GetState(handle) == ResourceState::Ready;
    }

    bool RenderInterface::IsReady(PipelineHandle handle) const noexcept
    {
        auto& impl = RenderInterfaceImpl::Get();
        return impl.pipelineRegistry.GetState(handle) == ResourceState::Ready;
    }

    uint64_t RenderInterface::GetBufferDeviceAddress(BufferHandle handle) const noexcept
    {
        if (!handle.IsValid()) return 0;
        auto& impl = RenderInterfaceImpl::Get();
        Buffer* b = impl.bufferRegistry.GetBackendResource(handle);
        if (!b) return 0;
        return b->GetDeviceAddress();
    }
}
