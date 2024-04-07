#include <pshine/game.h>
#include <pshine/util.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include "vk_util.h"
#include "math.h"

struct vulkan_renderer;
enum queue_family {
	QUEUE_GRAPHICS,
	QUEUE_PRESENT,
	QUEUE_FAMILY_COUNT
};

#define FRAMES_IN_FLIGHT 2

struct global_uniform_data {
	float4x4 view;
	float4x4 proj;
	float4 sun;
	float4 camera;
};

struct atmo_uniform_data {
	float4 planet; // xyz, w=radius
	float radius;
	float density_falloff;
	unsigned int optical_depth_samples;
	unsigned int scatter_point_samples;
	float blend_factor;
	float _pad[3];
	float3 wavelengths;
};

struct material_uniform_data {
	float4 color;
};

struct static_mesh_uniform_data {
	float4x4 mvp;
};

struct planet_pushconst_data {
	float unused0[4];
};

struct vulkan_image {
	VkImage image;
	VkImageView view;
	VmaAllocation allocation;
};

struct vulkan_buffer {
	VkBuffer buffer;
	VmaAllocation allocation;
};

struct vulkan_mesh {
	struct vulkan_buffer vertex_buffer, index_buffer;
	uint32_t index_count, vertex_count;
	enum pshine_vertex_type vertex_type;
};

struct pshine_planet_graphics_data {
	struct vulkan_mesh *mesh_ref;
	struct vulkan_buffer uniform_buffer;
	VkDescriptorSet descriptor_set;
};

struct per_frame_data {
	struct {
		VkSemaphore image_avail_semaphore;
		VkSemaphore render_finish_semaphore;
		VkFence in_flight_fence;
	} sync;
	struct vulkan_buffer global_uniform_buffer;
	struct vulkan_buffer material_uniform_buffer;
	struct vulkan_buffer atmo_uniform_buffer;
	VkDescriptorSet global_descriptor_set;
	VkDescriptorSet material_descriptor_set; // TODO: move to material struct
	VkDescriptorSet atmo_descriptor_set;
	VkCommandBuffer command_buffer;
};

struct render_pass_transients {
	struct vulkan_image color_0;
	struct vulkan_image depth_0;
};

struct vulkan_renderer {
	struct pshine_renderer as_base;
	struct pshine_game *game;
	GLFWwindow *window;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical_device;
	VkSurfaceFormatKHR surface_format;
	VkDevice device;
	uint32_t queue_families[QUEUE_FAMILY_COUNT];
	VkQueue queues[QUEUE_FAMILY_COUNT];
	VkSwapchainKHR swapchain;
	VkExtent2D swapchain_extent;
	uint32_t swapchain_image_count;
	VkImage *swapchain_images_own; // `*[.swapchain_image_count]`
	VkImageView *swapchain_image_views_own; // `*[.swapchain_image_count]`

	VkFormat depth_format;
	struct vulkan_image depth_image;
	struct render_pass_transients transients;

	VkFramebuffer *swapchain_framebuffers_own; // `*[.swapchain_image_count]`

	VkPhysicalDeviceProperties2 *physical_device_properties_own;

	struct {
		VkPipelineLayout tri_pipeline_layout;
		VkPipeline tri_pipeline;
		VkPipelineLayout mesh_pipeline_layout;
		VkPipeline mesh_pipeline;
		VkPipelineLayout planet_pipeline_layout;
		VkPipeline planet_pipeline;
		VkPipelineLayout atmosphere_pipeline_layout;
		VkPipeline atmosphere_pipeline;
	} pipelines;

	struct {
		VkRenderPass main_pass;
	} render_passes;
	VkCommandPool command_pool_graphics;
	VkCommandPool command_pool_transfer;

	struct {
		VkDescriptorPool pool;
		VkDescriptorSetLayout global_layout;
		VkDescriptorSetLayout material_layout;
		VkDescriptorSetLayout static_mesh_layout;
		VkDescriptorSetLayout atmo_layout;
	} descriptors;

	struct per_frame_data frames[FRAMES_IN_FLIGHT];

	VmaAllocator allocator;
	struct vulkan_mesh *sphere_mesh;
	// PSHINE_DYNA_(struct mesh) meshes;

	uint8_t *key_states;
};

VkResult check_vk_impl_(struct pshine_debug_file_loc where, VkResult result, const char *expr) {
	if (result == VK_SUCCESS) return result;
	(void)expr;
	pshine_log_impl(
		where, PSHINE_LOG_SEVERITY_CRITICAL,
		"Vulkan Error (%d): %s",
		(int32_t)result, pshine_vk_result_string(result)
	);
	PSHINE_PANIC("Vulkan Error");
}

#define CHECKVK(e) (check_vk_impl_(PSHINE_DEBUG_FILE_LOC_HERE, (e), #e))

struct pshine_renderer *pshine_create_renderer() {
	return &((struct vulkan_renderer*)calloc(1, sizeof(struct vulkan_renderer)))->as_base;
}

void pshine_destroy_renderer(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	free(r);
}

static void init_glfw(struct vulkan_renderer *r); static void deinit_glfw(struct vulkan_renderer *r);
static void init_vulkan(struct vulkan_renderer *r); static void deinit_vulkan(struct vulkan_renderer *r);
static void init_swapchain(struct vulkan_renderer *r); static void deinit_swapchain(struct vulkan_renderer *r);
static void init_rpasses(struct vulkan_renderer *r); static void deinit_rpasses(struct vulkan_renderer *r);
static void init_pipelines(struct vulkan_renderer *r); static void deinit_pipelines(struct vulkan_renderer *r);
static void init_fbufs(struct vulkan_renderer *r); static void deinit_fbufs(struct vulkan_renderer *r);
static void init_cmdbufs(struct vulkan_renderer *r); static void deinit_cmdbufs(struct vulkan_renderer *r);
static void init_sync(struct vulkan_renderer *r); static void deinit_sync(struct vulkan_renderer *r);
static void init_ubufs(struct vulkan_renderer *r); static void deinit_ubufs(struct vulkan_renderer *r);
static void init_descriptors(struct vulkan_renderer *r); static void deinit_descriptors(struct vulkan_renderer *r);
static void init_frames(struct vulkan_renderer *r); static void deinit_frames(struct vulkan_renderer *r);

static void deallocate_buffer(
	struct vulkan_renderer *r,
	struct vulkan_buffer buffer
) {
	vmaDestroyBuffer(r->allocator, buffer.buffer, buffer.allocation);
}

