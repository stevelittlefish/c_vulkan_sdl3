#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <cglm/cglm.h>

#include "io.h"

typedef struct {
    uint32_t graphics_family;
    uint32_t present_family;
	bool has_graphics_family;
	bool has_present_family;
} QueueFamilyIndices;

typedef struct {
    VkSurfaceCapabilitiesKHR capabilities;
	uint32_t formats_count;
	uint32_t present_modes_count;
	VkSurfaceFormatKHR* formats;
	VkPresentModeKHR* present_modes;
} SwapChainSupportDetails;

typedef struct {
	vec2 pos;
	vec3 color;
} Vertex;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 proj;
} UniformBufferObject;

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

#define MAX_FRAMES_IN_FLIGHT 2

// Validation layers can be enabled/disabled via CMake with -DENABLE_VALIDATION_LAYERS=ON/OFF
#ifdef ENABLE_VALIDATION_LAYERS
const bool enable_validation_layers = true;
#else
const bool enable_validation_layers = false;
#endif

#define NUM_VALIDATION_LAYERS 1
const char* validation_layers[NUM_VALIDATION_LAYERS] = {
	"VK_LAYER_KHRONOS_validation"
};

#define NUM_DEVICE_EXTENSIONS 1
const char* device_extensions[NUM_DEVICE_EXTENSIONS] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

SDL_Window* window = NULL;

VkInstance instance = {0};
VkDebugUtilsMessengerEXT debug_messenger = {0};
VkSurfaceKHR surface = {0};
// Phyiical device is the GPU
VkPhysicalDevice physical_device = VK_NULL_HANDLE;
// Logical device is the interface to the physical device
VkDevice device = {0};

VkQueue graphics_queue = {0};
VkQueue present_queue = {0};

VkSwapchainKHR swap_chain = {0};
uint32_t swap_chain_images_count = 0;
VkImage *swap_chain_images = NULL;
VkFormat swap_chain_image_format = VK_FORMAT_UNDEFINED;
VkExtent2D swap_chain_extent = {0};
uint32_t swap_chain_image_views_count = 0;
VkImageView *swap_chain_image_views = NULL;
uint32_t swap_chain_framebuffers_count = 0;
VkFramebuffer *swap_chain_framebuffers = NULL;

VkRenderPass render_pass = {0};
VkDescriptorSetLayout descriptor_set_layout = {0};
VkPipelineLayout pipeline_layout = {0};
VkPipeline graphics_pipeline = {0};

VkDescriptorPool descriptor_pool = {0};
VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT] = {0};

VkCommandPool command_pool = {0};
const uint32_t command_buffers_count = MAX_FRAMES_IN_FLIGHT;
VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT] = {0};

// Semaphores: need enough to avoid reuse before presentation completes
// Use max of MAX_FRAMES_IN_FLIGHT and swapchain image count
uint32_t semaphores_count = 0;
VkSemaphore* image_available_semaphores = NULL;
VkSemaphore* render_finished_semaphores = NULL;

// Per-frame fences
const uint32_t in_flight_fences_count = MAX_FRAMES_IN_FLIGHT;
VkFence in_flight_fences[MAX_FRAMES_IN_FLIGHT] = {0};

uint32_t current_frame = 0;
bool framebuffer_resized = false;

#define NUM_VERTICES 4
Vertex vertices[NUM_VERTICES] = {
    {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}
};

#define NUM_VERTEX_INDICES 6
uint16_t vertex_indices[NUM_VERTEX_INDICES] = {
	0, 1, 2, 2, 3, 0
};

VkBuffer vertex_buffer = {0};
VkDeviceMemory vertex_buffer_memory = {0};

VkBuffer index_buffer = {0};
VkDeviceMemory index_buffer_memory = {0};

VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT] = {0};
VkDeviceMemory uniform_buffers_memory[MAX_FRAMES_IN_FLIGHT] = {0};
void* uniform_buffers_mapped[MAX_FRAMES_IN_FLIGHT] = {0};

bool check_validation_layer_support() {
	uint32_t layer_count;
	vkEnumerateInstanceLayerProperties(&layer_count, NULL);
	
	VkLayerProperties* available_layers = malloc(sizeof(VkLayerProperties) * layer_count);
	vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

	for (int i = 0; i < NUM_VALIDATION_LAYERS; i++) {
		bool layer_found = false;
		
		for (int j = 0; j < layer_count; j++) {
			if (strcmp(validation_layers[i], available_layers[j].layerName) == 0) {
				layer_found = true;
				break;
			}
		}

		if (!layer_found) {
			return false;
		}
	}

	return true;
}

