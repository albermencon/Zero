#pragma once
#include "Graphics/core/GraphicsDevice.h"
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <optional>

namespace VoxelEngine
{
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    class Window;

    class VulkanDevice : public GraphicsDevice
    {
    public:
        VulkanDevice(Window* window);
        ~VulkanDevice();

        virtual void init() override;
        virtual void SwapBuffers() override;

        virtual void BeginFrame() override;
        virtual void EndFrame() override;

        virtual void OnFinished() override;

        void recreateSwapChain();

        void recordCommandBuffer(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);
        void transition_image_layout(
            uint32_t imageIndex,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout,
            vk::AccessFlags2 srcAccessMask,
            vk::AccessFlags2 dstAccessMask,
            vk::PipelineStageFlags2 srcStageMask,
            vk::PipelineStageFlags2 dstStageMask,
            vk::raii::CommandBuffer& commandBuffer
        );
    private:
        void createNewInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain();
        void createImageViews();
        void createGraphicsPipeline();
        void createCommandPool();
        void createCommandBuffers();
        void createSyncObjetcs();
    private:
        uint32_t findQueueFamilies(vk::raii::PhysicalDevice physicalDevice);
        vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& avaibleFormats);
        vk::PresentModeKHR chooseSwapChainPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
        vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
    private:
        vk::raii::Context context;
        vk::raii::Instance instance = nullptr;
        vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
        vk::raii::SurfaceKHR surface = nullptr;
        vk::raii::PhysicalDevice physicalDevice = nullptr;
        vk::raii::Device device = nullptr;
        
        vk::raii::Queue graphicsQueue = nullptr;
        vk::raii::Queue presentQueue = nullptr;
        uint32_t graphicsFamilyIndex = UINT32_MAX;
        uint32_t presentFamilyIndex = UINT32_MAX;

        // swapchain
        vk::raii::SwapchainKHR swapChain = nullptr;
        std::vector<vk::Image> swapChainImages;

        vk::Extent2D swapChainExtent;
        vk::Format swapChainImageFormat;

        vk::SurfaceFormatKHR swapChainSurfaceFormat;

        std::vector<vk::raii::ImageView> swapChainImageViews;

        // Pipeline
        vk::raii::PipelineLayout pipelineLayout = nullptr;
        vk::raii::Pipeline graphicsPipeline = nullptr;

        // Command pool
        vk::raii::CommandPool commandPool = nullptr;
        //vk::raii::CommandBuffer commandBuffer = nullptr;
        std::vector<vk::raii::CommandBuffer> commandBuffers;

        // Syncronization
        std::vector<vk::raii::Semaphore> presentCompletedSemaphores;
        std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
        std::vector<vk::raii::Fence> inFlightFences;
        std::vector<vk::Fence> imagesInFlight;
        uint32_t currentFrame = 0;

        const uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    private:
        Window* m_Window;
    };
}