struct vulkan_image_alloc_info {
	VkImageCreateInfo *image_info;
	VmaAllocationCreateFlags allocation_flags;
	VmaMemoryUsage memory_usage;
	VkMemoryPropertyFlags required_memory_property_flags;
	VkMemoryPropertyFlags preferred_memory_property_flags;
	VmaAllocationInfo *out_allocation_info;
	VkImageViewCreateInfo *view_info;
};

static struct vulkan_image allocate_image(
	struct vulkan_renderer *r,
	const struct vulkan_image_alloc_info *info
) {
	struct vulkan_image img = {};
	info->image_info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	CHECKVK(vmaCreateImage(
		r->allocator,
		info->image_info,
		&(VmaAllocationCreateInfo){
			.flags = info->allocation_flags,
			.requiredFlags = info->required_memory_property_flags,
			.preferredFlags = info->preferred_memory_property_flags,
			.usage = info->memory_usage
		},
		&img.image,
		&img.allocation,
		info->out_allocation_info
	));
	if (info->view_info != NULL) {
		info->view_info->sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info->view_info->image = img.image;
		CHECKVK(vkCreateImageView(r->device, info->view_info, NULL, &img.view));
	}
	return img;
}

static void deallocate_image(
	struct vulkan_renderer *r,
	struct vulkan_image image
) {
	vmaDestroyImage(r->allocator, image.image, image.allocation);
	if (image.view != VK_NULL_HANDLE)
		vkDestroyImageView(r->device, image.view, NULL);
}

static struct vulkan_buffer allocate_buffer(
	struct vulkan_renderer *r,
	VkDeviceSize size,
	VkBufferUsageFlags buffer_usage,
	VkMemoryPropertyFlags required_memory_property_flags,
	VmaAllocationCreateFlags allocation_flags,
	VmaMemoryUsage memory_usage,
	VmaAllocationInfo *out_allocation_info
) {
	struct vulkan_buffer buf;
	vmaCreateBuffer(
		r->allocator,
		&(VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = buffer_usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		},
		&(VmaAllocationCreateInfo){
			.usage = memory_usage,
			.requiredFlags = required_memory_property_flags,
			.flags = allocation_flags
		},
		&buf.buffer,
		&buf.allocation,
		out_allocation_info
	);
	return buf;
}

static VkDeviceSize get_padded_uniform_buffer_size(struct vulkan_renderer *r, VkDeviceSize original_size) {
	VkDeviceSize align = r->physical_device_properties_own->properties.limits.minUniformBufferOffsetAlignment;
	return align > 0 ? (original_size + align - 1) & ~(align - 1) : original_size;
}

static void write_to_buffer_staged(
	struct vulkan_renderer *r,
	struct vulkan_buffer *buffer,
	VkDeviceSize offset,
	VkDeviceSize size,
	const void *data
) {
	VmaAllocationInfo staging_alloc_info = {};
	struct vulkan_buffer staging_buffer = allocate_buffer(
		r,
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		0,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		| VMA_ALLOCATION_CREATE_MAPPED_BIT,
		VMA_MEMORY_USAGE_AUTO,
		&staging_alloc_info
	);
	memcpy((char*)staging_alloc_info.pMappedData, data, size);

	VkCommandBuffer command_buffer;
	CHECKVK(vkAllocateCommandBuffers(r->device, &(VkCommandBufferAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandBufferCount = 1,
		.commandPool = r->command_pool_transfer,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	}, &command_buffer));
	CHECKVK(vkBeginCommandBuffer(command_buffer, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	}));
	vkCmdCopyBuffer(command_buffer, staging_buffer.buffer, buffer->buffer, 1, &(VkBufferCopy){
		.srcOffset = 0,
		.dstOffset = offset,
		.size = size
	});
	CHECKVK(vkEndCommandBuffer(command_buffer));
	vkQueueSubmit(r->queues[QUEUE_GRAPHICS], 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &command_buffer
	}, VK_NULL_HANDLE);
	vkQueueWaitIdle(r->queues[QUEUE_GRAPHICS]);
	vkFreeCommandBuffers(r->device, r->command_pool_transfer, 1, &command_buffer);
	deallocate_buffer(r, staging_buffer);
}

static struct vulkan_mesh *create_mesh(
	struct vulkan_renderer *r,
	const struct pshine_mesh_data *mesh_data
) {
	struct vulkan_mesh *mesh = calloc(1, sizeof(struct vulkan_mesh));
	PSHINE_DEBUG("mesh %zu vertices, %zu indices", mesh_data->index_count, mesh_data->vertex_count);
	mesh->index_buffer = allocate_buffer(
		r,
		mesh_data->index_count * sizeof(uint32_t),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0, 0, VMA_MEMORY_USAGE_AUTO, NULL
	);
	mesh->vertex_buffer = allocate_buffer(
		r,
		mesh_data->vertex_count * sizeof(struct pshine_static_mesh_vertex),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		0, 0, VMA_MEMORY_USAGE_AUTO, NULL
	);
	write_to_buffer_staged(r, &mesh->index_buffer,
		0, mesh_data->index_count * sizeof(uint32_t), mesh_data->indices);
	write_to_buffer_staged(r, &mesh->vertex_buffer,
		0, mesh_data->vertex_count * sizeof(struct pshine_static_mesh_vertex), mesh_data->vertices);
	
	mesh->vertex_count = mesh_data->vertex_count;
	mesh->index_count = mesh_data->index_count;
	mesh->vertex_type = mesh_data->vertex_type;

	return mesh;
}

static void destroy_mesh(struct vulkan_renderer *r, struct vulkan_mesh *mesh) {
	deallocate_buffer(r, mesh->vertex_buffer);
	deallocate_buffer(r, mesh->index_buffer);
	free(mesh);
}

static inline void name_vk_object_impl(
	struct vulkan_renderer *r,
	uint64_t o,
	VkDebugReportObjectTypeEXT t,
	const char *n, ...
) {
	va_list va;
	va_start(va, fmt);
	char *s = pshine_vformat_string(n, va);
	va_end(va);
	CHECKVK(vkDebugMarkerSetObjectNameEXT((r)->device, &(VkDebugMarkerObjectNameInfoEXT){
		.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
		.pObjectName = s,
		.object = o,
		.objectType = t,
	}));
	free(s);
}

#define NAME_VK_OBJECT(r, o, t, n, ...) \
	name_vk_object_impl((r), (uint64_t)(o), (t), (n) __VA_OPT__(,) __VA_ARGS__)

