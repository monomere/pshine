#include <pshine/game.h>
#include <pshine/util.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>

#include <cimgui/cimgui.h>
#include <cimgui/backends/cimgui_impl_glfw.h>
#include <cimgui/backends/cimgui_impl_vulkan.h>
#include "stb_image.h"

// #include <giraffe/giraffe.h>

#include "vk_util.h"
#include "math.h"

#define SCSd3_WCSd3(wcs) (double3mul((wcs), PSHINE_SCS_FACTOR))
#define SCSd3_WCSp3(wcs) SCSd3_WCSd3(double3vs((wcs).values))
#define SCSd_WCSd(wcs) ((wcs) * PSHINE_SCS_FACTOR)

struct vulkan_renderer;
enum queue_family {
	QUEUE_GRAPHICS,
	QUEUE_PRESENT,
	QUEUE_COMPUTE,
	QUEUE_FAMILY_COUNT_
};

#define FRAMES_IN_FLIGHT 2

struct global_uniform_data {
	float4 sun;
	float4 camera;        // xyz, w=near_plane.z
	float4 camera_right;  // xyz, w=near_plane.x
	float4 camera_up;     // xyz, w=near_plane.y
};

struct atmo_uniform_data {
	float4 planet; // xyz, w=radius
	float4 coefs_ray; // xyz=k_ray, w=falloff_ray
	float4 coefs_mie; // x=k_mie, y=k_mie_ext, z=g, w=falloff_mie
	float4 camera; // xyz, w=_
	float radius;
	unsigned int optical_depth_samples;
	unsigned int scatter_point_samples;
	float intensity;
};

struct material_uniform_data {
	float4 color;
	float3 view_dir;
	float smoothness;
};

struct static_mesh_uniform_data {
	float4x4 proj;
	float4x4 model_view;
	float4x4 model;
};

struct atmo_lut_push_const_data {
	float planet_radius;
	float atmo_height;
	float2 falloffs;
	uint32_t samples;
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
	struct vulkan_image atmo_lut;
	struct vulkan_image surface_albedo;
	struct vulkan_image surface_lights;
	struct vulkan_image surface_specular;
	struct vulkan_image surface_bump;
	VkDescriptorSet descriptor_set;
	VkCommandBuffer compute_cmdbuf;
	bool should_submit_compute;
};

struct per_frame_data {
	struct {
		VkSemaphore image_avail_semaphore;
		VkSemaphore render_finish_semaphore;
		VkFence in_flight_fence;
	} sync;
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
	uint32_t queue_families[QUEUE_FAMILY_COUNT_];
	VkQueue queues[QUEUE_FAMILY_COUNT_];
	VkSwapchainKHR swapchain;
	VkExtent2D swapchain_extent;
	uint32_t swapchain_image_count;
	VkImage *swapchain_images_own; // `*[.swapchain_image_count]`
	VkImageView *swapchain_image_views_own; // `*[.swapchain_image_count]`

	bool currently_recomputing_luts;

	VkFormat depth_format;
	struct vulkan_image depth_image;
	struct render_pass_transients transients;

	VkFramebuffer *swapchain_framebuffers_own; // `*[.swapchain_image_count]`

	VkPhysicalDeviceProperties2 *physical_device_properties_own;
	VkPhysicalDeviceFeatures *physical_device_features_own;

	struct {
		// graphics pipelines
		VkPipelineLayout tri_layout;
		VkPipeline tri_pipeline;
		VkPipelineLayout mesh_layout;
		VkPipeline mesh_pipeline;
		VkPipelineLayout planet_layout;
		VkPipeline planet_pipeline;
		VkPipelineLayout atmo_layout;
		VkPipeline atmo_pipeline;

		// compute pipelines
		VkPipelineLayout atmo_lut_layout;
		VkPipeline atmo_lut_pipeline;
	} pipelines;

	struct {
		VkRenderPass main_pass;
	} render_passes;
	VkCommandPool command_pool_graphics;
	VkCommandPool command_pool_transfer;
	VkCommandPool command_pool_compute;

	struct {
		VkDescriptorPool pool;
		VkDescriptorPool pool_imgui;
		VkDescriptorSetLayout global_layout;
		VkDescriptorSetLayout material_layout;
		VkDescriptorSetLayout static_mesh_layout;
		VkDescriptorSetLayout atmo_layout;
		VkDescriptorSetLayout atmo_lut_layout;
	} descriptors;

	struct {
		struct vulkan_buffer global_uniform_buffer;
		struct vulkan_buffer material_uniform_buffer;
		struct vulkan_buffer atmo_uniform_buffer;
		VkDescriptorSet global_descriptor_set;
		VkDescriptorSet material_descriptor_set; // TODO: move to material struct
		VkDescriptorSet atmo_descriptor_set;
		VkDescriptorSet atmo_lut_descriptor_set;
	} data;

	struct per_frame_data frames[FRAMES_IN_FLIGHT];

	VmaAllocator allocator;
	struct vulkan_mesh *sphere_mesh;