char const * const * get_required_extensions(uint32_t* count) {
	/*
	 * If validation layers are enabled, we need to request the VK_EXT_DEBUG_UTILS_EXTENSION_NAME extension
	 * as well as the extensions required by GLFW.
	 *
	 * Note: calling this repeatedly could cause a memory leak as we create a new array each time
	 * if validation layers are enabled.
	 */
	assert(count != NULL);
	
	// Get the required extensions from GLFW and set count to the number of extensions
	char const * const * sdl_extensions = SDL_Vulkan_GetInstanceExtensions(count);

	if (sdl_extensions == NULL) {
		fprintf(stderr, "Failed to get required extensions from GLFW\n");
		exit(1);
	}

	if (!enable_validation_layers) {
		return sdl_extensions;
	}

	// If validation layers are enabled, add the debug utils extension
	const char** extensions = malloc(sizeof(const char*) * (*count + 1));
	memcpy(extensions, sdl_extensions, sizeof(const char*) * *count);
	extensions[*count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	*count += 1;
	
	return (char const * const *) extensions;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
	fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);

	return VK_FALSE;
}

void populate_debug_messenger_create_info(VkDebugUtilsMessengerCreateInfoEXT* create_info) {
	*create_info = (VkDebugUtilsMessengerCreateInfoEXT) {0};
	create_info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	create_info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	create_info->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	create_info->pfnUserCallback = debug_callback;
}

QueueFamilyIndices find_queue_families(VkPhysicalDevice device) {
	QueueFamilyIndices indices = {0};

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

	VkQueueFamilyProperties* queue_families = malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

	for (int i = 0; i < queue_family_count; i++) {
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics_family = i;
			indices.has_graphics_family = true;
		}

		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);

		if (present_support) {
			indices.present_family = i;
			indices.has_present_family = true;
		}

		if (indices.has_graphics_family && indices.has_present_family) {
			break;
		}
	}

	free(queue_families);

	return indices;
}

SwapChainSupportDetails query_swap_chain_support(VkPhysicalDevice device) {
	SwapChainSupportDetails details = {0};

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.formats_count, NULL);
	details.formats = malloc(sizeof(VkSurfaceFormatKHR) * details.formats_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details.formats_count, details.formats);

	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_modes_count, NULL);
	details.present_modes = malloc(sizeof(VkPresentModeKHR) * details.present_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &details.present_modes_count, details.present_modes);
	
	return details;
}

void free_swap_chain_support(SwapChainSupportDetails* details) {
	free(details->formats);
	free(details->present_modes);
}

VkPhysicalDevice pick_physical_device(void) {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(instance, &device_count, NULL);

	if (device_count == 0) {
		fprintf(stderr, "failed to find GPUs with Vulkan support!\n");
		exit(1);
	}

	VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * device_count);
	vkEnumeratePhysicalDevices(instance, &device_count, devices);
	
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;

	for (int i = 0; i < device_count; i++) {
		printf(" Physical Device %d\n", i);
        QueueFamilyIndices indices = find_queue_families(devices[i]);

		printf("  Graphics Family: %d\n", indices.graphics_family);
		printf("  Present Family: %d\n", indices.present_family);
		
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, NULL);
		
		VkExtensionProperties* available_extensions = malloc(sizeof(VkExtensionProperties) * extension_count);
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, available_extensions);
		
		// Check all of the required extensions are supported
		bool required_extensions_supported = true;

		for (int j = 0; j < NUM_DEVICE_EXTENSIONS; j++) {
			bool extension_found = false;
			for (int k = 0; k < extension_count; k++) {
				if (strcmp(device_extensions[j], available_extensions[k].extensionName) == 0) {
					extension_found = true;
					break;
				}
			}

			if (!extension_found) {
				required_extensions_supported = false;
				printf("Extension %s not supported\n", device_extensions[j]);
				break;
			}
		}

		free(available_extensions);

		if (!required_extensions_supported) {
			continue;
		}
		
		SwapChainSupportDetails swap_chain_support = query_swap_chain_support(devices[i]);
		bool swap_chain_adequate = swap_chain_support.formats_count > 0 && swap_chain_support.present_modes_count > 0;
		free_swap_chain_support(&swap_chain_support);

        if (indices.has_graphics_family
				&& indices.has_present_family
				&& swap_chain_adequate) {
			printf(" Device %d is suitable\n", i);
			physical_device = devices[i];
			break;
		}
	}

	free(devices);
	return physical_device;
}

VkExtent2D choose_swap_extent(VkSurfaceCapabilitiesKHR *capabilities) {
	if (capabilities->currentExtent.width != 0xFFFFFFFF) {
		return capabilities->currentExtent;
	} else {
		int width, height;
		SDL_GetWindowSize(window, &width, &height);

		VkExtent2D actualExtent = {
			width,
			height
		};
		
		// Clamp the width and height to the min and max extents
		if (actualExtent.width < capabilities->minImageExtent.width) {
			actualExtent.width = capabilities->minImageExtent.width;
		}
		else if (actualExtent.width > capabilities->maxImageExtent.width) {
			actualExtent.width = capabilities->maxImageExtent.width;
		}

		if (actualExtent.height < capabilities->minImageExtent.height) {
			actualExtent.height = capabilities->minImageExtent.height;
		}
		else if (actualExtent.height > capabilities->maxImageExtent.height) {
			actualExtent.height = capabilities->maxImageExtent.height;
		}

		return actualExtent;
	}
}