void pshine_init_renderer(struct pshine_renderer *renderer, struct pshine_game *game) {
	struct vulkan_renderer *r = (void*)renderer;
	r->game = game;
	r->as_base.name = "Vulkan Renderer";

	r->key_states = calloc(PSHINE_KEY_COUNT_, sizeof(uint8_t));

	init_glfw(r);
	init_vulkan(r);
	init_swapchain(r);
	init_rpasses(r);
	init_descriptors(r);
	init_fbufs(r);
	init_pipelines(r);
	init_cmdbufs(r);
	init_sync(r);
	init_ubufs(r);
	init_frames(r);

	{
		struct pshine_mesh_data mesh_data = {
			.index_count = 6,
			.indices = (uint32_t[]){ 0, 1, 2, 2, 0, 3 },
			.vertex_count = 4,
			.vertices = (struct pshine_static_mesh_vertex[]){
				{ { +0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
				{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
				{ { -0.5f, +0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
				{ { +0.5f, +0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
			},
			.vertex_type = PSHINE_VERTEX_STATIC_MESH
		};
		pshine_generate_planet_mesh(NULL, &mesh_data);
		r->sphere_mesh = create_mesh(r, &mesh_data);
		free(mesh_data.indices);
		free(mesh_data.vertices);
	}

	for (uint32_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			p->graphics_data = calloc(1, sizeof(struct pshine_planet_graphics_data));
			p->graphics_data->mesh_ref = r->sphere_mesh;
			p->graphics_data->uniform_buffer = allocate_buffer(
				r,
				get_padded_uniform_buffer_size(r, sizeof(struct static_mesh_uniform_data)) * FRAMES_IN_FLIGHT,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
				NULL
			);
			CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = r->descriptors.pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &r->descriptors.static_mesh_layout,
			}, &p->graphics_data->descriptor_set));
			vkUpdateDescriptorSets(r->device, 1, (VkWriteDescriptorSet[]){
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.dstSet = p->graphics_data->descriptor_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.pBufferInfo = &(VkDescriptorBufferInfo){
						.buffer = p->graphics_data->uniform_buffer.buffer,
						.offset = 0,
						.range = sizeof(struct static_mesh_uniform_data)
					}
				}
			}, 0, NULL);
		}
	}
}

// Vulkan and GLFW

static void error_cb_glfw_(int error, const char *msg) {
	PSHINE_ERROR("glfw error %d: %s", error, msg);
}

static void key_cb_glfw_(GLFWwindow *window, int key, int scancode, int action, int mods) {
	struct vulkan_renderer *r = glfwGetWindowUserPointer(window);
	r->key_states[key] = action;
}

static void init_glfw(struct vulkan_renderer *r) {
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	glfwSetErrorCallback(&error_cb_glfw_);
	if (!glfwInit()) PSHINE_PANIC("could not initialize GLFW");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	r->window = glfwCreateWindow(1920 / 1.5f, 1080 / 1.5f, "pshine2", NULL, NULL);
	glfwSetWindowUserPointer(r->window, r);
	if (r->window == NULL) PSHINE_PANIC("could not create window");
	glfwSetKeyCallback(r->window, &key_cb_glfw_);
}

static void deinit_glfw(struct vulkan_renderer *r) {
	glfwDestroyWindow(r->window);
	glfwTerminate();
}

static void init_vulkan(struct vulkan_renderer *r) {
	CHECKVK(volkInitialize());

	uint32_t extension_count_glfw = 0;
	const char **extensions_glfw = glfwGetRequiredInstanceExtensions(&extension_count_glfw);

	bool have_portability_ext = false;
	{
		uint32_t count = 0;
		CHECKVK(vkEnumerateInstanceExtensionProperties(NULL, &count, NULL));
		VkExtensionProperties properties[count];
		CHECKVK(vkEnumerateInstanceExtensionProperties(NULL, &count, properties));
		for (uint32_t i = 0; i < count; ++i) {
			if (strcmp(properties[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
				have_portability_ext = true;
				break;
			}
		}
	}

	uint32_t ext_count = extension_count_glfw + 2 + have_portability_ext;
	const char **extensions = calloc(ext_count, sizeof(const char *));
	memcpy(extensions, extensions_glfw, sizeof(const char *) * extension_count_glfw);
	extensions[extension_count_glfw + 0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	extensions[extension_count_glfw + 1] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
	if (have_portability_ext)
		extensions[extension_count_glfw + 2] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

	CHECKVK(vkCreateInstance(&(VkInstanceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &(VkApplicationInfo){
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.apiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0),
			.pApplicationName = "planetshine",
			.applicationVersion = VK_MAKE_VERSION(0, 1, 0),
			.pEngineName = "planetshine-engine",
			.engineVersion = VK_MAKE_VERSION(0, 2, 0)
		},
		.enabledExtensionCount = ext_count,
		.ppEnabledExtensionNames = extensions,
		.enabledLayerCount = 1, // VALIDATION_LAYERS
		.ppEnabledLayerNames = (const char *[1]) {
			"VK_LAYER_KHRONOS_validation"
		}
	}, NULL, &r->instance));

	free(extensions);

	volkLoadInstanceOnly(r->instance);

	CHECKVK(glfwCreateWindowSurface(r->instance, r->window, NULL, &r->surface));

	{
		uint32_t physical_device_count = 0;
		CHECKVK(vkEnumeratePhysicalDevices(r->instance, &physical_device_count, NULL));
		VkPhysicalDevice physical_devices[physical_device_count];
		CHECKVK(vkEnumeratePhysicalDevices(r->instance, &physical_device_count, physical_devices));
		r->physical_device = physical_devices[0]; // TODO: physical device selection
	}

	{
		uint32_t property_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &property_count, NULL);
		VkQueueFamilyProperties properties[property_count];
		vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &property_count, properties);
		for (uint32_t i = 0; i < property_count; ++i) {
			if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				r->queue_families[QUEUE_GRAPHICS] = i;
			}
			VkBool32 is_surface_supported = VK_FALSE;
			CHECKVK(vkGetPhysicalDeviceSurfaceSupportKHR(r->physical_device, i, r->surface, &is_surface_supported));
			if (is_surface_supported) {
				r->queue_families[QUEUE_PRESENT] = i;
			}
		}

		r->physical_device_properties_own = calloc(1, sizeof(VkPhysicalDeviceProperties2));
		r->physical_device_properties_own->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		vkGetPhysicalDeviceProperties2(r->physical_device, r->physical_device_properties_own);
	}

	{
		uint32_t queue_create_info_count = QUEUE_FAMILY_COUNT;
		static_assert(QUEUE_FAMILY_COUNT == 2);
		if (r->queue_families[0] == r->queue_families[1]) queue_create_info_count = 1;
		VkDeviceQueueCreateInfo queue_create_infos[queue_create_info_count];

		float queuePriority = 1.0f;
		for (uint32_t i = 0; i < queue_create_info_count; ++i) {
			queue_create_infos[i] = (VkDeviceQueueCreateInfo){
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.pQueuePriorities = &queuePriority,
				.queueCount = 1,
				.queueFamilyIndex = r->queue_families[i]
			};
		}

		CHECKVK(vkCreateDevice(r->physical_device, &(VkDeviceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = queue_create_info_count,
			.pQueueCreateInfos = queue_create_infos,
			.enabledExtensionCount = 2,
			.ppEnabledExtensionNames = (const char *[]){
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
			},
			.pEnabledFeatures = &(VkPhysicalDeviceFeatures){}
		}, NULL, &r->device));
	}

	volkLoadDevice(r->device);

	vkGetDeviceQueue(r->device, r->queue_families[QUEUE_GRAPHICS], 0, &r->queues[QUEUE_GRAPHICS]);
	vkGetDeviceQueue(r->device, r->queue_families[QUEUE_PRESENT], 0, &r->queues[QUEUE_PRESENT]);

	CHECKVK(vmaCreateAllocator(&(VmaAllocatorCreateInfo){
		.instance = r->instance,
		.device = r->device,
		.physicalDevice = r->physical_device,
		.vulkanApiVersion = VK_MAKE_API_VERSION(0, 1, 2, 0),
		.pVulkanFunctions = &(VmaVulkanFunctions){
			.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
			.vkGetDeviceProcAddr = vkGetDeviceProcAddr
		}
	}, &r->allocator));
}

static void deinit_vulkan(struct vulkan_renderer *r) {
	vmaDestroyAllocator(r->allocator);

	vkDestroyDevice(r->device, NULL);

	free(r->physical_device_properties_own);

	vkDestroySurfaceKHR(r->instance, r->surface, NULL);
	vkDestroyInstance(r->instance, NULL);
}

// Swapchain

static VkExtent2D get_swapchain_extent(struct vulkan_renderer *r, const VkSurfaceCapabilitiesKHR *caps) {
	if (caps->currentExtent.width != UINT32_MAX) {
		return caps->currentExtent;
	} else {
		int width, height;
		glfwGetFramebufferSize(r->window, &width, &height);
		VkExtent2D extent = { (uint32_t)width, (uint32_t)height };
		extent.width = extent.width < caps->minImageExtent.width ? caps->minImageExtent.width
			: extent.width > caps->maxImageExtent.width ? caps->maxImageExtent.width : extent.width;
		extent.height = extent.height < caps->minImageExtent.height ? caps->minImageExtent.height
			: extent.height > caps->maxImageExtent.height ? caps->maxImageExtent.height : extent.height;
		return extent;
	}
}

static VkSurfaceFormatKHR find_surface_format(struct vulkan_renderer *r) {
	uint32_t format_count = 0;
	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, NULL));
	VkSurfaceFormatKHR formats[format_count];
	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, formats));
	VkSurfaceFormatKHR found_format;
	bool found = false;
	for (uint32_t i = 0; i < format_count; ++i) {
		if (formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
				found_format = formats[i];
				found = true;
			}
		}
	}
	PSHINE_CHECK(found, "did not find good surface format");
	return found_format;
}