	VkSampler atmo_lut_sampler;
	VkSampler material_texture_sampler;
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
static void init_imgui(struct vulkan_renderer *r); static void deinit_imgui(struct vulkan_renderer *r);

static void init_atmo_lut_compute(struct vulkan_renderer *r, struct pshine_planet *planet);
static void compute_atmo_lut(struct vulkan_renderer *r, struct pshine_planet *planet, bool first_time);
static void load_planet_texture(struct vulkan_renderer *r, struct pshine_planet *planet);

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
	VkSamplerCreateInfo *sampler_info; // optional
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
	if (info->sampler_info != NULL) {
		info->sampler_info->sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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

struct vulkan_buffer_alloc_info {
	VkDeviceSize size;
	VkBufferUsageFlags buffer_usage;
	VkMemoryPropertyFlags required_memory_property_flags;
	VmaAllocationCreateFlags allocation_flags;
	VmaMemoryUsage memory_usage;
	VmaAllocationInfo *out_allocation_info;
	bool concurrent;
	uint32_t used_in_queues[QUEUE_FAMILY_COUNT_];
};

static struct vulkan_buffer allocate_buffer(
	struct vulkan_renderer *r,
	const struct vulkan_buffer_alloc_info *desc
) {
	uint32_t queue_use_count = 0;
	uint32_t used_queues[QUEUE_FAMILY_COUNT_] = {};
	if (desc->concurrent) {
		for (uint32_t i = 0; i < QUEUE_FAMILY_COUNT_; ++i) {
			bool is_used = desc->used_in_queues[i] != VK_QUEUE_FAMILY_IGNORED;
			if (is_used) used_queues[queue_use_count++] += desc->used_in_queues[i];
		}
	}

	struct vulkan_buffer buf;
	vmaCreateBuffer(
		r->allocator,
		&(VkBufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = desc->size,
			.usage = desc->buffer_usage,
			.sharingMode = queue_use_count <= 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
			.queueFamilyIndexCount = queue_use_count <= 1 ? 0 : queue_use_count,
			.pQueueFamilyIndices = queue_use_count <= 1 ? nullptr : used_queues,
		},
		&(VmaAllocationCreateInfo){
			.usage = desc->memory_usage,
			.requiredFlags = desc->required_memory_property_flags,
			.flags = desc->allocation_flags
		},
		&buf.buffer,
		&buf.allocation,
		desc->out_allocation_info
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
	// TODO: Use separate transfer queue if available.

	VmaAllocationInfo staging_alloc_info = {};
	struct vulkan_buffer staging_buffer = allocate_buffer(
		r,
		&(struct vulkan_buffer_alloc_info){
			.size = size,
			.buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
			.out_allocation_info = &staging_alloc_info,
		}
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

static void write_to_image_staged(
	struct vulkan_renderer *r,
	struct vulkan_image *target_image,
	VkExtent3D extent,
	VkFormat format,
	VkDeviceSize size,
	const void *data
) {
	VmaAllocationInfo staging_alloc_info = {};
	struct vulkan_buffer staging_buffer = allocate_buffer(
		r,
		&(struct vulkan_buffer_alloc_info){
			.size = size,
			.buffer_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
			.out_allocation_info = &staging_alloc_info,
		}
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
	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = target_image->image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		}
	);
	vkCmdCopyBufferToImage(
		command_buffer,
		staging_buffer.buffer,
		target_image->image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&(VkBufferImageCopy){
			.imageExtent = extent,
			.imageOffset = { 0, 0, 0 },
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.imageSubresource.mipLevel = 0,
			.imageSubresource.baseArrayLayer = 0,
			.imageSubresource.layerCount = 1,
		}
	);
	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, NULL,
		0, NULL,
		1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = target_image->image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		}
	);
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
		&(struct vulkan_buffer_alloc_info){
			.size = mesh_data->index_count * sizeof(uint32_t),
			.buffer_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
		}
	);
	mesh->vertex_buffer = allocate_buffer(
		r,
		&(struct vulkan_buffer_alloc_info){
			.size = mesh_data->vertex_count * sizeof(struct pshine_static_mesh_vertex),
			.buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
		}
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
	VkObjectType t,
	const char *n, ...
) {
	va_list va;
	va_start(va, n);
	char *s = pshine_vformat_string(n, va);
	va_end(va);
	CHECKVK(vkSetDebugUtilsObjectNameEXT((r)->device, &(VkDebugUtilsObjectNameInfoEXT){
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.pObjectName = s,
		.objectHandle = o,
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
	init_imgui(r);

	{
		VkPhysicalDeviceFeatures features = {};
		vkGetPhysicalDeviceFeatures(r->physical_device, &features);
		PSHINE_INFO("GPU 64 bit float support: %s", features.shaderFloat64 ? "true" : "false");
		PSHINE_INFO("GPU 64 bit int support:   %s", features.shaderInt64 ? "true" : "false");
	}

	{
		struct pshine_mesh_data mesh_data = {
			.index_count = 0,
			.indices = NULL,
			.vertex_count = 0,
			.vertices = NULL,
			.vertex_type = PSHINE_VERTEX_PLANET
		};
		pshine_generate_planet_mesh(NULL, &mesh_data);
		r->sphere_mesh = create_mesh(r, &mesh_data);
		free(mesh_data.indices);
		free(mesh_data.vertices);
	}
	
	vkCreateSampler(r->device, &(VkSamplerCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.mipLodBias = 0.0f,
		.minLod = 0.0f,
		.maxLod = 0.0f,
	}, NULL, &r->atmo_lut_sampler);
	
	vkCreateSampler(r->device, &(VkSamplerCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 1.0f,
		.compareEnable = VK_FALSE,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.mipLodBias = 0.0f,
		.minLod = 0.0f,
		.maxLod = 0.0f,
	}, NULL, &r->material_texture_sampler);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.atmo_lut_layout,
	}, &r->data.atmo_lut_descriptor_set));

	for (uint32_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			p->graphics_data = calloc(1, sizeof(struct pshine_planet_graphics_data));
			// PSHINE_MEASURE("atmosphere LUT gen", init_atmo_lut_compute(r, p));
			init_atmo_lut_compute(r, p);
			compute_atmo_lut(r, p, true);
			load_planet_texture(r, p);
			p->graphics_data->mesh_ref = r->sphere_mesh;
			p->graphics_data->uniform_buffer = allocate_buffer(
				r,
				&(struct vulkan_buffer_alloc_info){
					.size = get_padded_uniform_buffer_size(r, sizeof(struct static_mesh_uniform_data)) * FRAMES_IN_FLIGHT,
					.buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					.required_memory_property_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
					.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
				}
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
				},
			}, 0, NULL);
		}
	}

	{
		vkUpdateDescriptorSets(r->device, 4, (VkWriteDescriptorSet[4]){
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.atmo_descriptor_set,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = ((struct pshine_planet*)r->game->celestial_bodies_own[0])->graphics_data->atmo_lut.view,
					.sampler = r->atmo_lut_sampler
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.material_descriptor_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = ((struct pshine_planet*)r->game->celestial_bodies_own[0])->graphics_data->surface_albedo.view,
					.sampler = r->material_texture_sampler
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.material_descriptor_set,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = ((struct pshine_planet*)r->game->celestial_bodies_own[0])->graphics_data->surface_bump.view,
					.sampler = r->material_texture_sampler
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.material_descriptor_set,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = ((struct pshine_planet*)r->game->celestial_bodies_own[0])->graphics_data->surface_specular.view,
					.sampler = r->material_texture_sampler
				},
			},
		}, 0, NULL);
	}
}

static const VkExtent2D atmo_lut_extent = { 1024, 1024 };
static const VkFormat atmo_lut_format = VK_FORMAT_R32G32_SFLOAT;

