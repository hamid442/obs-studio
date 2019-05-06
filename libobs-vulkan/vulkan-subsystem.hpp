#pragma once
#include "vulkan.hpp"
#if WIN32

#endif
#include <util/base.h>
#include <graphics/matrix4.h>
#include <graphics/graphics.h>
#include <graphics/device-exports.h>
#include "vulkan_utils/utils.hpp"
#include "vulkan_utils/shaders.hpp"

static vk::UniqueInstance instance;
static const std::string AppName = "VulkanSubsystem";
static const std::string EngineName = "VulkanHpp";

bool vulkan_init();
std::vector<vk::PhysicalDevice> vulkan_enum_devices();

struct gs_device;
struct gs_shader;
struct gs_vertex_shader;
struct gs_pixel_shader;
struct gs_index_buffer;
struct gs_swap_chain;

struct gs_device {
	vk::UniqueDevice device;
	vk::PhysicalDevice physicalDevice;
	bool nv12Supported = false;

	gs_vertex_buffer            *curVertexBuffer = nullptr;
	gs_index_buffer             *curIndexBuffer = nullptr;
	gs_vertex_shader            *curVertexShader = nullptr;
	gs_pixel_shader             *curPixelShader = nullptr;
	gs_swap_chain               *curSwapChain = nullptr;

	gs_cull_mode cullMode = GS_NEITHER;

	gs_device(uint32_t adapterIdx)
	{
		if (!vulkan_init()) {
			return;
		}
		try {
			std::vector<vk::PhysicalDevice> physicalDevices =
				instance->enumeratePhysicalDevices();
			assert(adapterIdx < physicalDevices.size());

			std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
				physicalDevices[adapterIdx].getQueueFamilyProperties();

			size_t graphicsQueueFamilyIndex = std::distance(queueFamilyProperties.begin(),
				std::find_if(queueFamilyProperties.begin(),
					queueFamilyProperties.end(),
					[](vk::QueueFamilyProperties const& qfp) { return qfp.queueFlags & vk::QueueFlagBits::eGraphics; }));
			assert(graphicsQueueFamilyIndex < queueFamilyProperties.size());

			// create a UniqueDevice
			float queuePriority = 0.0f;
			vk::DeviceQueueCreateInfo deviceQueueCreateInfo(vk::DeviceQueueCreateFlags(), static_cast<uint32_t>(graphicsQueueFamilyIndex), 1, &queuePriority);
			device = physicalDevices[adapterIdx].createDeviceUnique(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1, &deviceQueueCreateInfo));
			physicalDevice = physicalDevices[adapterIdx];
		} catch (vk::SystemError err) {
			blog(LOG_WARNING, "vk::SystemError: %s", err.what());
		} catch (...) {
			blog(LOG_WARNING, "vulkan: Unknown Error");
		}
	}
};

struct gs_shader {
	gs_shader()
	{

	};
};

struct gs_vertex_shader : gs_shader {
	std::vector<unsigned int> vertexShaderSPV;
	vk::UniqueShaderModule vertexShaderModule;
	std::string shaderText;
	std::string filePath;
	gs_vertex_shader(gs_device_t *device, const char *file,
		const char *shaderString)
	{
		shaderText = shaderString;
		filePath = file;
		bool ok = vk::su::GLSLtoSPV(vk::ShaderStageFlagBits::eVertex,
			shaderText, vertexShaderSPV);
		assert(ok);
		vk::ShaderModuleCreateInfo vertexShaderModuleCreateInfo(
				vk::ShaderModuleCreateFlags(),
				vertexShaderSPV.size() * sizeof(unsigned int),
				vertexShaderSPV.data());

		vertexShaderModule =
			device->device->createShaderModuleUnique(
				vertexShaderModuleCreateInfo);
	};
};

struct gs_pixel_shader : gs_shader {
	std::vector<unsigned int> pixelShaderSPV;
	vk::UniqueShaderModule pixelShaderModule;
	std::string shaderText;
	std::string filePath;

	gs_pixel_shader(gs_device_t *device, const char *file,
		const char *shaderString)
	{
		shaderText = shaderString;
		filePath = file;
		bool ok = vk::su::GLSLtoSPV(vk::ShaderStageFlagBits::eFragment,
			shaderText, pixelShaderSPV);
		assert(ok);
		vk::ShaderModuleCreateInfo pixelShaderModuleCreateInfo(
				vk::ShaderModuleCreateFlags(),
				pixelShaderSPV.size() * sizeof(unsigned int),
				pixelShaderSPV.data());
		pixelShaderModule =
			device->device->createShaderModuleUnique(
				pixelShaderModuleCreateInfo);
	};
};

struct gs_vertex_buffer {
	gs_vertex_buffer(gs_device_t *device, struct gs_vb_data *data,
		uint32_t flags)
	{

	};
};

struct gs_index_buffer {
	size_t               num;
	gs_index_buffer(gs_device_t *device, enum gs_index_type type,
		void *indices, size_t num, uint32_t flags)
	{

	};
};

