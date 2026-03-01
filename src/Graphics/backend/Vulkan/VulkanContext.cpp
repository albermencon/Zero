#include "pch.h"
#include <Engine/Window.h>
#include <Engine/Log.h>
#include "VulkanContext.h"
#include <map>
#include <GLFW/glfw3.h>

#include "Graphics/backend/Vulkan/ShaderModule.h"
#include "Graphics/backend/Vulkan/ShaderProgram.h"

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

/*
* TODO: Create a validation system for extensions and layers
* TODO: Make present mode configurable
*/

namespace VoxelEngine
{
    VulkanDevice::VulkanDevice(Window* window) : m_Window(window)
    {
        ENGINE_CORE_INFO("Initializing Vulkan Device...");
        init();
    }

    VulkanDevice::~VulkanDevice()
    {

    }

    void VulkanDevice::init() {
        createNewInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createGraphicsPipeline();
        createCommandPool();
        createCommandBuffers();
        createSyncObjetcs();
        ENGINE_CORE_INFO("Vulkan Device initialized successfully.");
    }

    void VulkanDevice::SwapBuffers() 
    {
        glfwPollEvents();
    }
    void VulkanDevice::BeginFrame()
    {
        // Wait for the previus frame to complete
        auto fenceResult = device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX);

        // Grab an image from the framebuffer
        // First parameter indices the timeout in nanoseconds, beign the max unsigned 64 bit integer effictivily disable the timeout
        // The next to parameters are the syncronizations objects to be signaled when the presentation engine is finished using the image
        // It is possible to specify a semaphore, a fence or both.
        // The last paramater specifies a variable to output the index of the swap chain image that has become avaible
        auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompletedSemaphores[currentFrame], nullptr);

        // CRITICAL: wait if this swapchain image is already in flight
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        {
            device.waitForFences(
                imagesInFlight[imageIndex],
                vk::True,
                UINT64_MAX);
        }

        // mark this image as now being in flight
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];

        device.resetFences(*inFlightFences[currentFrame]);

        recordCommandBuffer(commandBuffers[currentFrame], imageIndex);

        // The first three parameters specify which semaphores to wait on before execution begins and in which stage(s) of the pipeline to wait.
        // The next parameter specifies which command buffers to actually submit for execution. We simply submit the single command buffer we have.
        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo{};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &*presentCompletedSemaphores[currentFrame];
        submitInfo.pWaitDstStageMask = &waitDestinationStageMask;

        // Command buffers
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &*commandBuffers[currentFrame];

        // Signal semaphores
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex];

        // Submit to the gpu
        graphicsQueue.submit(submitInfo, *inFlightFences[currentFrame]);

        vk::PresentInfoKHR presentInfoKHR{};
        presentInfoKHR.waitSemaphoreCount = 1;
        presentInfoKHR.pWaitSemaphores = &*renderFinishedSemaphores[imageIndex];

        presentInfoKHR.swapchainCount = 1;
        presentInfoKHR.pSwapchains = &*swapChain;
        presentInfoKHR.pImageIndices = &imageIndex;
        presentInfoKHR.pResults = nullptr; // Optional

        result = presentQueue.presentKHR(presentInfoKHR);

        currentFrame =
            (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        //vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, {});
        //vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, {},
        //    vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
        //    {}, vk::AccessFlagBits::eColorAttachmentWrite);
    }
    void VulkanDevice::EndFrame()
    {
        
    }

    void VulkanDevice::OnFinished()
    {
        device.waitIdle();
    }

    void VulkanDevice::recreateSwapChain()
    {
        device.waitIdle();

        //cleanupSwapchain();

        createSwapChain();
        createImageViews();
        createCommandBuffers();
    }

    void VulkanDevice::recordCommandBuffer(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        vk::CommandBufferBeginInfo beginInfo{};
        commandBuffer.begin(beginInfo);

        // Before starting rendering, transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            {},                                                         // srcAccessMask (no need to wait for previous operations)
            vk::AccessFlagBits2::eColorAttachmentWrite,                 // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,         // srcStage
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,          // dstStage
            commandBuffer
        );

        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        vk::RenderingAttachmentInfo attachmentInfo = {};

        attachmentInfo.imageView = swapChainImageViews[imageIndex];
        attachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        attachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
        attachmentInfo.clearValue = clearColor;

        vk::RenderingInfo renderingInfo = {};
        renderingInfo.renderArea = { .offset = { 0, 0 }, .extent = swapChainExtent };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachmentInfo;

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        // Set dynamic data
        commandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
        commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));

        commandBuffer.draw(3, 1, 0, 0);

        commandBuffer.endRendering();

        // After rendering, transition the swapchain image to PRESENT_SRC
        transition_image_layout(
            imageIndex,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
            {},                                                     // dstAccessMask
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
            vk::PipelineStageFlagBits2::eBottomOfPipe,               // dstStage
            commandBuffer
        );

        commandBuffer.end();
    }

    void VulkanDevice::transition_image_layout(uint32_t imageIndex, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask, vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask, vk::raii::CommandBuffer& commandBuffer)
    {
        vk::ImageMemoryBarrier2 barrier{};
        // Source synchronization scope
        barrier.srcStageMask = srcStageMask;
        barrier.srcAccessMask = srcAccessMask;

        // Destination synchronization scope
        barrier.dstStageMask = dstStageMask;
        barrier.dstAccessMask = dstAccessMask;

        // Layout transition info
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;

        // Queue ownership transfer (ignored here)
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        // Target image
        barrier.image = swapChainImages[imageIndex];

        // Subresource range
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vk::DependencyInfo dependencyInfo{};
        dependencyInfo.dependencyFlags = {};
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &barrier;

        commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* data,
        void*)
    {
        switch (severity)
        {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            ENGINE_CORE_ERROR("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            ENGINE_CORE_WARN("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
            ENGINE_CORE_INFO("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        default:
            ENGINE_CORE_TRACE("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;
        }

        return VK_FALSE;
    }

    void VulkanDevice::createNewInstance()
    {
        vk::ApplicationInfo appInfo{};
        appInfo.pApplicationName = "Vulkan";
        //appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Voxel Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = vk::ApiVersion13;

        vk::InstanceCreateInfo createInfo{};
        createInfo.pApplicationInfo = &appInfo;

        std::vector<const char*> extensions;

        // Get the required instance extension from GLFW
        uint32_t glfwExtensionCount = 0;
        auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        // Add GLFW extensions
        for (uint32_t i = 0; i < glfwExtensionCount; ++i)
            extensions.push_back(glfwExtensions[i]);

#ifdef ENGINE_DEBUG
        // Add debug utils extension (REQUIRED for debug messenger)
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

        // Validate extensions support
        auto extensionProperties = context.enumerateInstanceExtensionProperties();

        for (const char* requiredExtension : extensions)
        {
            bool found = std::ranges::any_of(
                extensionProperties,
                [requiredExtension](const vk::ExtensionProperties& prop)
                {
                    return strcmp(prop.extensionName, requiredExtension) == 0;
                });

            if (!found)
                throw std::runtime_error(
                    std::string("Required extension not supported: ") + requiredExtension);
        }

#ifdef ENGINE_DEBUG
        const std::vector<const char*> validationLayers =
        {
            "VK_LAYER_KHRONOS_validation"
        };

        auto supportedLayers = context.enumerateInstanceLayerProperties();

        for (const char* layer : validationLayers)
        {
            bool found = std::ranges::any_of(
                supportedLayers,
                [layer](const vk::LayerProperties& prop)
                {
                    return strcmp(prop.layerName, layer) == 0;
                });

            if (!found)
                throw std::runtime_error("Validation layer not supported");
        }

        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        ENGINE_CORE_TRACE("Enabled VK_LAYER_KHRONOS_validation");
#endif

        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        instance = vk::raii::Instance(context, createInfo);
    }
    void VulkanDevice::setupDebugMessenger()
    {
#ifdef ENGINE_DEBUG
        vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
        vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
        vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
        debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
        debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
        debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &debugCallback;

        //debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
        debugMessenger = vk::raii::DebugUtilsMessengerEXT(instance, debugUtilsMessengerCreateInfoEXT);
#endif
    }
    void VulkanDevice::createSurface()
    {
        VkSurfaceKHR _surface;
        
        GLFWwindow* window = static_cast<GLFWwindow*>(m_Window->GetNativeWindow());
        if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0) 
        {
            throw std::runtime_error("failed to create window surface!");
        }

        surface = vk::raii::SurfaceKHR(instance, _surface);
    }
    void VulkanDevice::pickPhysicalDevice()
    {
        std::vector<vk::raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
        if (devices.empty()) 
        {
            ENGINE_CORE_ERROR("failed to find GPUs with Vulkan support");
        }

        // Use an ordered map to automatically sort candidates by increasing score
        std::multimap<int, vk::raii::PhysicalDevice> candidates;

        for (const auto& device : devices) {
            auto deviceProperties = device.getProperties();
            auto deviceFeatures = device.getFeatures();
            uint32_t score = 0;

            if (deviceProperties.apiVersion < VK_API_VERSION_1_3) 
            {
                continue;
            }

            // Discrete GPUs have a significant performance advantage
            if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                score += 1000;
            }
            
            // Maximum possible size of textures affects graphics quality
            score += deviceProperties.limits.maxImageDimension2D;

            // Application can't function without geometry shaders
            if (!deviceFeatures.geometryShader) {
                continue;
            }
            candidates.insert(std::make_pair(score, device));
        }

        // Check if the best candidate is suitable at all
        if (candidates.rbegin()->first > 0) {
            physicalDevice = candidates.rbegin()->second;
        }
        else {
            throw std::runtime_error("failed to find a suitable GPU!");
        }

        auto props = physicalDevice.getProperties();
        auto memProps = physicalDevice.getMemoryProperties();
        auto limits = props.limits;

        uint64_t deviceLocalBytes = 0;
        for (uint32_t i = 0; i < memProps.memoryHeapCount; i++)
        {
            if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
            {
                deviceLocalBytes += memProps.memoryHeaps[i].size;
            }
        }

        double vramGB = deviceLocalBytes / (1024.0 * 1024.0 * 1024.0);

        const char* vendorName = "Unknown";

        switch (props.vendorID)
        {
        case 0x1002: vendorName = "AMD"; break;
        case 0x10DE: vendorName = "NVIDIA"; break;
        case 0x8086: vendorName = "Intel"; break;
        case 0x13B5: vendorName = "ARM"; break;
        case 0x5143: vendorName = "Qualcomm"; break;
        }

        uint32_t apiVersion = props.apiVersion;
        uint32_t driverVersion = props.driverVersion;
        const char* deviceName = props.deviceName;
        ENGINE_CORE_INFO("GPU Name: {}", deviceName);
        ENGINE_CORE_INFO("Vulkan API Version: {}.{}.{}",
            VK_VERSION_MAJOR(apiVersion),
            VK_VERSION_MINOR(apiVersion),
            VK_VERSION_PATCH(apiVersion));
        ENGINE_CORE_INFO("Driver Version: {}.{}.{}",
            VK_VERSION_MAJOR(driverVersion),
            VK_VERSION_MINOR(driverVersion),
            VK_VERSION_PATCH(driverVersion));
        ENGINE_CORE_INFO("VRAM: {:.0f} GB", vramGB);
        ENGINE_CORE_INFO("Vendor: {0}", vendorName);
    }

    void VulkanDevice::createLogicalDevice()
    {
        // find the index of the first queue family that supports graphics
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        uint32_t graphicsIndex = findQueueFamilies(physicalDevice);

        // determine a queueFamilyIndex that supports present
        // first check if the graphicsIndex is good enough
        auto presentIndex = physicalDevice.getSurfaceSupportKHR(graphicsIndex, *surface)
            ? graphicsIndex
            : static_cast<uint32_t>(queueFamilyProperties.size());

        if (presentIndex == queueFamilyProperties.size())
        {
            // the graphicsIndex doesn't support present -> look for another family index that supports both
            // graphics and present
            for (size_t i = 0; i < queueFamilyProperties.size(); i++)
            {
                if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                    physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
                {
                    graphicsIndex = static_cast<uint32_t>(i);
                    presentIndex = graphicsIndex;
                    break;
                }
            }
            if (presentIndex == queueFamilyProperties.size())
            {
                // there's nothing like a single family index that supports both graphics and present -> look for another
                // family index that supports present
                for (size_t i = 0; i < queueFamilyProperties.size(); i++)
                {
                    if (physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), *surface))
                    {
                        presentIndex = static_cast<uint32_t>(i);
                        break;
                    }
                }
            }
        }
        if ((graphicsIndex == queueFamilyProperties.size()) || (presentIndex == queueFamilyProperties.size()))
        {
            throw std::runtime_error("Could not find a queue for graphics or present -> terminating");
        }

        // Create a chain of feature structures
        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceVulkan13Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
            vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
        > featureChain{};

        // Enable features directly in the chain
        featureChain.get<vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR>().presentModeFifoLatestReady = VK_TRUE;
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE; // Enable dynamic rendering from Vulkan 1.3
        featureChain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
        featureChain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = VK_TRUE; // Enable extended dynamic state from the extension
        featureChain.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;
        std::vector<const char*> deviceExtensions = {
            vk::KHRSwapchainExtensionName,
            vk::KHRPresentModeFifoLatestReadyExtensionName
        };

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { graphicsIndex, presentIndex };

        float queuePriority = 0.5f;
        for (uint32_t family : uniqueQueueFamilies) 
        {
            vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
            deviceQueueCreateInfo.queueFamilyIndex = family;
            deviceQueueCreateInfo.queueCount = 1;
            deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

            queueCreateInfos.push_back(deviceQueueCreateInfo);
        }

        vk::DeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // query device capabilities
        // Basic surface capabilities (min/max number of images in swap chain, min/max width and height of images)
        // Surface formats (pixel format, color space)
        // Available presentation modes
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

        device = vk::raii::Device(physicalDevice, deviceCreateInfo);
        graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
        presentQueue = vk::raii::Queue(device, presentIndex, 0);

        graphicsFamilyIndex = graphicsIndex;
        presentFamilyIndex = presentIndex;
    }

    void VulkanDevice::createSwapChain()
    {
        auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
        swapChainSurfaceFormat = chooseSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(*surface));
        swapChainExtent = chooseSwapExtent(surfaceCapabilities);

        auto formatStr = vk::to_string(swapChainSurfaceFormat.format);
        auto colorSpaceStr = vk::to_string(swapChainSurfaceFormat.colorSpace);

        ENGINE_CORE_TRACE("Swapchain format : {0} | {1}", formatStr, colorSpaceStr);
        ENGINE_CORE_TRACE("SwapChainExtent: Width {0} Height {1}", swapChainExtent.width, swapChainExtent.height);

        // Request triple buffering when possible
        // Ensures at least 3 images unless the surface requires more
        uint32_t imageCount = std::max(
            3u,
            surfaceCapabilities.minImageCount
        );
        
        // Clamp to maxImageCount if defined
        // maxImageCount == 0 means no upper limit
        if (surfaceCapabilities.maxImageCount > 0 
            && imageCount > surfaceCapabilities.maxImageCount) 
        {
            imageCount = surfaceCapabilities.maxImageCount;
        }

        ENGINE_CORE_INFO("Images in flight: {}", imageCount);

        vk::PresentModeKHR presentMode = chooseSwapChainPresentMode(physicalDevice.getSurfacePresentModesKHR(*surface));

        ENGINE_CORE_INFO("PresentMode: {}", vk::to_string(presentMode));

        vk::SwapchainCreateInfoKHR swapChainCreateInfo{};
        swapChainCreateInfo
            .setFlags(vk::SwapchainCreateFlagsKHR())
            .setSurface(*surface)
            .setMinImageCount(imageCount)
            .setImageFormat(swapChainSurfaceFormat.format)
            .setImageColorSpace(swapChainSurfaceFormat.colorSpace)
            .setImageExtent(swapChainExtent)
            .setImageArrayLayers(1) // unless developing a stereoscopic 3D application
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(surfaceCapabilities.currentTransform)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(presentMode)
            .setClipped(vk::True)
            .setOldSwapchain(nullptr)
            ;

        uint32_t queueFamilyIndices[] = { graphicsFamilyIndex, presentFamilyIndex };

        if (graphicsFamilyIndex != presentFamilyIndex) {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
            swapChainCreateInfo.queueFamilyIndexCount = 2;
            swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
            swapChainCreateInfo.queueFamilyIndexCount = 0; // Optional
            swapChainCreateInfo.pQueueFamilyIndices = nullptr; // Optional
        }

        swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
        swapChainImages = swapChain.getImages();

        // If I dont assign it here aswell, vulkan throws
        swapChainImageFormat = swapChainSurfaceFormat.format;
    }

    void VulkanDevice::createImageViews()
    {
        swapChainImageViews.clear();
        swapChainImageViews.reserve(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); ++i)
        {
            vk::ImageViewCreateInfo createInfo{};

            createInfo
                .setImage(swapChainImages[i])
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(swapChainImageFormat)
                .setComponents(vk::ComponentMapping{
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity,
                    vk::ComponentSwizzle::eIdentity
                    })
                .setSubresourceRange(vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor,
                    0,  // baseMipLevel
                    1,  // levelCount
                    0,  // baseArrayLayer
                    1   // layerCount
                    });

            swapChainImageViews.emplace_back(device, createInfo);
        }
    }

    void VulkanDevice::createGraphicsPipeline()
    {
        SpirVSource vertSource = SpirVSource::fromFile("C:\\Dev\\EngineProject\\VoxelEngine\\Shaders\\shader.vert.spv");
        SpirVSource fragSource = SpirVSource::fromFile("C:\\Dev\\EngineProject\\VoxelEngine\\Shaders\\shader.frag.spv");

        auto shaderVertModule = ShaderModule(device, std::move(vertSource));
        auto shaderFragModule = ShaderModule(device, std::move(fragSource));

        auto verShaderStage = ShaderStage::vertex(shaderVertModule, "main");
        auto fragShaderStage = ShaderStage::fragment(shaderFragModule, "main");

        ShaderProgram shaderProgram = ShaderProgram();
        shaderProgram.addStage(std::move(verShaderStage));
        shaderProgram.addStage(std::move(fragShaderStage));

        std::vector dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pushConstantRangeCount = 0;

        pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

        vk::PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainImageFormat;

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = vk::False;
        rasterizer.rasterizerDiscardEnable = vk::False;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eClockwise;
        rasterizer.depthBiasEnable = vk::False;
        rasterizer.depthBiasSlopeFactor = 1.0f;
        rasterizer.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
        multisampling.sampleShadingEnable = vk::False;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = vk::True;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        
        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = vk::False;
        colorBlending.logicOp = vk::LogicOp::eCopy;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.pNext = &pipelineRenderingCreateInfo;
        pipelineInfo.stageCount = shaderProgram.stageCount();
        pipelineInfo.pStages = shaderProgram.stageInfos().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState; 
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = *pipelineLayout;
        pipelineInfo.renderPass = nullptr;
        // These values are only used if the VK_PIPELINE_CREATE_DERIVATIVE_BIT flag is also specified in the flags field of VkGraphicsPipelineCreateInfo.
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        graphicsPipeline = vk::raii::Pipeline(device, nullptr, pipelineInfo);
    }

    void VulkanDevice::createCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = graphicsFamilyIndex;

        ENGINE_CORE_INFO("Graphics Family Index: {} | Present Family Index: {}", graphicsFamilyIndex, presentFamilyIndex);

        commandPool = vk::raii::CommandPool(device, poolInfo);
    }

    void VulkanDevice::createCommandBuffers()
    {
        commandBuffers.clear();

        vk::CommandBufferAllocateInfo allocinfo{};
        allocinfo.commandPool = commandPool;
        allocinfo.level = vk::CommandBufferLevel::ePrimary;
        allocinfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        commandBuffers = vk::raii::CommandBuffers(device, allocinfo);
    }

    void VulkanDevice::createSyncObjetcs()
    {
        uint32_t imageCount = swapChainImages.size();

        // Tracks which fence is using each swapchain image
        imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

        presentCompletedSemaphores.reserve(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.reserve(imageCount);
        inFlightFences.reserve(MAX_FRAMES_IN_FLIGHT);

        // Per-frame resources
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            presentCompletedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());

            vk::FenceCreateInfo fenceInfo{};
            fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

            inFlightFences.emplace_back(device, fenceInfo);
        }

        // Per-image resources
        for (uint32_t i = 0; i < imageCount; i++)
        {
            renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
        }
    }

    uint32_t VulkanDevice::findQueueFamilies(vk::raii::PhysicalDevice physicalDevice)
    {
        // find the index of the first queue family that supports graphics
        std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // get the first index into queueFamilyProperties which supports graphics
        auto graphicsQueueFamilyProperty = 
            std::find_if( queueFamilyProperties.begin(),
                          queueFamilyProperties.end(),
                [](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; }
            );
        return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));
    }

    vk::SurfaceFormatKHR VulkanDevice::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& avaibleFormats)
    {
        // Search for the preferred 8-bit BGRA format with sRGB encoding
        // This ensures proper gamma-correct rendering and presentation
        for (const auto& availableFormat : avaibleFormats)
        {
            if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
                availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                return availableFormat;
            }
        }

        // Vulkan always gurantees at least one value for swapchain creation
        return avaibleFormats[0];
    }

    // Selects the optimal presentation mode for the swapchain.
    // The present mode controls how rendered images are queued and synchronized
    // with the display refresh cycle, affecting latency, tearing, and performance.
    vk::PresentModeKHR VulkanDevice::chooseSwapChainPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
    {
        // TODO: Make present mode configurable.
        //      Desktop: MAILBOX (low latency, no tearing)
        //      Mobile: FIFO (better power efficiency)
        //      Fallback: FIFO (always supported)
        // Present mode affects latency, power consumption, and frame pacing.

        for (const auto& presentMode : availablePresentModes) 
        {
            if (presentMode == vk::PresentModeKHR::eMailbox) 
            {
                return presentMode;
            }
        }

        for (const auto& presentMode : availablePresentModes)
        {
            if (presentMode == vk::PresentModeKHR::eFifoLatestReady)
            {
                return presentMode;
            }
        }

        for (const auto& presentMode : availablePresentModes)
        {
            if (presentMode == vk::PresentModeKHR::eImmediate)
            {
                return presentMode;
            }
        }

        return vk::PresentModeKHR::eFifo; // fifo is the only guaranteed
    }

    vk::Extent2D VulkanDevice::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
    {
        // The size is fixed by the window system (Wayland, Android, or exclusive fullscreen)
        // Vulkan requires using this exact extent, and the application cannot override it.
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) 
        {
            return capabilities.currentExtent;
        }

        // Otherwise, the surface allows the application to choose the swapchain extent
        // Query the current framebuffer size from the window system
        auto [width, height] = m_Window->GetSize();

        // Vulkan enforces limits defined in minImageExtent and MaxImageExtent
        // Clamp to valid values preventing swapchain creation failure or validation erros
        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }
  
}
