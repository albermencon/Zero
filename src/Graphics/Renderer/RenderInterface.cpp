#include <Engine/Graphics/RenderInterface.h>
#include "Graphics/Renderer/RenderInterfaceImpl.h"

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

            std::lock_guard<std::mutex> lock(impl.queueMutex);
            impl.pendingBuffers.push_back(std::move(req));
        }
        return handle;
    }

    void RenderInterface::UpdateBuffer(BufferHandle handle, const BufferUpdate& update)
    {
        // TODO
        // Enqueues an upload request. For now we assume upload requests 
        // are handled elsewhere or add a stub.
    }

    void RenderInterface::DestroyBuffer(BufferHandle handle)
    {
        auto& impl = RenderInterfaceImpl::Get();
        impl.bufferRegistry.Deallocate(handle);
        
        std::lock_guard<std::mutex> lock(impl.queueMutex);
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

            std::lock_guard<std::mutex> lock(impl.queueMutex);
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
        
        std::lock_guard<std::mutex> lock(impl.queueMutex);
        impl.pendingDestroys.push_back({ handle.value, ResourceDestroyRequest::Type::Texture });
    }

    PipelineHandle RenderInterface::CreatePipeline(const PipelineDesc& desc)
    {
        auto& impl = RenderInterfaceImpl::Get();
        PipelineHandle handle = impl.pipelineRegistry.Allocate();
        
        if (handle.IsValid())
        {
#ifdef ZERO_DEBUG
            if (desc.debugName)
                impl.pipelineRegistry.SetDebugName(handle.GetIndex(), desc.debugName);
#endif

            std::lock_guard<std::mutex> lock(impl.queueMutex);
            impl.pendingPipelines.push_back({ handle, desc });
        }
        return handle;
    }

    void RenderInterface::DestroyPipeline(PipelineHandle handle)
    {
        auto& impl = RenderInterfaceImpl::Get();
        impl.pipelineRegistry.Deallocate(handle);
        
        std::lock_guard<std::mutex> lock(impl.queueMutex);
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
}