static void compute_atmo_lut(struct vulkan_renderer *r, struct pshine_planet *planet, bool first_time) {
	VkCommandBuffer cmdbuf = planet->graphics_data->compute_cmdbuf;

	// We could do better sync here, but there's not much point in doing that since we only compute the LUTs once.
	CHECKVK(vkQueueWaitIdle(r->queues[QUEUE_GRAPHICS]));
	CHECKVK(vkQueueWaitIdle(r->queues[QUEUE_COMPUTE])); // handle multiple calls to this function, although we shouldn't get them.

	CHECKVK(vkResetCommandBuffer(cmdbuf, 0));
	CHECKVK(vkBeginCommandBuffer(cmdbuf, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = 0,
	}));

	// if (r->queue_families[QUEUE_COMPUTE] == r->queue_families[QUEUE_GRAPHICS]) {
	vkCmdPipelineBarrier(
		cmdbuf,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = planet->graphics_data->atmo_lut.image,
			.srcAccessMask = 0, // VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = first_time
				? VK_IMAGE_LAYOUT_UNDEFINED
				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
			.dstQueueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		}
	);
	// }

	vkCmdBindDescriptorSets(
		cmdbuf,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		r->pipelines.atmo_lut_layout,
		0, 1,
		&r->data.atmo_lut_descriptor_set,
		0, NULL
	);
	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.atmo_lut_pipeline);
	
	double scs_atmo_h = SCSd_WCSd(planet->atmosphere.height);
	double scs_body_r = SCSd_WCSd(planet->as_body.radius);
	double scs_body_r_scaled = scs_body_r / (scs_body_r + scs_atmo_h);

	struct atmo_lut_push_const_data data = {
		.planet_radius = scs_body_r_scaled,
		.atmo_height = 1.0f - scs_body_r_scaled,
		.falloffs = float2xy(
			planet->atmosphere.rayleigh_falloff,
			planet->atmosphere.mie_falloff
		),
		.samples = 4096,
	};

	vkCmdPushConstants(cmdbuf, r->pipelines.atmo_lut_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(struct atmo_lut_push_const_data), &data);

	vkCmdDispatch(cmdbuf, atmo_lut_extent.width / 16, atmo_lut_extent.height / 16, 1);

	vkCmdPipelineBarrier(
		cmdbuf,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		0, 0, NULL, 0, NULL, 1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = planet->graphics_data->atmo_lut.image,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_NONE,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
			.dstQueueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		}
	);
	CHECKVK(vkEndCommandBuffer(cmdbuf));

	vkQueueSubmit(r->queues[QUEUE_COMPUTE], 1, &(VkSubmitInfo){
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmdbuf
	}, VK_NULL_HANDLE);

	vkQueueWaitIdle(r->queues[QUEUE_COMPUTE]);
}

static struct vulkan_image load_texture_from_file(struct vulkan_renderer *r, const char *fpath, VkFormat format, int format_channels) {
	int width = 0, height = 0, channels = 0;
	void *data = stbi_load(fpath, &width, &height, &channels, format_channels);
	if (data == NULL) PSHINE_PANIC("failed to load surface texture");
	struct vulkan_image img = allocate_image(r, &(struct vulkan_image_alloc_info){
		.allocation_flags = 0,
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = 0,
		.required_memory_property_flags = 0,
		.out_allocation_info = NULL,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.extent = (VkExtent3D){ .width = width, .height = height, .depth = 1 },
			.format = format,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		},
	});
	NAME_VK_OBJECT(r, img.image, VK_OBJECT_TYPE_IMAGE, "%s image", fpath);
	NAME_VK_OBJECT(r, img.view, VK_OBJECT_TYPE_IMAGE_VIEW, "%s image view", fpath);
	write_to_image_staged(r, &img, (VkExtent3D){
		width,
		height,
		1
	}, format, width * height * format_channels, data);
	stbi_image_free(data);
	return img;
}

static void load_planet_texture(struct vulkan_renderer *r, struct pshine_planet *planet) {
	planet->graphics_data->surface_albedo = load_texture_from_file(r, planet->surface.albedo_texture_path, VK_FORMAT_R8G8B8A8_SRGB, 4);
	// planet->graphics_data->surface_lights = load_texture_from_file(r, planet->surface.lights_texture_path, VK_FORMAT_R8G8B8A8_SRGB, 4);
	planet->graphics_data->surface_bump = load_texture_from_file(r, planet->surface.bump_texture_path, VK_FORMAT_R8_UNORM, 1);
	planet->graphics_data->surface_specular = load_texture_from_file(r, planet->surface.spec_texture_path, VK_FORMAT_R8_UNORM, 1);
}

static void init_atmo_lut_compute(struct vulkan_renderer *r, struct pshine_planet *planet) {
	struct vulkan_image img = allocate_image(r, &(struct vulkan_image_alloc_info){
		.allocation_flags = 0,
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = 0,
		.required_memory_property_flags = 0,
		.out_allocation_info = NULL,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.extent = (VkExtent3D){ .width = atmo_lut_extent.width, .height = atmo_lut_extent.height, .depth = 1 },
			.format = atmo_lut_format,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = atmo_lut_format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		},
	});
	NAME_VK_OBJECT(r, img.image, VK_OBJECT_TYPE_IMAGE, "atmo lut image");
	NAME_VK_OBJECT(r, img.view, VK_OBJECT_TYPE_IMAGE_VIEW, "atmo lut image view");
	planet->graphics_data->atmo_lut = img;

	vkUpdateDescriptorSets(r->device, 1, &(VkWriteDescriptorSet){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.dstArrayElement = 0,
		.dstBinding = 0,
		.dstSet = r->data.atmo_lut_descriptor_set, // TODO: cpu-side sync (thread-safety)
		.pImageInfo = &(VkDescriptorImageInfo){
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.imageView = img.view,
			.sampler = VK_NULL_HANDLE
		}
	}, 0, NULL);

	CHECKVK(vkAllocateCommandBuffers(r->device, &(VkCommandBufferAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandBufferCount = 1,
		.commandPool = r->command_pool_compute,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	}, &planet->graphics_data->compute_cmdbuf));
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
	glfwInitVulkanLoader(vkGetInstanceProcAddr);
	PSHINE_DEBUG("GLFW version: %s", glfwGetVersionString());
#if !defined(_WIN32) && !defined(__APPLE__)
	if (pshine_check_has_option("-x11"))
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	else
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif
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

#ifdef __linux__
		have_portability_ext = false;