void create_image_views(void) {
	swap_chain_image_views_count = swap_chain_images_count;
	swap_chain_image_views = malloc(sizeof(VkImageView) * swap_chain_image_views_count);

	for (size_t i = 0; i < swap_chain_image_views_count; i++) {
		VkImageViewCreateInfo create_info = {0};
		create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		create_info.image = swap_chain_images[i];
		create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		create_info.format = swap_chain_image_format;
		create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		create_info.subresourceRange.baseMipLevel = 0;
		create_info.subresourceRange.levelCount = 1;
		create_info.subresourceRange.baseArrayLayer = 0;
		create_info.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &create_info, NULL, &swap_chain_image_views[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create image views!");
			exit(1);
		}
	}

	printf(" Image views created\n");
}

void cleanup_image_views(void) {
	for (size_t i = 0; i < swap_chain_image_views_count; i++) {
		vkDestroyImageView(device, swap_chain_image_views[i], NULL);
	}

	free(swap_chain_image_views);
	swap_chain_image_views = NULL;
	swap_chain_image_views_count = 0;
}

void create_framebuffers() {
	swap_chain_framebuffers_count = swap_chain_image_views_count;
	swap_chain_framebuffers = malloc(sizeof(VkFramebuffer) * swap_chain_framebuffers_count);

	for (size_t i = 0; i < swap_chain_image_views_count; i++) {
		VkImageView attachments[] = {
			swap_chain_image_views[i]
		};

		VkFramebufferCreateInfo framebuffer_info = {0};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = attachments;
		framebuffer_info.width = swap_chain_extent.width;
		framebuffer_info.height = swap_chain_extent.height;
		framebuffer_info.layers = 1;

		if (vkCreateFramebuffer(device, &framebuffer_info, NULL, &swap_chain_framebuffers[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create framebuffer!");
			exit(1);
		}
	}

	printf(" Created %d framebuffers\n", swap_chain_framebuffers_count);
}

void cleanup_framebuffers() {
	for (size_t i = 0; i < swap_chain_framebuffers_count; i++) {
		vkDestroyFramebuffer(device, swap_chain_framebuffers[i], NULL);
	}

	free(swap_chain_framebuffers);
	swap_chain_framebuffers = NULL;
	swap_chain_framebuffers_count = 0;
}

void create_swap_chain() {
	SwapChainSupportDetails swap_chain_support = query_swap_chain_support(physical_device);

	if (swap_chain_support.formats_count == 0) {
		fprintf(stderr, "Swap chain support not available (no formats)\n");
		exit(1);
	}
	else if (swap_chain_support.present_modes_count == 0) {
		fprintf(stderr, "Swap chain support not available (no present modes)\n");
		exit(1);
	}

	printf(" Swap chain support: %d formats, %d present modes\n", swap_chain_support.formats_count, swap_chain_support.present_modes_count);
	
	// Choose the best surface format from the available formats
	VkSurfaceFormatKHR surface_format = swap_chain_support.formats[0];
	for (int i = 0; i < swap_chain_support.formats_count; i++) {
		if (swap_chain_support.formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && swap_chain_support.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surface_format = swap_chain_support.formats[i];
			break;
		}
	}
	
	// Choose the best present mode from the available present modes
	// VkPresentModeKHR presentMode = chooseSwapPresentMode(swap_chain_support.presentModes);
	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	for (int i = 0; i < swap_chain_support.present_modes_count; i++) {
		if (swap_chain_support.present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			present_mode = swap_chain_support.present_modes[i];
			break;
		}
	}

	swap_chain_extent = choose_swap_extent(&swap_chain_support.capabilities);
	printf(" Swap chain extent: %d x %d\n", swap_chain_extent.width, swap_chain_extent.height);

	uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
	if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
		image_count = swap_chain_support.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info.surface = surface;

	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = swap_chain_extent;
	create_info.imageArrayLayers = 1;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = find_queue_families(physical_device);
	uint32_t queueFamilyIndices[] = {indices.graphics_family, indices.present_family};

	if (indices.graphics_family != indices.present_family) {
		create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		create_info.queueFamilyIndexCount = 2;
		create_info.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	create_info.preTransform = swap_chain_support.capabilities.currentTransform;
	create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info.presentMode = present_mode;
	create_info.clipped = VK_TRUE;

	if (vkCreateSwapchainKHR(device, &create_info, NULL, &swap_chain) != VK_SUCCESS) {
		fprintf(stderr, "failed to create swap chain!");
		exit(1);
	}
	
	vkGetSwapchainImagesKHR(device, swap_chain, &swap_chain_images_count, NULL);
	swap_chain_images = malloc(sizeof(VkImage) * swap_chain_images_count);
	vkGetSwapchainImagesKHR(device, swap_chain, &swap_chain_images_count, swap_chain_images);

	swap_chain_image_format = surface_format.format;

	free_swap_chain_support(&swap_chain_support);

	printf(" Swap chain created\n");
	
	// ----- Create semaphores -----
	// We need enough semaphores to avoid reuse while images are in flight
	// Use the maximum of MAX_FRAMES_IN_FLIGHT and swapchain image count
	semaphores_count = swap_chain_images_count > MAX_FRAMES_IN_FLIGHT ? swap_chain_images_count : MAX_FRAMES_IN_FLIGHT;
	
	image_available_semaphores = malloc(sizeof(VkSemaphore) * semaphores_count);
	render_finished_semaphores = malloc(sizeof(VkSemaphore) * semaphores_count);
	
	VkSemaphoreCreateInfo semaphore_info = {0};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	
	for (size_t i = 0; i < semaphores_count; i++) {
		if (vkCreateSemaphore(device, &semaphore_info, NULL, &image_available_semaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphore_info, NULL, &render_finished_semaphores[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create semaphores %zu!\n", i);
			exit(1);
		}
	}
	
	printf(" Created %d semaphore pairs\n", semaphores_count);
}

void cleanup_swap_chain(void) {
	printf("Cleaning up swap chain\n");
	
	// Destroy semaphores
	for (size_t i = 0; i < semaphores_count; i++) {
		vkDestroySemaphore(device, image_available_semaphores[i], NULL);
		vkDestroySemaphore(device, render_finished_semaphores[i], NULL);
	}
	
	free(image_available_semaphores);
	free(render_finished_semaphores);
	image_available_semaphores = NULL;
	render_finished_semaphores = NULL;
	semaphores_count = 0;
	
	cleanup_framebuffers();
	cleanup_image_views();
	vkDestroySwapchainKHR(device, swap_chain, NULL);
}

void recreate_swap_chain() {
	int width = 0, height = 0;
	SDL_GetWindowSize(window, &width, &height);

	vkDeviceWaitIdle(device);

	cleanup_swap_chain();

	create_swap_chain();
	create_image_views();
	create_framebuffers();
}

VkShaderModule create_shader_module(const char* code, size_t code_size) {
	VkShaderModuleCreateInfo create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = code_size;
	create_info.pCode = (const uint32_t*)code;

	VkShaderModule shader_module;
	if (vkCreateShaderModule(device, &create_info, NULL, &shader_module) != VK_SUCCESS) {
		fprintf(stderr, "failed to create shader module!");
		exit(1);
	}

	return shader_module;
}

VkVertexInputBindingDescription get_binding_description() {
	VkVertexInputBindingDescription binding_description = {0};
	binding_description.binding = 0;
	binding_description.stride = sizeof(Vertex);
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	return binding_description;
}

VkVertexInputAttributeDescription* get_attribute_descriptions(size_t* count) {
	VkVertexInputAttributeDescription* attribute_descriptions = malloc(sizeof(VkVertexInputAttributeDescription) * 2);
	
	attribute_descriptions[0] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[0].binding = 0;
	attribute_descriptions[0].location = 0;
	attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[0].offset = offsetof(Vertex, pos);

	attribute_descriptions[1] = (VkVertexInputAttributeDescription) {0};
	attribute_descriptions[1].binding = 0;
	attribute_descriptions[1].location = 1;
	attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attribute_descriptions[1].offset = offsetof(Vertex, color);

	*count = 2;
	return attribute_descriptions;
}


void create_graphics_pipeline(void) {
	size_t vert_shader_code_size;
	char* vert_shader_code = read_entire_binary_file("shaders/shader.vert.spv", &vert_shader_code_size);
	printf(" Read %d bytes\n", vert_shader_code_size);

	VkShaderModule vert_shader_module = create_shader_module(vert_shader_code, vert_shader_code_size);
	free(vert_shader_code);

	size_t frag_shader_code_size;
	char* frag_shader_code = read_entire_binary_file("shaders/shader.frag.spv", &frag_shader_code_size);
	printf(" Read %d bytes\n", frag_shader_code_size);

	VkShaderModule frag_shader_module = create_shader_module(frag_shader_code, frag_shader_code_size);
	free(frag_shader_code);

	VkPipelineShaderStageCreateInfo vert_shader_stage_info = {0};
	vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vert_shader_stage_info.module = vert_shader_module;
	vert_shader_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo frag_shader_stage_info = {0};
	frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_shader_stage_info.module = frag_shader_module;
	frag_shader_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};
	
	// Get the vertex format description
	VkVertexInputBindingDescription binding_description = get_binding_description();
	size_t attribute_descriptions_count;
	VkVertexInputAttributeDescription* attribute_descriptions = get_attribute_descriptions(&attribute_descriptions_count);
	
	VkPipelineVertexInputStateCreateInfo vertex_input_info = {0};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 1;
	vertex_input_info.vertexAttributeDescriptionCount = attribute_descriptions_count;
	vertex_input_info.pVertexBindingDescriptions = &binding_description;
	vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewport_state = {0};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {0};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {0};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend_attachment = {0};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend_attachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo color_blending = {0};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	color_blending.logicOp = VK_LOGIC_OP_COPY;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &color_blend_attachment;
	color_blending.blendConstants[0] = 0.0f;
	color_blending.blendConstants[1] = 0.0f;
	color_blending.blendConstants[2] = 0.0f;
	color_blending.blendConstants[3] = 0.0f;

	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dynamic_state = {0};
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic_states;

	VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
	pipeline_layout_info.pushConstantRangeCount = 0;

	if (vkCreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout) != VK_SUCCESS) {
		fprintf(stderr, "failed to create pipeline layout!");
		exit(1);
	}

	VkGraphicsPipelineCreateInfo pipeline_info = {0};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = pipeline_layout;
	pipeline_info.renderPass = render_pass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &graphics_pipeline) != VK_SUCCESS) {
		fprintf(stderr, "failed to create graphics pipeline!");
		exit(1);
	}

	vkDestroyShaderModule(device, frag_shader_module, NULL);
	vkDestroyShaderModule(device, vert_shader_module, NULL);


	free(attribute_descriptions);

	printf(" Graphics pipeline created\n");
}

uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
	/*
	 * See https://vulkan-tutorial.com/Vertex_buffers/Staging_buffer#page_Memory_types
	 * for an explanation of how this works
	 */
	VkPhysicalDeviceMemoryProperties mem_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
		if ((type_filter & (1 << i))
				&& (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	fprintf(stderr, "failed to find suitable memory type!");
	exit(1);
}

void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *buffer_memory) {
	VkBufferCreateInfo buffer_info = {0};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(device, &buffer_info, NULL, buffer) != VK_SUCCESS) {
		fprintf(stderr, "Failed to create buffer");
		exit(1);
	}

	VkMemoryRequirements mem_requirements = {0};
	vkGetBufferMemoryRequirements(device, *buffer, &mem_requirements);

	VkMemoryAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_requirements.size;
	alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &alloc_info, NULL, buffer_memory) != VK_SUCCESS) {
		fprintf(stderr, "Failed to allocate buffer memory");
		exit(1);
	}

	vkBindBufferMemory(device, *buffer, *buffer_memory, 0);
}

void copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) {
	/*
	 * Copy buffers - used to copy from staging buffer into vertext buffer
	 */
	VkCommandBufferAllocateInfo alloc_info = {0};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool = command_pool;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	vkAllocateCommandBuffers(device, &alloc_info, &command_buffer);

	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(command_buffer, &begin_info);

	VkBufferCopy copy_region = {0};
	copy_region.size = size;
	vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);

	vkEndCommandBuffer(command_buffer);

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphics_queue);

	vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

void init_vulkan(void) {
	printf("Initialising Vulkan\n");

	if (enable_validation_layers && !check_validation_layer_support()) {
		fprintf(stderr, "validation layers requested, but not available!");
		exit(1);
	}
	
	VkApplicationInfo app_info = {0};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Hello Triangle";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_0;

	VkInstanceCreateInfo instance_create_info = {0};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.pApplicationInfo = &app_info;

	instance_create_info.ppEnabledExtensionNames = get_required_extensions(&instance_create_info.enabledExtensionCount);

	for (int i = 0; i < instance_create_info.enabledExtensionCount; i++) {
		printf(" Extension: %s\n", instance_create_info.ppEnabledExtensionNames[i]);
	}

	VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {0};
	if (enable_validation_layers) {
		instance_create_info.enabledLayerCount = NUM_VALIDATION_LAYERS;
		instance_create_info.ppEnabledLayerNames = validation_layers;

		populate_debug_messenger_create_info(&debug_create_info);
		instance_create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debug_create_info;
	} else {
		instance_create_info.enabledLayerCount = 0;
		instance_create_info.pNext = NULL;
	}
	
	// ----- Create the Vulkan instance -----
	if (vkCreateInstance(&instance_create_info, NULL, &instance) != VK_SUCCESS) {
		fprintf(stderr, "failed to create instance!");
		exit(1);
	}

	// ----- Create the debug messenger -----
	if (enable_validation_layers) {
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info;
		populate_debug_messenger_create_info(&debug_messenger_create_info);
		
		// Create the debug messenger
		PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func == NULL) {
			fprintf(stderr, "failed to get vkCreateDebugUtilsMessengerEXT function pointer\n");
			exit(VK_ERROR_EXTENSION_NOT_PRESENT);
		}
	}

	// ----- Create the window surface -----
	if (!SDL_Vulkan_CreateSurface(window, instance, NULL, &surface)) {
		fprintf(stderr, "failed to create window surface!");
		exit(1);
	}
	
	// ----- Pick a physical device -----
	
	// Next find a phyiscal device (i.e. a GPU) that supports the required features
	physical_device = pick_physical_device();

	if (physical_device == VK_NULL_HANDLE) {
		fprintf(stderr, "failed to find a suitable GPU!\n");
		exit(1);
	}
	
	// ----- Create the logical device -----

	// Next we need to create a logical device to interface with the physical device
	// (and also the graphics and presentation queues)
	
	QueueFamilyIndices physical_indices = find_queue_families(physical_device);
	
	// I don't fully understand why, but sometimes it looks like both families could be the same
	uint32_t unique_queue_families[2] = {physical_indices.graphics_family, physical_indices.present_family};
	uint32_t num_unique_queue_families = unique_queue_families[0] == unique_queue_families[1] ? 1 : 2;
	VkDeviceQueueCreateInfo* queue_create_infos = malloc(sizeof(VkDeviceQueueCreateInfo) * num_unique_queue_families);

	float queue_priority = 1.0f;
	for (uint32_t i = 0; i < num_unique_queue_families; i++) {
		VkDeviceQueueCreateInfo queue_create_info = {0};
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = unique_queue_families[i];
		queue_create_info.queueCount = 1;
		queue_create_info.pQueuePriorities = &queue_priority;

		queue_create_infos[i] = queue_create_info;
	}

	VkPhysicalDeviceFeatures device_features = {0};

	VkDeviceCreateInfo create_info = {0};
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

	create_info.queueCreateInfoCount = num_unique_queue_families;
	create_info.pQueueCreateInfos = queue_create_infos;

	create_info.pEnabledFeatures = &device_features;

	create_info.enabledExtensionCount = NUM_DEVICE_EXTENSIONS;
	create_info.ppEnabledExtensionNames = device_extensions;

	if (enable_validation_layers) {
		create_info.enabledLayerCount = NUM_VALIDATION_LAYERS;
		create_info.ppEnabledLayerNames = validation_layers;
	} else {
		create_info.enabledLayerCount = 0;
	}

	if (vkCreateDevice(physical_device, &create_info, NULL, &device) != VK_SUCCESS) {
		fprintf(stderr, "failed to create logical device!\n");
		exit(1);
	}

	free(queue_create_infos);

	vkGetDeviceQueue(device, physical_indices.graphics_family, 0, &graphics_queue);
	vkGetDeviceQueue(device, physical_indices.present_family, 0, &present_queue);
	
	// ----- Create the swap chain -----
	create_swap_chain();
	
	// ----- Create the image views -----
	create_image_views();
	
	// ----- Create the render pass -----
	VkAttachmentDescription color_attachment = {0};
	color_attachment.format = swap_chain_image_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {0};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {0};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency = {0};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {0};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	if (vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) != VK_SUCCESS) {
		fprintf(stderr, "failed to create render pass!\n");
		exit(1);
	}

	printf(" Render pass created\n");

	// ----- Set up uniform buffer layout -----
	// create_descriptor_set_layout()
	VkDescriptorSetLayoutBinding ubo_layout_binding = {0};
	ubo_layout_binding.binding = 0;
	ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	ubo_layout_binding.descriptorCount = 1;
	ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	ubo_layout_binding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutCreateInfo layout_info = {0};
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = 1;
	layout_info.pBindings = &ubo_layout_binding;

	if (vkCreateDescriptorSetLayout(device, &layout_info, NULL, &descriptor_set_layout) != VK_SUCCESS) {
		fprintf(stderr, "failed to create descriptor set layout!\n");
		exit(1);
	}
	
	// ----- Create the graphics pipeline -----
	create_graphics_pipeline();
	
	// ----- Create the framebuffers -----
	create_framebuffers();
	
	// ----- Create the command pool -----
	VkCommandPoolCreateInfo command_pool_info = {0};
	command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = physical_indices.graphics_family;

	if (vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool) != VK_SUCCESS) {
		fprintf(stderr, "failed to create command pool!\n");
		exit(1);
	}

	// ----- Create the vertex buffer -----
	
	VkDeviceSize buffer_size = sizeof(vertices[0]) * NUM_VERTICES;
	
	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;
	create_buffer(
			buffer_size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_buffer,
			&staging_buffer_memory);

	void *data;
	vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
	memcpy(data, vertices, (size_t) buffer_size);
	vkUnmapMemory(device, staging_buffer_memory);

	create_buffer(
			buffer_size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		   	VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertex_buffer,
			&vertex_buffer_memory);

	// Copy the staging buffer into the vertex buffer
	copy_buffer(staging_buffer, vertex_buffer, buffer_size);

	vkDestroyBuffer(device, staging_buffer, NULL);
	vkFreeMemory(device, staging_buffer_memory, NULL);

	// ----- Create the index buffer -----
	
	VkDeviceSize index_buffer_size = sizeof(vertex_indices[0]) * NUM_VERTEX_INDICES;

	VkBuffer staging_index_buffer;
	VkDeviceMemory staging_index_buffer_memory;
	create_buffer(
			index_buffer_size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&staging_index_buffer,
			&staging_index_buffer_memory);

	void *index_data;
	vkMapMemory(device, staging_index_buffer_memory, 0, index_buffer_size, 0, &index_data);
	memcpy(index_data, vertex_indices, (size_t) index_buffer_size);
	vkUnmapMemory(device, staging_index_buffer_memory);

	create_buffer(
			index_buffer_size,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		   	VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&index_buffer,
			&index_buffer_memory);

	// Copy the staging buffer into the index buffer
	copy_buffer(staging_index_buffer, index_buffer, index_buffer_size);

	vkDestroyBuffer(device, staging_index_buffer, NULL);
	vkFreeMemory(device, staging_index_buffer_memory, NULL);

	// Map the buffer memory and copy the vertex data into it
	
	// ----- Create the uniform buffer -----
	VkDeviceSize uniform_buffer_size = sizeof(UniformBufferObject);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		create_buffer(
				uniform_buffer_size,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				&uniform_buffers[i],
				&uniform_buffers_memory[i]);

		// Map the buffer memory and copy the vertex data into it
		vkMapMemory(device, uniform_buffers_memory[i], 0, uniform_buffer_size, 0, &uniform_buffers_mapped[i]);
	}

	// ----- Create the descriptor pool -----
	VkDescriptorPoolSize desc_pool_size = {0};
	desc_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	desc_pool_size.descriptorCount = MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo desc_pool_info = {0};
	desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	desc_pool_info.poolSizeCount = 1;
	desc_pool_info.pPoolSizes = &desc_pool_size;
	desc_pool_info.maxSets = MAX_FRAMES_IN_FLIGHT;

	if (vkCreateDescriptorPool(device, &desc_pool_info, NULL, &descriptor_pool) != VK_SUCCESS) {
		fprintf(stderr, "failed to create descriptor pool!\n");
		exit(1);
	}

	// ----- Create the descriptor sets -----
	VkDescriptorSetLayout ds_layouts[MAX_FRAMES_IN_FLIGHT] = {0};
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		ds_layouts[i] = descriptor_set_layout;
	}

	VkDescriptorSetAllocateInfo ds_alloc_info = {0};
	ds_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ds_alloc_info.descriptorPool = descriptor_pool;
	ds_alloc_info.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
	ds_alloc_info.pSetLayouts = ds_layouts;

	if (vkAllocateDescriptorSets(device, &ds_alloc_info, descriptor_sets) != VK_SUCCESS) {
		fprintf(stderr, "failed to allocate descriptor sets!\n");
		exit(1);
	}

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo buffer_info = {0};
		buffer_info.buffer = uniform_buffers[i];
		buffer_info.offset = 0;
		buffer_info.range = sizeof(UniformBufferObject);

		VkWriteDescriptorSet descriptor_write = {0};
		descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_write.dstSet = descriptor_sets[i];
		descriptor_write.dstBinding = 0;
		descriptor_write.dstArrayElement = 0;
		descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptor_write.descriptorCount = 1;
		descriptor_write.pBufferInfo = &buffer_info;
		descriptor_write.pImageInfo = NULL;
		descriptor_write.pTexelBufferView = NULL;

		vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, NULL);
	}

	// ----- Create the command buffers -----
	VkCommandBufferAllocateInfo buf_alloc_info = {0};
	buf_alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	buf_alloc_info.commandPool = command_pool;
	buf_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	buf_alloc_info.commandBufferCount = command_buffers_count;

	if (vkAllocateCommandBuffers(device, &buf_alloc_info, command_buffers) != VK_SUCCESS) {
		fprintf(stderr, "failed to allocate command buffers!\n");
		exit(1);
	}

	// ----- Create the fences (per-frame synchronization) -----
	VkFenceCreateInfo fence_info = {0};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		if (vkCreateFence(device, &fence_info, NULL, &in_flight_fences[i]) != VK_SUCCESS) {
			fprintf(stderr, "failed to create fence for frame %zu!\n", i);
			exit(1);
		}
	}

	printf("Initiialisation complete\n");
}