struct gs_swap_chain {
	vk::su::SwapChainData *swapChain = nullptr;
	gs_swap_chain(gs_device_t *device, const struct gs_init_data *data)
	{
		/*
			struct gs_window        window;
			uint32_t                cx, cy;
			uint32_t                num_backbuffers;
			enum gs_color_format    format;
			enum gs_zstencil_format zsformat;
			uint32_t                adapter;
		*/
#if defined(VK_USE_PLATFORM_WIN32_KHR)
		//HWND window = vk::su::initializeWindow(AppName, AppName, width, height);
		HWND window = (HWND)data->window.hwnd;
		vk::UniqueSurfaceKHR surface = instance->createWin32SurfaceKHRUnique(
			vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(),
				GetModuleHandle(nullptr), window));
#else
#pragma error "unhandled platform"
#endif
		std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
			device->physicalDevice.getQueueFamilyProperties();
		uint32_t graphicsQueueFamilyIndex = vk::su::findGraphicsQueueFamilyIndex(queueFamilyProperties);

		size_t presentQueueFamilyIndex = device->physicalDevice.getSurfaceSupportKHR(
			static_cast<uint32_t>(graphicsQueueFamilyIndex),
			surface.get()) ? graphicsQueueFamilyIndex :
				queueFamilyProperties.size();
		if (presentQueueFamilyIndex == queueFamilyProperties.size()) {
			// the graphicsQueueFamilyIndex doesn't support present -> look for an other family index that supports both graphics and present
			for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
				if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
					device->physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), surface.get())) {
					graphicsQueueFamilyIndex = vk::su::checked_cast<uint32_t>(i);
					presentQueueFamilyIndex = i;
					break;
				}
			}
			if (presentQueueFamilyIndex == queueFamilyProperties.size()) {
				// there's nothing like a single family index that supports both graphics and present -> look for an other family index that supports present
				for (size_t i = 0; i < queueFamilyProperties.size(); i++) {
					if (device->physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(i), surface.get())) {
						presentQueueFamilyIndex = i;
						break;
					}
				}
			}
		}
		if ((graphicsQueueFamilyIndex == queueFamilyProperties.size())
			|| (presentQueueFamilyIndex == queueFamilyProperties.size())) {
			throw std::runtime_error("Could not find a queue for graphics or present -> terminating");
		}
		/*
		vk::Extent2D extent;
		vk::ImageUsageFlags usage;
		swapChain = new vk::su::SwapChainData(device->physicalDevice, device->device, surface,
			extent, vk::ImageUsageFlagBits::eTransferSrc,
			graphicsQueueFamilyIndex, presentQueueFamilyIndex);
		*/
		/*
		formats = device->physicalDevice.getSurfaceFormatsKHR(surface.get());
		assert(!formats.empty());
		vk::Format format = (formats[0].format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : formats[0].format;

		vk::SurfaceCapabilitiesKHR surfaceCapabilities = device->physicalDevice.getSurfaceCapabilitiesKHR(surface.get());
		swapData =
		*/
	};
};

enum class gs_type {
	gs_vertex_buffer,
	gs_index_buffer,
	gs_texture_2d,
	gs_zstencil_buffer,
	gs_stage_surface,
	gs_sampler_state,
	gs_vertex_shader,
	gs_pixel_shader,
	gs_duplicator,
	gs_swap_chain,
};

struct gs_texture {
	gs_texture_type type;
	uint32_t        levels;
	gs_color_format format;
	inline gs_texture(gs_texture_type type, uint32_t levels,
		gs_color_format format)
		: type(type),
		levels(levels),
		format(format)
	{
	};

	inline gs_texture(gs_device *device, gs_type obj_type,
		gs_texture_type type)
		: type(type)
	{
	};

	inline gs_texture(gs_device *device, gs_type obj_type,
		gs_texture_type type,
		uint32_t levels, gs_color_format format)
		: type(type),
		levels(levels),
		format(format)
	{
	};
};

struct gs_texture_2d : gs_texture {
	vk::su::TextureData *textureData = nullptr;
	uint32_t        width = 0, height = 0;
	uint32_t        flags = 0;

	inline gs_texture_2d()
		: gs_texture(GS_TEXTURE_2D, 0, GS_UNKNOWN)
	{
	};

	gs_texture_2d(gs_device_t *device, uint32_t width, uint32_t height,
		gs_color_format colorFormat, uint32_t levels,
		const uint8_t **data, uint32_t flags,
		gs_texture_type type, bool gdiCompatible,
		bool nv12 = false)
		: gs_texture(device, gs_type::gs_texture_2d, type, levels,
			colorFormat)
	{

	};
	/*
	gs_texture_2d(gs_device_t *device, ID3D11Texture2D *nv12,
		uint32_t flags)
	{

	};
	*/
	gs_texture_2d(gs_device_t *device, uint32_t handle)
		: gs_texture(device, gs_type::gs_texture_2d,
			GS_TEXTURE_2D)
	{

	};
};

struct gs_zstencil_buffer {
	gs_zstencil_buffer(gs_device_t *device, uint32_t width, uint32_t height,
		gs_zstencil_format format)
	{

	};
};

struct gs_stage_surface {
	uint32_t        width, height;
	gs_color_format format;
	gs_stage_surface(gs_device_t *device, uint32_t width, uint32_t height,
		gs_color_format colorFormat)
	{
	};
	gs_stage_surface(gs_device_t *device, uint32_t width, uint32_t height)
	{
	};
};

struct gs_sampler_state {
	gs_sampler_state(gs_device_t *device, const gs_sampler_info *info)
	{

	};
};

struct gs_shader_param {
	std::string                    name;
	gs_shader_param_type           type;
};