#endif

	uint32_t ext_count = extension_count_glfw + 1 + have_portability_ext;
	const char **extensions = calloc(ext_count, sizeof(const char *));
	memcpy(extensions, extensions_glfw, sizeof(const char *) * extension_count_glfw);
	extensions[extension_count_glfw + 0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	// extensions[extension_count_glfw + 1] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
	if (have_portability_ext)
		extensions[extension_count_glfw + 1] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

	CHECKVK(vkCreateInstance(&(VkInstanceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.flags = have_portability_ext ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
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
		PSHINE_DEBUG("physical_device_count=%u", physical_device_count);
		VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
		PSHINE_DEBUG("physical_devices=%p", physical_devices);
		PSHINE_DEBUG("r->instance=%p, vkEnumeratePhysicalDevices=%p", r->instance, vkEnumeratePhysicalDevices);
		CHECKVK(vkEnumeratePhysicalDevices(r->instance, &physical_device_count, physical_devices));
		r->physical_device = physical_devices[0]; // TODO: physical device selection
		free(physical_devices);
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
			if (properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				r->queue_families[QUEUE_COMPUTE] = i;
			}
			VkBool32 is_surface_supported = VK_FALSE;
			CHECKVK(vkGetPhysicalDeviceSurfaceSupportKHR(r->physical_device, i, r->surface, &is_surface_supported));
			if (is_surface_supported) {
				r->queue_families[QUEUE_PRESENT] = i;
			}
		}

		r->physical_device_properties_own = calloc(1, sizeof(*r->physical_device_properties_own));
		r->physical_device_properties_own->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		vkGetPhysicalDeviceProperties2(r->physical_device, r->physical_device_properties_own);

		r->physical_device_features_own = calloc(1, sizeof(*r->physical_device_features_own));
		vkGetPhysicalDeviceFeatures(r->physical_device, r->physical_device_features_own);
	}

	{
		static_assert(QUEUE_FAMILY_COUNT_ == 3);
		uint32_t queue_create_info_count = QUEUE_FAMILY_COUNT_;
		uint32_t unique_queue_family_indices[3] = {
			r->queue_families[0],
			r->queue_families[1],
			r->queue_families[3],
		};
		if (r->queue_families[0] == r->queue_families[1]) {
			queue_create_info_count = 2;
			unique_queue_family_indices[0] = r->queue_families[0];
			unique_queue_family_indices[1] = r->queue_families[2];
			if (r->queue_families[1] == r->queue_families[2])
				queue_create_info_count = 1;
		} else if (r->queue_families[0] == r->queue_families[2]) {
			queue_create_info_count = 2;
			unique_queue_family_indices[0] = r->queue_families[0];
			unique_queue_family_indices[1] = r->queue_families[1];
			if (r->queue_families[1] == r->queue_families[2])
				queue_create_info_count = 1;
		} else if (r->queue_families[1] == r->queue_families[2]) {
			queue_create_info_count = 2;
			unique_queue_family_indices[0] = r->queue_families[0];
			unique_queue_family_indices[1] = r->queue_families[1];
			if (r->queue_families[0] == r->queue_families[1])
				queue_create_info_count = 1;
		}
		VkDeviceQueueCreateInfo queue_create_infos[queue_create_info_count];

		float queuePriority = 1.0f;
		for (uint32_t i = 0; i < queue_create_info_count; ++i) {
			PSHINE_DEBUG("VkDeviceQueueCreateInfo[%d] -> %d", i, unique_queue_family_indices[i]);
			queue_create_infos[i] = (VkDeviceQueueCreateInfo){
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.pQueuePriorities = &queuePriority,
				.queueCount = 1,
				.queueFamilyIndex = unique_queue_family_indices[i]
			};
		}

		PSHINE_INFO("have_portabiliy_ext: %d", have_portability_ext);
		// TODO: Check if device actually supports VK_KHR_portability_subset!

		CHECKVK(vkCreateDevice(r->physical_device, &(VkDeviceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = queue_create_info_count,
			.pQueueCreateInfos = queue_create_infos,
			.enabledExtensionCount = 1 + have_portability_ext,
			.ppEnabledExtensionNames = (const char *[]){
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				"VK_KHR_portability_subset",
				// VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
			},
			.pEnabledFeatures = &(VkPhysicalDeviceFeatures){}
		}, NULL, &r->device));
	}

	volkLoadDevice(r->device);

	vkGetDeviceQueue(r->device, r->queue_families[QUEUE_GRAPHICS], 0, &r->queues[QUEUE_GRAPHICS]);
	vkGetDeviceQueue(r->device, r->queue_families[QUEUE_PRESENT], 0, &r->queues[QUEUE_PRESENT]);
	vkGetDeviceQueue(r->device, r->queue_families[QUEUE_COMPUTE], 0, &r->queues[QUEUE_COMPUTE]);

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
	free(r->physical_device_features_own);

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
	enum : uint32_t {
		output_attachment_index,
		transient_color_attachment_index,
		transient_depth_attachment_index,
		attachment_count,
	};

	enum : uint32_t {
		geometry_subpass_index,
		composite_subpass_index,
		imgui_subpass_index,
		subpass_count,
	};

	VkSubpassDependency subpass_dependencies[] = {
		// synchronize previous geometry_subpass transient_color_attachment write
		// before current geometry_subpass transient_color_attachment write.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = geometry_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
		// synchronize previous geometry_subpass transient_depth_attachment write
		// before current geometry_subpass transient_depth_attachment clear.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = geometry_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, // earlier write
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, // WRITE because loadOp is CLEAR
		},
		// synchronize previous composite_subpass output_attachment write and read
		// before current composite_subpass output_attachment write.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = composite_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, //  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, // VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, // earlier write/read
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // current write
		},
		// synchronize composite_subpass output_attachment write
		// before imgui_subpass output_attachment write.
		(VkSubpassDependency){
			.srcSubpass = composite_subpass_index,
			.dstSubpass = imgui_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // earlier write
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // current write
		},
		// synchronize geometry_subpass transient_color_attachment write
		// before composite_subpass transient_color_attachment (as input attachment) read.
		// synchronize geometry_subpass transient_depth_attachment write
		// before composite_subpass transient_depth_attachment (as input attachment) read
		(VkSubpassDependency){
			.srcSubpass = geometry_subpass_index,
			.dstSubpass = composite_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask
				= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
	};

	CHECKVK(vkCreateRenderPass(r->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.subpassCount = subpass_count,
		.pSubpasses = (VkSubpassDescription[]){
			[geometry_subpass_index] = (VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.pDepthStencilAttachment = &(VkAttachmentReference){
					.attachment = transient_depth_attachment_index,
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
				}
			},
			[composite_subpass_index] = (VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = output_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.inputAttachmentCount = 2,
				.pInputAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_depth_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
				}
			},
			[imgui_subpass_index] = (VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = output_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
			}
		},
		.attachmentCount = attachment_count,
		.pAttachments = (VkAttachmentDescription[]){
			[output_attachment_index] = (VkAttachmentDescription){
				.format = r->surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			[transient_color_attachment_index] = (VkAttachmentDescription){
				.format = VK_FORMAT_R8G8B8A8_SRGB,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
			[transient_depth_attachment_index] = (VkAttachmentDescription){
				.format = r->depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		},
		.dependencyCount = sizeof(subpass_dependencies) / sizeof(subpass_dependencies[0]),
		.pDependencies = subpass_dependencies,
	}, NULL, &r->render_passes.main_pass));
	NAME_VK_OBJECT(r, r->render_passes.main_pass, VK_OBJECT_TYPE_RENDER_PASS, "main render pass");
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
		// .allocation_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
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
				// | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
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
		// .allocation_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
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
				// | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
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
	NAME_VK_OBJECT(r, r->transients.color_0.image, VK_OBJECT_TYPE_IMAGE, "transient color0 image");
	NAME_VK_OBJECT(r, r->transients.color_0.view, VK_OBJECT_TYPE_IMAGE_VIEW, "transient color0 image view");

	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {

		CHECKVK(vkCreateFramebuffer(r->device, &(VkFramebufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.attachmentCount = 3,
			.pAttachments = (VkImageView[]){
				r->swapchain_image_views_own[i],
				r->transients.color_0.view,
				r->depth_image.view,
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

struct graphics_pipeline_info {
	const char *vert_fname, *frag_fname;
	uint32_t push_constant_range_count;
	const VkPushConstantRange *push_constant_ranges;
	uint32_t set_layout_count;
	const VkDescriptorSetLayout *set_layouts;
	VkPolygonMode polygon_mode;
	VkRenderPass render_pass;
	uint32_t subpass;
	const char *layout_name;
	const char *pipeline_name;
	bool depth_test;
	bool blend;
	bool vertex_input; // TODO: pass vertex attribute here
};

struct vulkan_pipeline {
	VkPipelineLayout layout;
	VkPipeline pipeline;
};

static struct vulkan_pipeline create_graphics_pipeline(struct vulkan_renderer *r, const struct graphics_pipeline_info *info) {
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

	PSHINE_DEBUG("info->push_constant_range_count=%u", info->push_constant_range_count);
	
	if (info->layout_name != NULL)
		NAME_VK_OBJECT(r, layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, info->layout_name);

	CHECKVK(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &(VkGraphicsPipelineCreateInfo){
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = info->vertex_input ? 1 : 0,
			.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
				.binding = 0,
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
				.stride = sizeof(struct pshine_planet_vertex)
			},
			.vertexAttributeDescriptionCount = info->vertex_input ? 3 : 0,
			.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]){
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32G32B32_SFLOAT,
					.location = 0,
					.offset = offsetof(struct pshine_planet_vertex, position)
				},
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32G32_SFLOAT,
					.location = 1,
					.offset = offsetof(struct pshine_planet_vertex, normal_oct)
				},
				(VkVertexInputAttributeDescription){
					.binding = 0,
					.format = VK_FORMAT_R32_SFLOAT,
					.location = 2,
					.offset = offsetof(struct pshine_planet_vertex, tangent_dia)
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
			.polygonMode = info->polygon_mode,
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
			.depthWriteEnable = info->depth_test ? VK_TRUE : VK_FALSE,
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
		NAME_VK_OBJECT(r, pipeline, VK_OBJECT_TYPE_PIPELINE, info->pipeline_name);

	vkDestroyShaderModule(r->device, vert_shader_module, NULL);
	vkDestroyShaderModule(r->device, frag_shader_module, NULL);

	return (struct vulkan_pipeline){ .pipeline = pipeline, .layout = layout };
}

static void init_pipelines(struct vulkan_renderer *r) {
	struct vulkan_pipeline mesh_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/mesh.vert.spv",
		.frag_fname = "build/pshine/data/mesh.frag.spv",
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
		.vertex_input = true,
		.layout_name = "static mesh pipeline layout",
		.pipeline_name = "static mesh pipeline"
	});
	r->pipelines.mesh_pipeline = mesh_pipeline.pipeline;
	r->pipelines.mesh_layout = mesh_pipeline.layout;
	struct vulkan_pipeline atmo_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/atmo.vert.spv",
		.frag_fname = "build/pshine/data/atmo.frag.spv",
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
		.vertex_input = false,
		.layout_name = "atmosphere pipeline layout",
		.pipeline_name = "atmosphere pipeline"
	});
	r->pipelines.atmo_pipeline = atmo_pipeline.pipeline;
	r->pipelines.atmo_layout = atmo_pipeline.layout;

	{
		vkCreatePipelineLayout(r->device, &(VkPipelineLayoutCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &r->descriptors.atmo_lut_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = &(VkPushConstantRange){
				.offset = 0,
				.size = sizeof(struct atmo_lut_push_const_data),
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			}
		}, NULL, &r->pipelines.atmo_lut_layout);

		VkShaderModule comp_shader_module = create_shader_module_file(r, "build/pshine/data/atmo_lut.comp.spv");
		vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.layout = r->pipelines.atmo_lut_layout,
			.stage = (VkPipelineShaderStageCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pSpecializationInfo = NULL,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = comp_shader_module,
				.pName = "main",
			},
		}, NULL, &r->pipelines.atmo_lut_pipeline);
		NAME_VK_OBJECT(r, r->pipelines.atmo_lut_pipeline, VK_OBJECT_TYPE_PIPELINE, "atmo lut pipeline");
		vkDestroyShaderModule(r->device, comp_shader_module, NULL);
	}
}