void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index) {
	VkCommandBufferBeginInfo begin_info = {0};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
		fprintf(stderr, "failed to begin recording command buffer!\n");
		exit(1);
	}

	VkRenderPassBeginInfo render_pass_info = {0};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_info.renderPass = render_pass;
	render_pass_info.framebuffer = swap_chain_framebuffers[image_index];
	render_pass_info.renderArea.offset.x = 0;
	render_pass_info.renderArea.offset.y = 0;
	render_pass_info.renderArea.extent = swap_chain_extent;

	VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	render_pass_info.clearValueCount = 1;
	render_pass_info.pClearValues = &clear_color;

	vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	
	// ------------- Render Pass ------------- //
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

	VkBuffer vertex_buffers[] = {vertex_buffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

	vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);
	
	VkViewport viewport = {0};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float) swap_chain_extent.width;
	viewport.height = (float) swap_chain_extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	VkRect2D scissor = {0};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = swap_chain_extent;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	
	// Bind the descriptor set to update the uniform buffer
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_sets[current_frame], 0, NULL);
	
	// Draw the triangles
	vkCmdDrawIndexed(command_buffer, NUM_VERTEX_INDICES, 1, 0, 0, 0);

	// ------------- /Render Pass ------------- //
	vkCmdEndRenderPass(command_buffer);

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		fprintf(stderr, "failed to record command buffer!\n");
		exit(1);
	}
}