static VkFormat find_optimal_format(
	struct vulkan_renderer *r,
	size_t candidate_format_count,
	VkFormat *candidate_formats,
	VkFormatFeatureFlags required_feature_flags,
	VkImageTiling tiling
) {
	VkFormat found_format = candidate_formats[0];
	bool found = false;
	for (size_t i = 0; i < candidate_format_count; ++i) {
		VkFormat candidate_format = candidate_formats[i];
    VkFormatProperties properties = {};
    vkGetPhysicalDeviceFormatProperties(r->physical_device, candidate_format, &properties);
		if (tiling == VK_IMAGE_TILING_LINEAR
		&& (properties.linearTilingFeatures & required_feature_flags) == required_feature_flags) {
			found_format = candidate_format;
			found = true;
			return found_format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL
		&& (properties.optimalTilingFeatures & required_feature_flags) == required_feature_flags) {
			found_format = candidate_format;
			found = true;
			return found_format;
		}
	}
	PSHINE_CHECK(found, "did not find good depth format");
	return found_format;
}

static void init_swapchain(struct vulkan_renderer *r) {
	VkSurfaceCapabilitiesKHR surface_capabilities = {};
	CHECKVK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical_device, r->surface, &surface_capabilities));
	r->swapchain_extent = get_swapchain_extent(r, &surface_capabilities);
	r->surface_format = find_surface_format(r);
	r->depth_format = find_optimal_format(r, 1, (VkFormat[]){
		VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
	}, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL);
	PSHINE_INFO("swapchain extent: %ux%u", r->swapchain_extent.width, r->swapchain_extent.height);

	// {
	// 	uint32_t surface_format_count = 0;
	// 	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &surface_format_count, NULL));
	// 	VkSurfaceFormatKHR surface_formats[surface_format_count];
	// 	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &surface_format_count, surface_formats));
	// 	for (uint32_t i = 0; i < surface_format_count; ++i) {
	// 		PSHINE_INFO("surface format: %d, color space: %d", surface_formats[i].format, surface_formats[i].colorSpace);
	// 	}
	// }

	CHECKVK(vkCreateSwapchainKHR(r->device, &(VkSwapchainCreateInfoKHR){
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = r->surface,
		.minImageCount = surface_capabilities.minImageCount,
		.imageFormat = r->surface_format.format,
		.imageArrayLayers = 1,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = r->swapchain_extent,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.queueFamilyIndexCount = r->queue_families[QUEUE_GRAPHICS] != r->queue_families[QUEUE_PRESENT] ? 2 : 0,
		.imageSharingMode = r->queue_families[QUEUE_GRAPHICS] != r->queue_families[QUEUE_PRESENT]
			? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.pQueueFamilyIndices = (uint32_t[]) { r->queue_families[QUEUE_GRAPHICS], r->queue_families[QUEUE_PRESENT] },
		.preTransform = surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.oldSwapchain = VK_NULL_HANDLE,
	}, NULL, &r->swapchain));

	r->swapchain_image_count = 0;
	CHECKVK(vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchain_image_count, NULL));
	r->swapchain_images_own = calloc(r->swapchain_image_count, sizeof(VkImage));
	CHECKVK(vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchain_image_count, r->swapchain_images_own));

	r->swapchain_image_views_own = calloc(r->swapchain_image_count, sizeof(VkImageView));
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		CHECKVK(vkCreateImageView(r->device, &(VkImageViewCreateInfo){
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = r->swapchain_images_own[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = r->surface_format.format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		}, NULL, &r->swapchain_image_views_own[i]));
	}
}