static void deinit_pipelines(struct vulkan_renderer *r) {
	vkDestroyPipeline(r->device, r->pipelines.mesh_pipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelines.mesh_layout, NULL);
	vkDestroyPipeline(r->device, r->pipelines.atmo_pipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelines.atmo_layout, NULL);
	vkDestroyPipeline(r->device, r->pipelines.atmo_lut_pipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelines.atmo_lut_layout, NULL);
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
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		// TODO: make sure that this flag is removed if doing per frame compute
		// maybe create another pool for transient compute?
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
	}, NULL, &r->command_pool_compute));
}

static void deinit_cmdbufs(struct vulkan_renderer *r) {
	vkDestroyCommandPool(r->device, r->command_pool_graphics, NULL);
	vkDestroyCommandPool(r->device, r->command_pool_transfer, NULL);
	vkDestroyCommandPool(r->device, r->command_pool_compute, NULL);
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
		.poolSizeCount = 5,
		.pPoolSizes = (VkDescriptorPoolSize[]){
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC },
			(VkDescriptorPoolSize){ .descriptorCount = 64, .type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT },
			(VkDescriptorPoolSize){ .descriptorCount = 128, .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
			(VkDescriptorPoolSize){ .descriptorCount = 64, .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE },
		}
	}, NULL, &r->descriptors.pool);
	NAME_VK_OBJECT(r, r->descriptors.pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "descriptor pool 1");
	vkCreateDescriptorPool(r->device, &(VkDescriptorPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1024,
		.poolSizeCount = 1,
		.pPoolSizes = (VkDescriptorPoolSize[]){
			(VkDescriptorPoolSize){ .descriptorCount = 256, .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
		}
	}, NULL, &r->descriptors.pool_imgui);
	NAME_VK_OBJECT(r, r->descriptors.pool_imgui, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "descriptor pool 2 (imgui)");

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
	NAME_VK_OBJECT(r, r->descriptors.global_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "global descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 4,
		.pBindings = (VkDescriptorSetLayoutBinding[4]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 2,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 3,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, NULL, &r->descriptors.material_layout);
	NAME_VK_OBJECT(r, r->descriptors.material_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "material descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	}, NULL, &r->descriptors.static_mesh_layout);
	NAME_VK_OBJECT(r, r->descriptors.static_mesh_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "static mesh descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 4,
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
			(VkDescriptorSetLayoutBinding){
				.binding = 3,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, NULL, &r->descriptors.atmo_layout);
	NAME_VK_OBJECT(r, r->descriptors.atmo_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "atmosphere descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		}
	}, NULL, &r->descriptors.atmo_lut_layout);
	NAME_VK_OBJECT(r, r->descriptors.atmo_lut_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "atmosphere lut descriptor set layout");
}

static void deinit_descriptors(struct vulkan_renderer *r) {
	vkDestroyDescriptorPool(r->device, r->descriptors.pool, NULL);
	vkDestroyDescriptorPool(r->device, r->descriptors.pool_imgui, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.global_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.material_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.static_mesh_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.atmo_layout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.atmo_lut_layout, NULL);
}


// Game data

static void init_data(struct vulkan_renderer *r) {
	struct vulkan_buffer_alloc_info common_alloc_info = {
		.size = 0,
		.buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.required_memory_property_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.memory_usage = VMA_MEMORY_USAGE_AUTO, // _PREFER_DEVICE
	};

	common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * FRAMES_IN_FLIGHT;
	r->data.global_uniform_buffer = allocate_buffer(r, &common_alloc_info);
	NAME_VK_OBJECT(r, r->data.global_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "global ub");

	common_alloc_info.size = sizeof(struct material_uniform_data);
	r->data.material_uniform_buffer = allocate_buffer(r, &common_alloc_info);
	NAME_VK_OBJECT(r, r->data.material_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "material ub");

	common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * FRAMES_IN_FLIGHT;
	r->data.atmo_uniform_buffer = allocate_buffer(r, &common_alloc_info);
	NAME_VK_OBJECT(r, r->data.atmo_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "atmosphere ub");

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.global_layout
	}, &r->data.global_descriptor_set));
	NAME_VK_OBJECT(r, r->data.global_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "global ds");

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.material_layout
	}, &r->data.material_descriptor_set));
	NAME_VK_OBJECT(r, r->data.material_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "material ds");

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.atmo_layout
	}, &r->data.atmo_descriptor_set));
	NAME_VK_OBJECT(r, r->data.atmo_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "atmosphere ds");

	vkUpdateDescriptorSets(r->device, 5, (VkWriteDescriptorSet[5]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = r->data.atmo_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = r->data.atmo_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct atmo_uniform_data)
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.atmo_descriptor_set,
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
			.dstSet = r->data.atmo_descriptor_set,
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
			.dstSet = r->data.material_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = r->data.material_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct material_uniform_data)
			}
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = r->data.global_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = r->data.global_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct global_uniform_data)
			}
		}
	}, 0, NULL);
}