void draw_frame() {
	vkWaitForFences(device, 1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	// Use per-frame semaphore (cycling with current_frame) for acquire
	// We can't use image_index here because it hasn't been set yet!
	VkResult result = vkAcquireNextImageKHR(device, swap_chain, UINT64_MAX, image_available_semaphores[current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		printf("Couldn't acquire swap chain image - recreating swap chain\n");
		recreate_swap_chain();
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		fprintf(stderr, "Failed to acquire swap chain image\n");
		exit(1);
	}

	vkResetFences(device, 1, &in_flight_fences[current_frame]);
	
	vkResetCommandBuffer(command_buffers[current_frame], /*VkCommandBufferResetFlagBits*/ 0);
	
	// Write our draw commands into the command buffer
	record_command_buffer(command_buffers[current_frame], image_index);

	// Update the uniform buffer
	// TODO: Temporary code to apply some transformations over time
	uint64_t ticks = SDL_GetTicksNS();
	double t = SDL_NS_TO_SECONDS((double) ticks);

	UniformBufferObject ubo = {0};

	// Model matrix
	glm_mat4_identity(ubo.model);
	vec3 axis = {0.0f, 0.0f, 1.0f};
	glm_rotate(ubo.model, (float) t, axis);
	
	// View matrix
	// glm_mat4_identity(ubo.view);
	vec3 eye = {2.0f, 2.0f, 2.0f};
	vec3 center = {0.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 0.0f, 1.0f};
	glm_lookat(eye, center, up, ubo.view);
	
	// Projection matrix
	glm_perspective(glm_rad(45.0f), swap_chain_extent.width / (float) swap_chain_extent.height, 0.1f, 10.0f, ubo.proj);
	// glm_mat4_identity(ubo.proj);

	// Flip the Y axis
	ubo.proj[1][1] *= -1;

	memcpy(uniform_buffers_mapped[current_frame], &ubo, sizeof(ubo));

	VkSubmitInfo submit_info = {0};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	
	// Wait on the per-frame semaphore that was signaled by vkAcquireNextImageKHR
	VkSemaphore wait_semaphores[] = {image_available_semaphores[current_frame]};
	VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_stages;

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffers[current_frame];

	// Signal the per-swapchain-image semaphore for this specific image
	VkSemaphore signal_semaphores[] = {render_finished_semaphores[image_index]};
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = signal_semaphores;

	if (vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame]) != VK_SUCCESS) {
		fprintf(stderr, "failed to submit draw command buffer!");
		exit(1);
	}

	VkPresentInfoKHR present_info = {0};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;

	VkSwapchainKHR swap_chains[] = {swap_chain};
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swap_chains;

	present_info.pImageIndices = &image_index;

	result = vkQueuePresentKHR(present_queue, &present_info);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized) {
		printf("Couldn't present swap chain image - recreating swap chain\n");
		framebuffer_resized = false;
		recreate_swap_chain();

	} else if (result != VK_SUCCESS) {
		fprintf(stderr, "failed to present swap chain image!\n");
		exit(1);
	}

	current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void cleanup_vulkan(void) {
	printf("Cleaning up Vulkan\n");

	// Cleanup swap chain (includes per-swapchain-image semaphores)
	cleanup_swap_chain();

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyBuffer(device, uniform_buffers[i], NULL);
		vkFreeMemory(device, uniform_buffers_memory[i], NULL);
	}
	
	vkDestroyDescriptorPool(device, descriptor_pool, NULL);

	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);

	vkDestroyBuffer(device, vertex_buffer, NULL);
	vkFreeMemory(device, vertex_buffer_memory, NULL);

	vkDestroyBuffer(device, index_buffer, NULL);
	vkFreeMemory(device, index_buffer_memory, NULL);

	vkDestroyPipeline(device, graphics_pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);

	vkDestroyRenderPass(device, render_pass, NULL);

	// Destroy per-frame fences
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyFence(device, in_flight_fences[i], NULL);
	}

	vkDestroyCommandPool(device, command_pool, NULL);

	vkDestroyDevice(device, NULL);

	if (enable_validation_layers) {
		PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != NULL) {
			func(instance, debug_messenger, NULL);
		}
	}

	vkDestroySurfaceKHR(instance, surface, NULL);
	vkDestroyInstance(instance, NULL);
}

int main(void) {
	printf("Hello, Vulkan!\n");

	// Initialise SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

	// Create the window
	SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
	window = SDL_CreateWindow("Vulkan", WIDTH, HEIGHT, window_flags);
    if (!window) {
		printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	printf("SDL window created\n");

	// Initialise Vulkan
	init_vulkan();
	
	// Make the window visible
	SDL_ShowWindow(window);

	// ----- Main loop -----

    bool running = true;
    SDL_Event event;
    while (running) {
        // Poll for events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
			else if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_Q) {
					printf("Quitting...\n");
					running = false;
				}
			}
        }

		draw_frame();
    }

	vkDeviceWaitIdle(device);
	
	cleanup_vulkan();

	// Cleanup SDL
	printf("Cleaning up SDL\n");
    SDL_DestroyWindow(window);
    SDL_Quit();
	
	printf("Goodbye Vulkan!\n");
	printf("Built: %s %s\n", __DATE__, __TIME__);
}