static void deinit_swapchain(struct vulkan_renderer *r) {	
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i)
		vkDestroyImageView(r->device, r->swapchain_image_views_own[i], NULL);

	free(r->swapchain_image_views_own);

	free(r->swapchain_images_own);

	vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
}


// Render passes

static void init_rpasses(struct vulkan_renderer *r) {
	CHECKVK(vkCreateRenderPass(r->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.subpassCount = 2,
		.pSubpasses = (VkSubpassDescription[]){
			(VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = 2,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.pDepthStencilAttachment = &(VkAttachmentReference){
					.attachment = 1,
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				}
			},
			(VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = 0,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.inputAttachmentCount = 2,
				.pInputAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = 2,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = 1,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
				}
			}
		},
		.attachmentCount = 3,
		.pAttachments = (VkAttachmentDescription[]){
			(VkAttachmentDescription){
				.format = r->surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			(VkAttachmentDescription){
				.format = r->depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
			},
			(VkAttachmentDescription){
				.format = VK_FORMAT_R8G8B8A8_SRGB,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		},
		.dependencyCount = 4, // TODO: synchronization
		.pDependencies = (VkSubpassDependency[]){
			(VkSubpassDependency){
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask
					= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
					| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = 0,
				.dstStageMask
					= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
					| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.dstAccessMask
					= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
					| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			},
			(VkSubpassDependency){
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 1,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			(VkSubpassDependency){
				.srcSubpass = 0,
				.dstSubpass = 1,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
			},
			(VkSubpassDependency){
				.srcSubpass = 0,
				.dstSubpass = 1,
				.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
			},
		}
	}, NULL, &r->render_passes.main_pass));
	vkDebugMarkerSetObjectNameEXT(r->device, &(VkDebugMarkerObjectNameInfoEXT){
		.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT,
		.pObjectName = "main render pass",
		.object = (uint64_t)r->render_passes.main_pass,
		.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT,
	});
}

static void deinit_rpasses(struct vulkan_renderer *r) {
	vkDestroyRenderPass(r->device, r->render_passes.main_pass, NULL);
}


// Framebuffers

static void init_fbufs(struct vulkan_renderer *r) {
	r->swapchain_framebuffers_own = calloc(r->swapchain_image_count, sizeof(VkFramebuffer));
	r->depth_image = allocate_image(r, &(struct vulkan_image_alloc_info){
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
		.allocation_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.extent = {
				.width = r->swapchain_extent.width,
				.height = r->swapchain_extent.height,
				.depth = 1,
			},
			.format = r->depth_format,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.mipLevels = 1,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage
				= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
				| VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
				| VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = r->depth_format,
			.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.baseMipLevel = 0,
				.levelCount = 1,
			}
		}
	});
	r->transients.color_0 = allocate_image(r, &(struct vulkan_image_alloc_info){
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
		.allocation_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.extent = {
				.width = r->swapchain_extent.width,
				.height = r->swapchain_extent.height,
				.depth = 1,
			},
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.mipLevels = 1,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage
				= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
				| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
				| VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R8G8B8A8_SRGB,
			.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.baseMipLevel = 0,
				.levelCount = 1,
			}
		}
	});
	NAME_VK_OBJECT(r, r->transients.color_0.image, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, "transient color0 image");
	NAME_VK_OBJECT(r, r->transients.color_0.view, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT, "transient color0 image view");

	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {

		CHECKVK(vkCreateFramebuffer(r->device, &(VkFramebufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.attachmentCount = 3,
			.pAttachments = (VkImageView[]){
				r->swapchain_image_views_own[i],
				r->depth_image.view,
				r->transients.color_0.view
			},
			.renderPass = r->render_passes.main_pass,
			.width = r->swapchain_extent.width,
			.height = r->swapchain_extent.height,
			.layers = 1,
		}, NULL, &r->swapchain_framebuffers_own[i]));
	}
}

static void deinit_fbufs(struct vulkan_renderer *r) {
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		vkDestroyFramebuffer(r->device, r->swapchain_framebuffers_own[i], NULL);
	}
	deallocate_image(r, r->depth_image);
	deallocate_image(r, r->transients.color_0);
	free(r->swapchain_framebuffers_own);
}


// Pipelines

static VkShaderModule create_shader_module(struct vulkan_renderer *r, size_t size, const char *src) {
	VkShaderModule shader_module = VK_NULL_HANDLE;
	CHECKVK(vkCreateShaderModule(r->device, &(VkShaderModuleCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = (const uint32_t*)src,
	}, NULL, &shader_module));
	return shader_module;
}

static VkShaderModule create_shader_module_file(struct vulkan_renderer *r, const char *fname) {
	size_t size = 0;
	char *src = pshine_read_file(fname, &size);
	VkShaderModule module = create_shader_module(r, size, src);
	free(src);
	return module;
}

struct pipeline_info {
	const char *vert_fname, *frag_fname;
	uint32_t push_constant_range_count;
	const VkPushConstantRange *push_constant_ranges;
	uint32_t set_layout_count;
	const VkDescriptorSetLayout *set_layouts;
	VkRenderPass render_pass;
	uint32_t subpass;
	const char *layout_name;
	const char *pipeline_name;
	bool depth_test;
	bool blend;
};

struct vulkan_pipeline {
	VkPipelineLayout layout;
	VkPipeline pipeline;
};

static struct vulkan_pipeline create_pipeline(struct vulkan_renderer *r, const struct pipeline_info *info) {
	VkShaderModule vert_shader_module = create_shader_module_file(r, info->vert_fname);
	VkShaderModule frag_shader_module = create_shader_module_file(r, info->frag_fname);

	VkPipelineLayout layout;
	VkPipeline pipeline;