static void init_frame(
	struct vulkan_renderer *r,
	uint32_t frame_index,
	struct per_frame_data *f,
	size_t planet_count
) {
	CHECKVK(vkAllocateCommandBuffers(r->device, &(VkCommandBufferAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->command_pool_graphics,
		.commandBufferCount = 1,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY
	}, &f->command_buffer));
	NAME_VK_OBJECT(r, f->command_buffer, VK_OBJECT_TYPE_COMMAND_BUFFER, "cmdbuf for frame %u", frame_index);

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
	vkDestroyFence(r->device, f->sync.in_flight_fence, NULL);
	vkDestroySemaphore(r->device, f->sync.render_finish_semaphore, NULL);
	vkDestroySemaphore(r->device, f->sync.image_avail_semaphore, NULL);
}

static void init_frames(struct vulkan_renderer *r) {
	init_data(r);
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

	deallocate_buffer(r, r->data.global_uniform_buffer);
	deallocate_buffer(r, r->data.material_uniform_buffer);
	deallocate_buffer(r, r->data.atmo_uniform_buffer);
}

// ImGui

static void check_vk_result_imgui(VkResult res) { CHECKVK(res); }
static PFN_vkVoidFunction vulkan_loader_func_imgui(const char *name, void *user) {
	struct vulkan_renderer *r = user;
	PFN_vkVoidFunction instanceAddr = vkGetInstanceProcAddr(r->instance, name);
	PFN_vkVoidFunction deviceAddr = vkGetDeviceProcAddr(r->device, name);
	return deviceAddr ? deviceAddr : instanceAddr;
}

static void init_imgui(struct vulkan_renderer *r) {
	ImGuiContext *ctx = ImGui_CreateContext(NULL);
	ImGui_SetCurrentContext(ctx);
	ImGuiIO *io = ImGui_GetIO();
	io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	ImGui_StyleColorsDark(NULL);

	cImGui_ImplVulkan_LoadFunctionsEx(&vulkan_loader_func_imgui, r);
	cImGui_ImplGlfw_InitForVulkan(r->window, true);
	cImGui_ImplVulkan_Init(&(ImGui_ImplVulkan_InitInfo){
		.Instance = r->instance,
		.PhysicalDevice = r->physical_device,
		.Device = r->device,
		.QueueFamily = r->queue_families[QUEUE_GRAPHICS],
		.Queue = r->queues[QUEUE_GRAPHICS],
		.PipelineCache = VK_NULL_HANDLE,
		.DescriptorPool = r->descriptors.pool_imgui,
		.Subpass = 2,
		.MinImageCount = FRAMES_IN_FLIGHT,
		.ImageCount = FRAMES_IN_FLIGHT,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Allocator = NULL,
		.CheckVkResultFn = &check_vk_result_imgui,
		.RenderPass = r->render_passes.main_pass
	});
}

static void deinit_imgui(struct vulkan_renderer *r) {
	cImGui_ImplVulkan_Shutdown();
	cImGui_ImplGlfw_Shutdown();
	ImGui_DestroyContext(ImGui_GetCurrentContext());
}


void pshine_deinit_renderer(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	
	vkDeviceWaitIdle(r->device);

	for (uint32_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			deallocate_buffer(r, p->graphics_data->uniform_buffer);
			deallocate_image(r, p->graphics_data->atmo_lut);
			deallocate_image(r, p->graphics_data->surface_albedo);
			deallocate_image(r, p->graphics_data->surface_bump);
			deallocate_image(r, p->graphics_data->surface_specular);
			vkFreeCommandBuffers(r->device, r->command_pool_compute, 1, &p->graphics_data->compute_cmdbuf);
			free(p->graphics_data);
		}
	}

	vkDestroySampler(r->device, r->atmo_lut_sampler, NULL);
	vkDestroySampler(r->device, r->material_texture_sampler, NULL);

	destroy_mesh(r, r->sphere_mesh);

	deinit_imgui(r);
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