	CHECKVK(vkCreatePipelineLayout(r->device, &(VkPipelineLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = info->set_layout_count,
		.pSetLayouts = info->set_layouts,
		.pushConstantRangeCount = info->push_constant_range_count,
		.pPushConstantRanges = info->push_constant_ranges
	}, NULL, &layout));
	
	if (info->layout_name != NULL)
		NAME_VK_OBJECT(r, layout, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT, info->layout_name);

	CHECKVK(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &(VkGraphicsPipelineCreateInfo){
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
				.binding = 0,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
				.stride = sizeof(struct pshine_static_mesh_vertex)
			},
			.vertexAttributeDescriptionCount = 3,
			.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]){
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32G32B32_SFLOAT,
					.location = 0,
					.offset = offsetof(struct pshine_static_mesh_vertex, position)
				},
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32G32B32_SFLOAT,
					.location = 1,
					.offset = offsetof(struct pshine_static_mesh_vertex, normal)
				},
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32G32_SFLOAT,
					.location = 2,
					.offset = offsetof(struct pshine_static_mesh_vertex, texcoord)
				}
			}
		},
		.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.primitiveRestartEnable = VK_FALSE,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
		},
		.pViewportState = &(VkPipelineViewportStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.pViewports = &(VkViewport){
				.width = r->swapchain_extent.width,
				.height = r->swapchain_extent.height,
				.x = 0.0f,
				.y = 0.0f,
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			},
			.viewportCount = 1,
			.scissorCount = 1,
			.pScissors = &(VkRect2D){
				.offset = { 0, 0 },
				.extent = r->swapchain_extent,
			},
		},
		.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.depthClampEnable = VK_FALSE,
			.rasterizerDiscardEnable = VK_FALSE,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.lineWidth = 1.0f,
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.depthBiasEnable = VK_FALSE,
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.sampleShadingEnable = VK_FALSE,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
		},
		.pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthBoundsTestEnable = VK_FALSE,
			.depthTestEnable = info->depth_test ? VK_TRUE : VK_FALSE,
			.depthWriteEnable = VK_TRUE,
			.stencilTestEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_GREATER,
			.maxDepthBounds = 1.0f,
			.minDepthBounds = 0.0f,
			.back = {},
			.front = {}
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = &(VkPipelineColorBlendAttachmentState){
				.colorWriteMask
					= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
					| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
				.blendEnable = info->blend ? VK_TRUE : VK_FALSE,
				.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
				.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
				.colorBlendOp = VK_BLEND_OP_ADD,
				.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
				.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
				.alphaBlendOp = VK_BLEND_OP_ADD,
			}
		},
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]){
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vert_shader_module,
				.pName = "main"
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = frag_shader_module,
				.pName = "main"
			}
		},
		.layout = layout,
		.renderPass = info->render_pass,
		.subpass = info->subpass
	}, NULL, &pipeline));
	
	if (info->pipeline_name != NULL)
		NAME_VK_OBJECT(r, pipeline, VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, info->pipeline_name);

	vkDestroyShaderModule(r->device, vert_shader_module, NULL);
	vkDestroyShaderModule(r->device, frag_shader_module, NULL);

	return (struct vulkan_pipeline){ .pipeline = pipeline, .layout = layout };
}

static void init_pipelines(struct vulkan_renderer *r) {
	struct vulkan_pipeline mesh_pipeline = create_pipeline(r, &(struct pipeline_info){
		.vert_fname = "build/data/mesh.vert.spv",
		.frag_fname = "build/data/mesh.frag.spv",
		.render_pass = r->render_passes.main_pass,
		.subpass = 0,
		.push_constant_range_count = 0,
		.set_layout_count = 3,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.material_layout,
			r->descriptors.static_mesh_layout,
		},
		.blend = false,
		.depth_test = true,
		.layout_name = "static mesh pipeline layout",
		.pipeline_name = "static mesh pipeline"
	});
	r->pipelines.mesh_pipeline = mesh_pipeline.pipeline;
	r->pipelines.mesh_pipeline_layout = mesh_pipeline.layout;
	struct vulkan_pipeline atmo_pipeline = create_pipeline(r, &(struct pipeline_info){
		.vert_fname = "build/data/atmo.vert.spv",
		.frag_fname = "build/data/atmo.frag.spv",
		.render_pass = r->render_passes.main_pass,
		.subpass = 1,
		.push_constant_range_count = 0,
		.set_layout_count = 3,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.material_layout,
			r->descriptors.atmo_layout,
		},
		.blend = false,
		.depth_test = false,
		.layout_name = "atmosphere pipeline layout",
		.pipeline_name = "atmosphere pipeline"
	});
	r->pipelines.atmosphere_pipeline = atmo_pipeline.pipeline;
	r->pipelines.atmosphere_pipeline_layout = atmo_pipeline.layout;
}

static void deinit_pipelines(struct vulkan_renderer *r) {
	vkDestroyPipeline(r->device, r->pipelines.mesh_pipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelines.mesh_pipeline_layout, NULL);
	vkDestroyPipeline(r->device, r->pipelines.atmosphere_pipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelines.atmosphere_pipeline_layout, NULL);
}


// Command buffers

static void init_cmdbufs(struct vulkan_renderer *r) {
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
	}, NULL, &r->command_pool_graphics));
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
	}, NULL, &r->command_pool_transfer));
}

static void deinit_cmdbufs(struct vulkan_renderer *r) {
	vkDestroyCommandPool(r->device, r->command_pool_graphics, NULL);
	vkDestroyCommandPool(r->device, r->command_pool_transfer, NULL);
}


// Synchronization

static void init_sync(struct vulkan_renderer *r) {
	(void)r;
}

static void deinit_sync(struct vulkan_renderer *r) {
	(void)r;
}


// Uniform buffers

static void init_ubufs(struct vulkan_renderer *r) {
	(void)r;
}

static void deinit_ubufs(struct vulkan_renderer *r) {
	(void)r;
}


// Descriptors

static void init_descriptors(struct vulkan_renderer *r) {
	vkCreateDescriptorPool(r->device, &(VkDescriptorPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1024,
		.poolSizeCount = 2,
		.pPoolSizes = (VkDescriptorPoolSize[]){
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC },
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT },
		}
	}, NULL, &r->descriptors.pool);
	NAME_VK_OBJECT(r, r->descriptors.pool, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT, "descriptor pool 1");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS
		}
	}, NULL, &r->descriptors.global_layout);
	NAME_VK_OBJECT(r, r->descriptors.global_layout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "global descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	}, NULL, &r->descriptors.material_layout);
	NAME_VK_OBJECT(r, r->descriptors.material_layout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "material descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		}
	}, NULL, &r->descriptors.static_mesh_layout);
	NAME_VK_OBJECT(r, r->descriptors.static_mesh_layout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "static mesh descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 2,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, NULL, &r->descriptors.atmo_layout);
	NAME_VK_OBJECT(r, r->descriptors.atmo_layout, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT, "atmosphere descriptor set layout");
}

static void deinit_descriptors(struct vulkan_renderer *r) {
	vkDestroyDescriptorPool(r->device, r->descriptors.pool, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.global_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.material_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.static_mesh_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.atmo_layout, NULL);
}


// Per-frame data

static void init_frame(
	struct vulkan_renderer *r,
	uint32_t frame_index,
	struct per_frame_data *f,
	size_t planet_count
) {
	f->global_uniform_buffer = allocate_buffer(
		r,
		get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		NULL
	);
	NAME_VK_OBJECT(r, f->global_uniform_buffer.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "global ub #%u", frame_index);

	f->material_uniform_buffer = allocate_buffer(
		r,
		sizeof(struct material_uniform_data),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		NULL
	);
	NAME_VK_OBJECT(r, f->material_uniform_buffer.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "material ub #%u", frame_index);

	f->atmo_uniform_buffer = allocate_buffer(
		r,
		get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		NULL
	);
	NAME_VK_OBJECT(r, f->atmo_uniform_buffer.buffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "atmosphere ub #%u", frame_index);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.global_layout
	}, &f->global_descriptor_set));
	NAME_VK_OBJECT(r, f->global_descriptor_set, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, "global ds #%u", frame_index);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.material_layout
	}, &f->material_descriptor_set));
	NAME_VK_OBJECT(r, f->material_descriptor_set, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, "material ds #%u", frame_index);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.atmo_layout
	}, &f->atmo_descriptor_set));
	NAME_VK_OBJECT(r, f->atmo_descriptor_set, VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT, "atmosphere ds #%u", frame_index);

	vkUpdateDescriptorSets(r->device, 5, (VkWriteDescriptorSet[]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = f->atmo_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = f->atmo_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct atmo_uniform_data)
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = f->atmo_descriptor_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.imageView = r->transients.color_0.view,
				.sampler = VK_NULL_HANDLE
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = f->atmo_descriptor_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.imageView = r->depth_image.view,
				.sampler = VK_NULL_HANDLE
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.dstSet = f->material_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = f->material_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct material_uniform_data)
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = f->global_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = f->global_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct global_uniform_data)
			}
		}
	}, 0, NULL);

	CHECKVK(vkAllocateCommandBuffers(r->device, &(VkCommandBufferAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->command_pool_graphics,
		.commandBufferCount = 1,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY
	}, &f->command_buffer));

	CHECKVK(vkCreateSemaphore(r->device, &(VkSemaphoreCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	}, NULL, &f->sync.image_avail_semaphore));
	CHECKVK(vkCreateSemaphore(r->device, &(VkSemaphoreCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	}, NULL, &f->sync.render_finish_semaphore));
	CHECKVK(vkCreateFence(r->device, &(VkFenceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	}, NULL, &f->sync.in_flight_fence));
}

void deinit_frame(struct vulkan_renderer *r, struct per_frame_data *f) {
	deallocate_buffer(r, f->global_uniform_buffer);
	deallocate_buffer(r, f->material_uniform_buffer);
	deallocate_buffer(r, f->atmo_uniform_buffer);
	vkDestroyFence(r->device, f->sync.in_flight_fence, NULL);
	vkDestroySemaphore(r->device, f->sync.render_finish_semaphore, NULL);
	vkDestroySemaphore(r->device, f->sync.image_avail_semaphore, NULL);
}

static void init_frames(struct vulkan_renderer *r) {
	size_t planet_count = 0;
	for (size_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		planet_count += b->type == PSHINE_CELESTIAL_BODY_PLANET;
	}
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
		init_frame(r, i, &r->frames[i], planet_count);
	}
}

static void deinit_frames(struct vulkan_renderer *r) {
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
		deinit_frame(r, &r->frames[i]);
	}
}


void pshine_deinit_renderer(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	
	vkDeviceWaitIdle(r->device);

	for (uint32_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			deallocate_buffer(r, p->graphics_data->uniform_buffer);
			free(p->graphics_data);
		}
	}

	destroy_mesh(r, r->sphere_mesh);

	deinit_frames(r);
	deinit_ubufs(r);
	deinit_sync(r);
	deinit_cmdbufs(r);
	deinit_fbufs(r);
	deinit_pipelines(r);
	deinit_descriptors(r);
	deinit_rpasses(r);
	deinit_swapchain(r);
	deinit_vulkan(r);
	deinit_glfw(r);

	free(r->key_states);
}