MATH_FN_ void setdouble4x4trans(double4x4 *m, double3 s) {
	setdouble4x4iden(m);
	m->v4s[3] = double4xyz3w(s, 1);
}

MATH_FN_ void setdouble4x4scale(double4x4 *m, double3 s) {
	memset(m->vs, 0, sizeof m->vs);
	m->vs[0][0] = s.x;
	m->vs[1][1] = s.y;
	m->vs[1][1] = s.y;
	m->vs[2][2] = s.z;
	m->vs[3][3] = 1.0;
}

static void do_frame(struct vulkan_renderer *r, uint32_t current_frame, uint32_t image_index) {
	struct per_frame_data *f = &r->frames[current_frame];

	{
		struct material_uniform_data new_data = {
			.color = float4rgba(0.8f, 0.3f, 0.1f, 1.0f),
			.view_dir = float3_double3(double3vs(r->game->camera_forward.values)),
			.smoothness = r->game->material_smoothness_,
		};
		struct material_uniform_data *data;
		vmaMapMemory(r->allocator, r->data.material_uniform_buffer.allocation, (void**)&data);
		memcpy(data, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, r->data.material_uniform_buffer.allocation);
	}

	double aspect_ratio = r->swapchain_extent.width /(double) r->swapchain_extent.height;
	double4x4 view_mat = {};
	setdouble4x4iden(&view_mat);
	setdouble4x4lookat(
		&view_mat,
		SCSd3_WCSp3(r->game->camera_position),
		double3add(SCSd3_WCSp3(r->game->camera_position), double3vs(r->game->camera_forward.values)),
		double3xyz(0.0, 1.0, 0.0)
	);
	// float4x4trans(&view_mat, float3neg(float3vs(r->game->camera_position.values)));
	double4x4 proj_mat = {};
	struct double4x4persp_info persp_info = setdouble4x4persp(&proj_mat, 60.0, aspect_ratio, 0.01);

	{
		float3 cam_y = float3xyz(0.0f, 1.0f, 0.0f);
		float3 cam_z = float3_double3(double3norm(double3vs(r->game->camera_forward.values)));
		float3 cam_x = float3norm(float3cross(cam_y, cam_z));
		cam_y = float3norm(float3cross(cam_z, cam_x));
		struct global_uniform_data new_data = {
			.camera = float4xyz3w(float3_double3(SCSd3_WCSp3(r->game->camera_position)), persp_info.znear),
			.camera_right = float4xyz3w(cam_x, persp_info.plane.x),
			.camera_up = float4xyz3w(cam_y, persp_info.plane.y),
			.sun = float4xyz3w(float3_double3(
				double3norm(double3vs(r->game->sun_direction_.values))
			), 1.0f),
		};
		char *data;
		vmaMapMemory(r->allocator, r->data.global_uniform_buffer.allocation, (void**)&data);
		data += get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame;
		memcpy(data, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, r->data.global_uniform_buffer.allocation);
	}

	for (size_t i = 0; i < r->game->celestial_body_count; ++i) {
		struct pshine_celestial_body *b = r->game->celestial_bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			struct static_mesh_uniform_data new_data = {};
			double4x4 model_mat = {};
			setdouble4x4iden(&model_mat);
			double4x4 model_rot_mat = {0};

			{
				double a = p->as_body.rotation, c = cosf(a), s = sinf(a), C = 1 - c;
				double3 axis = double3norm(double3vs(p->as_body.rotation_axis.values));

				model_rot_mat.vs[0][0] = c + C * axis.x * axis.x;
				model_rot_mat.vs[0][1] = C * axis.x * axis.y + s * axis.z;
				model_rot_mat.vs[0][2] = C * axis.x * axis.z - s * axis.y;
				model_rot_mat.vs[0][3] = 0.0f;
				model_rot_mat.vs[1][0] = C * axis.y * axis.x - s * axis.z;
				model_rot_mat.vs[1][1] = c + C * axis.y * axis.y;
				model_rot_mat.vs[1][2] = C * axis.y * axis.z + s * axis.x;
				model_rot_mat.vs[1][3] = 0.0f;
				model_rot_mat.vs[2][0] = C * axis.z * axis.x + s * axis.y;
				model_rot_mat.vs[2][1] = C * axis.z * axis.y - s * axis.x;
				model_rot_mat.vs[2][2] = c + C * axis.z * axis.z;
				model_rot_mat.vs[2][3] = 0.0f;
				model_rot_mat.vs[3][0] = 0.0f;
				model_rot_mat.vs[3][1] = 0.0f;
				model_rot_mat.vs[3][2] = 0.0f;
				model_rot_mat.vs[3][3] = 1.0f;
				// double4x4mul(&model_mat, &r);
			}

			// double3 pos = SCSd3_WCSp3(p->as_body.position);
			// PSHINE_DEBUG("%.2f %.2f %.2f",
			// 	pos.x,
			// 	pos.y,
			// 	pos.z);
			// setdouble4x4scale

			double4x4 model_scale_mat;
			setdouble4x4scale(&model_scale_mat, double3v(SCSd_WCSd(p->as_body.radius)));

			double4x4 model_trans_mat;
			setdouble4x4trans(&model_trans_mat, SCSd3_WCSp3(p->as_body.position));

			double4x4mul(&model_mat, &model_rot_mat);
			double4x4mul(&model_mat, &model_scale_mat);
			double4x4mul(&model_mat, &model_trans_mat);

			// double4x4trans(&model_mat, (SCSd3_WCSp3(p->as_body.position)));

			// PSHINE_DEBUG("⎡ %f %f %f %f ⎤", model_mat.vs[0][0], model_mat.vs[1][0], model_mat.vs[2][0], model_mat.vs[3][0]);
			// PSHINE_DEBUG("⎢ %f %f %f %f ⎥", model_mat.vs[0][1], model_mat.vs[1][1], model_mat.vs[2][1], model_mat.vs[3][1]);
			// PSHINE_DEBUG("⎢ %f %f %f %f ⎥", model_mat.vs[0][2], model_mat.vs[1][2], model_mat.vs[2][2], model_mat.vs[3][2]);
			// PSHINE_DEBUG("⎣ %f %f %f %f ⎦", model_mat.vs[0][3], model_mat.vs[1][3], model_mat.vs[2][3], model_mat.vs[3][3]);

			new_data.proj = float4x4_double4x4(proj_mat);

			double4x4 model_view_mat = model_mat;
			double4x4mul(&model_view_mat, &view_mat);
			new_data.model_view = float4x4_double4x4(model_view_mat);
			new_data.model = float4x4_double4x4(model_mat);

			char *data_raw;
			vmaMapMemory(r->allocator, p->graphics_data->uniform_buffer.allocation, (void**)&data_raw);
			data_raw += get_padded_uniform_buffer_size(r, sizeof(struct static_mesh_uniform_data)) * current_frame;
			memcpy(data_raw, &new_data, sizeof(new_data));
			vmaUnmapMemory(r->allocator, p->graphics_data->uniform_buffer.allocation);
		}
	}

	{
		struct pshine_planet *p = (void*)r->game->celestial_bodies_own[0];
		
		// float3 wavelengths = float3rgb(
		// 	powf(400.0f / p->atmosphere.wavelengths[0], 4) * p->atmosphere.scattering_strength,
		// 	powf(400.0f / p->atmosphere.wavelengths[1], 4) * p->atmosphere.scattering_strength,
		// 	powf(400.0f / p->atmosphere.wavelengths[2], 4) * p->atmosphere.scattering_strength
		// );

		double scs_atmo_h = SCSd_WCSd(p->atmosphere.height);
		double scs_body_r = SCSd_WCSd(p->as_body.radius);

		double scale_fact = scs_body_r + scs_atmo_h;

		double3 scs_body_pos = SCSd3_WCSp3(p->as_body.position);
		double3 scs_body_pos_scaled = double3div(scs_body_pos, scale_fact);

		double scs_body_r_scaled = scs_body_r / scale_fact;
		double3 scs_cam = SCSd3_WCSp3(r->game->camera_position);
		double3 scs_cam_scaled = double3div(scs_cam, scale_fact);

		struct atmo_uniform_data new_data = {
			.planet = float4xyz3w(
				float3_double3(scs_body_pos_scaled),
				scs_body_r_scaled
			),
			.radius = 1.0f,
			.camera = float4xyz3w(float3_double3(scs_cam_scaled), 0.0f),
			.coefs_ray = float4xyz3w(
				float3vs(p->atmosphere.rayleigh_coefs),
				p->atmosphere.rayleigh_falloff
			),
			.coefs_mie = float4xyzw(
				p->atmosphere.mie_coef,
				p->atmosphere.mie_ext_coef,
				p->atmosphere.mie_g_coef,
				p->atmosphere.mie_falloff
			),
			.optical_depth_samples = 5,
			.scatter_point_samples = 80,
			.intensity = p->atmosphere.intensity,
		};
		char *data_raw;
		vmaMapMemory(r->allocator, r->data.atmo_uniform_buffer.allocation, (void**)&data_raw);
		data_raw += get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame;
		memcpy(data_raw, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, r->data.atmo_uniform_buffer.allocation);
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
		r->pipelines.mesh_layout,
		0,
		2,
		(VkDescriptorSet[]){ r->data.global_descriptor_set, r->data.material_descriptor_set },
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
				r->pipelines.mesh_layout,
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
	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.atmo_pipeline);
	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.atmo_layout,
		2,
		1,
		&r->data.atmo_descriptor_set,
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame
		}
	);
	vkCmdDraw(f->command_buffer, 3, 1, 0, 0);

	vkCmdNextSubpass(f->command_buffer, VK_SUBPASS_CONTENTS_INLINE);
	cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), f->command_buffer);

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

struct renderer_stats {
	float fps;
	float delta_time;
	float avg_delta_time;
	size_t frame_number;
	struct {
		float total_usage; // MiB
		size_t allocation_count;
	} gpu_memory;
	float cpu_memory_used;
};

static void show_stats_window(struct vulkan_renderer *r, const struct renderer_stats *stats) {
	if (ImGui_Begin("Stats", NULL, 0)) {
		ImGui_Text("FPS: %.1f", stats->fps);
		ImGui_Text("Delta Time: %.2fms", stats->delta_time * 1000.0f);
		ImGui_Text("Avg. Delta Time: %.2fms", stats->avg_delta_time * 1000.0f);
		ImGui_Text("Frame: %zu", stats->frame_number);
		ImGui_Text("CPU Memory (MiB): %.2f", stats->cpu_memory_used);
		ImGui_Text("GPU Memory (MiB): %.2f", stats->gpu_memory.total_usage);
		ImGui_Text("GPU Allocation Count: %zu", stats->gpu_memory.allocation_count);
	}
	ImGui_End();
}

static void show_utils_window(struct vulkan_renderer *r) {
	if (ImGui_Begin("Utils", NULL, 0)) {
		ImGui_BeginDisabled(r->currently_recomputing_luts);
		if (ImGui_Button("Recompute Atmo LUTs")) {
			compute_atmo_lut(r, (void*)r->game->celestial_bodies_own[0], false);
		}
		ImGui_EndDisabled();
	}
	ImGui_End();
}

static void set_gpu_mem_usage(struct vulkan_renderer *r, struct renderer_stats *stats) {
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(r->allocator, budgets);
	stats->gpu_memory.total_usage = (size_t)(budgets[0].usage / 1024) / 1024.0f;
	stats->gpu_memory.allocation_count = budgets[0].statistics.allocationCount;
}

void pshine_main_loop(struct pshine_game *game, struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	
	float last_time = glfwGetTime();
	uint32_t current_frame = 0;
	size_t frame_number = 0, frames_since_dt_reset = 0;
	
	float delta_time_sum = 0.0f;

	while (!glfwWindowShouldClose(r->window)) {
		++frame_number; ++frames_since_dt_reset;
		float current_time = glfwGetTime();
		float delta_time = current_time - last_time;
		glfwPollEvents();
		cImGui_ImplVulkan_NewFrame();
		cImGui_ImplGlfw_NewFrame();
		ImGui_NewFrame();
		ImGui_ShowDemoWindow(NULL);
		pshine_update_game(game, delta_time);
		{
			struct renderer_stats stats = {
				.frame_number = frame_number,
				.avg_delta_time = delta_time_sum / frames_since_dt_reset,
				.delta_time = delta_time,
				.cpu_memory_used = pshine_get_mem_usage() / 1024.0f,
				.gpu_memory = {},
				.fps = 1.0f / delta_time,
			};
			set_gpu_mem_usage(r, &stats);
			show_stats_window(r, &stats);
		}
		show_utils_window(r);
		ImGui_Render();
		render(r, current_frame);
		if (delta_time_sum >= 20.0f) {
			delta_time_sum = 0.0f;
			frames_since_dt_reset = 0;
		}
		delta_time_sum += delta_time;
		last_time = current_time;
		current_frame = (current_frame + 1) % FRAMES_IN_FLIGHT;
	}
}

const uint8_t *pshine_get_key_states(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	return r->key_states;
}