static void do_frame(struct vulkan_renderer *r, uint32_t current_frame, uint32_t image_index) {
	struct per_frame_data *f = &r->frames[current_frame];

	{
		struct material_uniform_data new_data = {
			.color = float4rgba(0.1f, 0.3f, 0.8f, 1.0f)
		};
		struct material_uniform_data *data;
		vmaMapMemory(r->allocator, f->material_uniform_buffer.allocation, (void**)&data);
		memcpy(data, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, f->material_uniform_buffer.allocation);
	}

	float aspect_ratio = r->swapchain_extent.width /(float) r->swapchain_extent.height;
	float4x4 view_mat = {};
	setfloat4x4iden(&view_mat);
	setfloat4x4lookat(
		&view_mat,
		float3vs(r->game->camera_position.values),
		float3add(float3vs(r->game->camera_position.values), float3vs(r->game->camera_forward.values)),
		float3xyz(0.0f, 1.0f, 0.0f)
	);
	// float4x4trans(&view_mat, float3neg(float3vs(r->game->camera_position.values)));
	float4x4 proj_mat = {};
	struct float4x4persp_info persp_info = setfloat4x4persp(&proj_mat, 60.0f, aspect_ratio, 0.01f);
	float4x4 view_proj_mat = {};
	float4x4mul(&view_proj_mat, &proj_mat, &view_mat);

	{
		float3 cam_y = float3xyz(0.0f, 1.0f, 0.0f);
		float3 cam_z = float3norm(float3vs(r->game->camera_forward.values));
		float3 cam_x = float3norm(float3cross(cam_y, cam_z));
		cam_y = float3norm(float3cross(cam_z, cam_x));
		struct global_uniform_data new_data = {
			.proj = proj_mat,
			.view = view_mat,
			.sun = float4xyz3w(float3norm(float3xyz(-1.0f, 0.0f, 0.0f)), 1.0f),
			.camera = float4xyz3w(float3vs(r->game->camera_position.values), persp_info.znear),
		};
		char *data;
		vmaMapMemory(r->allocator, f->global_uniform_buffer.allocation, (void**)&data);
		data += get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame;
		memcpy(data, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, f->global_uniform_buffer.allocation);
	}

	for (size_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			struct static_mesh_uniform_data new_data = {};
			float4x4 model_mat = {};
			setfloat4x4iden(&model_mat);
			float4x4trans(&model_mat, float3vs(p->as_body.position.values));
			float4x4scale(&model_mat, float3v(p->as_body.radius));
			float4x4mul(&new_data.mvp, &view_proj_mat, &model_mat);

			char *data_raw;
			vmaMapMemory(r->allocator, p->graphics_data->uniform_buffer.allocation, (void**)&data_raw);
			data_raw += get_padded_uniform_buffer_size(r, sizeof(struct static_mesh_uniform_data)) * current_frame;
			memcpy(data_raw, &new_data, sizeof(new_data));
			vmaUnmapMemory(r->allocator, p->graphics_data->uniform_buffer.allocation);
		}
	}

	{
		struct pshine_planet *p = (void*)r->game->celestial_bodies_own[0];
		
		float3 wavelengths = float3rgb(
			powf(400.0f / p->atmosphere.wavelengths[0], 4) * p->atmosphere.scattering_strength,
			powf(400.0f / p->atmosphere.wavelengths[1], 4) * p->atmosphere.scattering_strength,
			powf(400.0f / p->atmosphere.wavelengths[2], 4) * p->atmosphere.scattering_strength
		);
		struct atmo_uniform_data new_data = {
			.density_falloff = p->atmosphere.density_falloff,
			.optical_depth_samples = 5,
			.scatter_point_samples = 5,
			.planet = float4xyz3w(float3vs(p->as_body.position.values), p->as_body.radius),
			.radius = p->as_body.radius + p->atmosphere.height,
			.blend_factor = r->game->atmo_blend_factor,
			.wavelengths = wavelengths
		};
		char *data_raw;
		vmaMapMemory(r->allocator, f->atmo_uniform_buffer.allocation, (void**)&data_raw);
		data_raw += get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame;
		memcpy(data_raw, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, f->atmo_uniform_buffer.allocation);
	}

	CHECKVK(vkBeginCommandBuffer(f->command_buffer, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	}));
	vkCmdBeginRenderPass(f->command_buffer, &(VkRenderPassBeginInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = r->render_passes.main_pass,
		.framebuffer = r->swapchain_framebuffers_own[image_index],
		.renderArea = (VkRect2D){
			.offset = { 0, 0 },
			.extent = r->swapchain_extent
		},
		.clearValueCount = 3,
		.pClearValues = (VkClearValue[]){
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
			(VkClearValue){ .depthStencil = (VkClearDepthStencilValue){ .depth = 0.0f, .stencil = 0 } },
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
		}
	}, VK_SUBPASS_CONTENTS_INLINE);

	// vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.tri_pipeline);
	// vkCmdDraw(f->command_buffer, 3, 1, 0, 0);

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.mesh_pipeline);

	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.mesh_pipeline_layout,
		0,
		2,
		(VkDescriptorSet[]){ f->global_descriptor_set, f->material_descriptor_set },
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
		}
	);

	for (size_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.mesh_pipeline_layout,
				2,
				1,
				&p->graphics_data->descriptor_set,
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct static_mesh_uniform_data)) * current_frame
				}
			);
			vkCmdBindVertexBuffers(f->command_buffer, 0, 1, &p->graphics_data->mesh_ref->vertex_buffer.buffer, &(VkDeviceSize){0});
			vkCmdBindIndexBuffer(f->command_buffer, p->graphics_data->mesh_ref->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(f->command_buffer, p->graphics_data->mesh_ref->index_count, 1, 0, 0, 0);
		}
	}

	vkCmdNextSubpass(f->command_buffer, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.atmosphere_pipeline);
	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.atmosphere_pipeline_layout,
		2,
		1,
		&f->atmo_descriptor_set,
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame
		}
	);
	vkCmdDraw(f->command_buffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(f->command_buffer);
	CHECKVK(vkEndCommandBuffer(f->command_buffer));
}

static void render(struct vulkan_renderer *r, uint32_t current_frame) {
	struct per_frame_data *f = &r->frames[current_frame];
	vkWaitForFences(r->device, 1, &f->sync.in_flight_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(r->device, 1, &f->sync.in_flight_fence);
	uint32_t image_index = 0;
	CHECKVK(vkAcquireNextImageKHR(
		r->device, r->swapchain,
		UINT64_MAX,
		f->sync.image_avail_semaphore, VK_NULL_HANDLE,
		&image_index
	));
	CHECKVK(vkResetCommandBuffer(f->command_buffer, 0));
	do_frame(r, current_frame, image_index);
	CHECKVK(vkQueueSubmit(r->queues[QUEUE_GRAPHICS], 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &f->command_buffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &f->sync.render_finish_semaphore,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &f->sync.image_avail_semaphore,
		.pWaitDstStageMask = &(VkPipelineStageFlags){
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		}
	}, f->sync.in_flight_fence));
	CHECKVK(vkQueuePresentKHR(r->queues[QUEUE_PRESENT], &(VkPresentInfoKHR){
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &f->sync.render_finish_semaphore,
		.swapchainCount = 1,
		.pSwapchains = &r->swapchain,
		.pImageIndices = &image_index,
		.pResults = NULL
	}));
}

void pshine_main_loop(struct pshine_game *game, struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	float last_time = glfwGetTime();
	uint32_t current_frame = 0;
	while (!glfwWindowShouldClose(r->window)) {
		float current_time = glfwGetTime();
		float delta_time = current_time - last_time;
		glfwPollEvents();
		pshine_update_game(game, delta_time);
		render(r, current_frame);
		last_time = current_time;
		current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
	}
}

const uint8_t *pshine_get_key_states(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	return r->key_states;
}
