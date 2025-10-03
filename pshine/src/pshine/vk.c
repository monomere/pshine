#include <pshine/game.h>
#include <pshine/util.h>
#include <stdio.h>
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
#include <stb_image.h>
// TODO: weird include guard error. included in vk_util.h
// #include <cgltf.h>

// #include <giraffe/giraffe.h>

#include "vk_util.h"
#include "math.h"
#include "vertex_util.h"

#define SCSd3_WCSd3(wcs) (double3mul((wcs), PSHINE_SCS_FACTOR))
#define SCSd3_WCSp3(wcs) SCSd3_WCSd3(double3vs((wcs).values))
#define SCSd_WCSd(wcs) ((wcs) * PSHINE_SCS_FACTOR)

#define BLOOM_STAGE_COUNT 8

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
	float4x4 inv_view;
	float4x4 inv_proj;
	float4x4 local_view;
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
	float3 sun;
	float scale_factor;
};

struct planet_material_uniform_data {
	float4 color;
	float3 view_dir;
	float smoothness;
};

struct std_material_uniform_data {
	float3 view_dir;
};

struct planet_mesh_uniform_data {
	float4x4 proj;
	float4x4 model_view;
	float4x4 model;
	float4 sun;
};

struct std_mesh_uniform_data {
	float4x4 proj;
	float4x4 model_view;
	float4x4 model;
	float4x4 unscaled_model;
	float4 sun;
	float4 rel_cam_pos;
};

struct rings_uniform_data {
	float4x4 proj;
	float4x4 model_view;
	float4 sun;
	float inner_radius;
	float outer_radius;
	float rel_planet_radius;
	float shadow_smoothing;
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
	uint32_t width, height;
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

struct pshine_star_graphics_data {
	struct vulkan_buffer uniform_buffer;
	struct vulkan_buffer rings_uniform_buffer;
	// struct vulkan_image atmo_lut;
	// struct vulkan_image surface_albedo;
	// struct vulkan_image surface_lights;
	// struct vulkan_image surface_specular;
	// struct vulkan_image surface_bump;
	VkDescriptorSet descriptor_set;
	VkDescriptorSet rings_descriptor_set;
	// VkCommandBuffer compute_cmdbuf;
	// bool should_submit_compute;
};

struct pshine_planet_graphics_data {
	// struct vulkan_mesh *mesh_ref;
	struct vulkan_buffer uniform_buffer;
	struct vulkan_buffer atmo_uniform_buffer;
	struct vulkan_buffer material_uniform_buffer;
	/// Note: Can be uninitialized if there are no rings.
	struct vulkan_buffer rings_uniform_buffer;
	struct vulkan_image atmo_lut;
	struct vulkan_image surface_albedo;
	struct vulkan_image surface_lights;
	struct vulkan_image surface_specular;
	struct vulkan_image surface_bump;
	VkDescriptorSet descriptor_set;
	VkCommandBuffer compute_cmdbuf;
	bool should_submit_compute;
	VkDescriptorSet atmo_descriptor_set;
	VkDescriptorSet atmo_lut_descriptor_set;
	VkDescriptorSet material_descriptor_set;
	/// Note: Can be VK_NULL_HANDLE if there are no rings.
	VkDescriptorSet rings_descriptor_set;
	struct vulkan_image ring_slice_texture;
};

struct vulkan_std_material_images {
	struct vulkan_image diffuse;
	struct vulkan_image emissive;
	struct vulkan_image normal;
	struct vulkan_image ao_metallic_roughness;
};

struct vulkan_std_material {
	struct vulkan_std_material_images images;
	VkDescriptorSet descriptor_set;
	struct vulkan_buffer uniform_buffer;
};

struct vulkan_mesh_model {
	size_t part_count;
	struct vulkan_mesh_model_part {
		size_t material_index;
		struct vulkan_mesh mesh;
	} *parts_own;
	size_t material_count;
	double4x4 transform;
	struct vulkan_std_material *materials_own;
};

static void load_mesh_model_from_gltf(
	struct vulkan_renderer *r,
	const char *fpath,
	struct vulkan_mesh_model *out
);

struct pshine_ship_graphics_data {
	struct vulkan_mesh_model model;
	struct vulkan_buffer uniform_buffer;
	VkDescriptorSet descriptor_set;
};

struct swapchain_image_sync_data {
	VkSemaphore image_avail_semaphore;
	VkSemaphore render_finish_semaphore;
	VkFence in_flight_fence;
};

struct per_frame_data {
	struct swapchain_image_sync_data sync;
	VkCommandBuffer command_buffer;
};

enum gbuffer_images {
	// Diffuse + AO
	GBUFFER_IMAGE_DIFFUSE_O,
	// Normal + Roughness + Metallic
	GBUFFER_IMAGE_NORMAL_R_M,
	GBUFFER_IMAGE_EMISSIVE,
	GBUFFER_IMAGE_COUNT_,
};

struct gbuffer_image {
	const char *name;
	VkFormat format;
	struct vulkan_image image;
};

struct render_pass_transients {
	struct gbuffer_image gbuffer[GBUFFER_IMAGE_COUNT_];
	struct vulkan_image color_0;
	struct vulkan_image bloom[BLOOM_STAGE_COUNT];
};

struct model_store_entry {
	/// Must be `(size_t)-1` on valid entries.
	size_t _alive_marker;
	char *fpath_own;
	struct vulkan_mesh_model model;
};

struct image_store_entry {
	/// Must be `(size_t)-1` on valid entries.
	size_t _alive_marker;
	char *fpath_own;
	struct vulkan_image image;
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
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VkExtent2D swapchain_extent;
	uint32_t swapchain_image_count;
	VkImage *swapchain_images_own; // `*[.swapchain_image_count]`
	VkImageView *swapchain_image_views_own; // `*[.swapchain_image_count]`

	bool opt_bloom;

	bool currently_recomputing_luts;

	bool supports_srgb_no_a_format;
	bool supports_no_a_format;

	VkFormat depth_format;
	struct vulkan_image depth_image;
	struct render_pass_transients transients;

	VkFramebuffer *sdr_framebuffers_own; // `*[.swapchain_image_count]`
	VkFramebuffer *hdr_framebuffers_own; // `*[.swapchain_image_count]`

	VkPhysicalDeviceProperties2 *physical_device_properties_own;
	VkPhysicalDeviceFeatures *physical_device_features_own;

	struct {
		// graphics pipelines
		VkPipelineLayout tri_layout;
		VkPipeline tri_pipeline;
		VkPipelineLayout planet_mesh_layout;
		VkPipeline planet_mesh_pipeline;
		VkPipelineLayout planet_color_mesh_layout;
		VkPipeline planet_color_mesh_pipeline;
		VkPipelineLayout atmo_layout;
		VkPipeline atmo_pipeline;
		VkPipelineLayout line_gizmo_layout;
		VkPipeline line_gizmo_pipeline;
		VkPipelineLayout blit_layout;
		VkPipeline blit_pipeline;
		VkPipelineLayout rings_layout;
		VkPipeline rings_pipeline;
		VkPipelineLayout skybox_layout;
		VkPipeline skybox_pipeline;
		VkPipelineLayout std_mesh_layout;
		VkPipeline std_mesh_pipeline;
		VkPipelineLayout std_mesh_de_layout;
		VkPipeline std_mesh_de_pipeline;
		VkPipelineLayout std_mesh_shadow_layout;
		VkPipeline std_mesh_shadow_pipeline;
		VkPipelineLayout light_layout;
		VkPipeline light_pipeline;

		// compute pipelines
		VkPipelineLayout atmo_lut_layout;
		VkPipeline atmo_lut_pipeline;
		VkPipelineLayout upsample_bloom_layout;
		VkPipeline upsample_bloom_pipeline;
		VkPipelineLayout downsample_bloom_layout;
		VkPipeline downsample_bloom_pipeline;
		VkPipeline first_downsample_bloom_pipeline;
	} pipelines;

	struct {
		VkRenderPass shadow_pass;
		VkRenderPass hdr_pass;
		VkRenderPass sdr_pass;
	} render_passes;

	VkCommandPool command_pool_graphics;
	VkCommandPool command_pool_transfer;
	VkCommandPool command_pool_compute;

	struct {
		VkDescriptorPool pool;
		VkDescriptorPool pool_imgui;
		VkDescriptorSetLayout global_layout;
		VkDescriptorSetLayout planet_material_layout;
		VkDescriptorSetLayout planet_mesh_layout;
		VkDescriptorSetLayout atmo_layout;
		VkDescriptorSetLayout atmo_lut_layout;
		VkDescriptorSetLayout blit_layout;
		VkDescriptorSetLayout light_layout;
		VkDescriptorSetLayout rings_layout;
		VkDescriptorSetLayout skybox_layout;
		VkDescriptorSetLayout upsample_bloom_layout;
		VkDescriptorSetLayout downsample_bloom_layout;
		VkDescriptorSetLayout std_material_layout;
		VkDescriptorSetLayout std_mesh_layout;
	} descriptors;

	struct {
		struct vulkan_buffer global_uniform_buffer;
		VkDescriptorSet global_descriptor_set;
		VkDescriptorSet blit_descriptor_set;
		VkDescriptorSet light_descriptor_set;
		/// `[i]` reads from `i` and writes to `i - 1`.
		VkDescriptorSet upsample_bloom_descriptor_sets[BLOOM_STAGE_COUNT];
		/// `[i]` reads from `i` and writes to `i + 1`.
		VkDescriptorSet downsample_bloom_descriptor_sets[BLOOM_STAGE_COUNT];
		VkDescriptorSet skybox_descriptor_set;
	} data;

	struct per_frame_data frames[FRAMES_IN_FLIGHT];
	// struct swapchain_image_sync_data image_sync_data[FRAMES_IN_FLIGHT];

	VmaAllocator allocator;

	size_t sphere_mesh_count;
	struct vulkan_mesh *own_sphere_meshes;

	VkSampler atmo_lut_sampler;
	VkSampler material_texture_sampler;
	VkSampler skybox_sampler;
	VkSampler bloom_mipmap_sampler;

	struct vulkan_image skybox_image;
	// PSHINE_DYNA_(struct mesh) meshes;

	double lod_ranges[4];

	uint8_t *key_states;
	uint8_t mouse_states[8];
	double2 scroll_delta;

	PSHINE_DYNA_(struct image_store_entry) image_store;
	PSHINE_DYNA_(struct model_store_entry) model_store;
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
static void init_transients(struct vulkan_renderer *r); static void deinit_transients(struct vulkan_renderer *r);
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
	if (info->view_info != nullptr) {
		info->view_info->sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info->view_info->image = img.image;
		CHECKVK(vkCreateImageView(r->device, info->view_info, nullptr, &img.view));
	}
	if (info->sampler_info != nullptr) {
		info->sampler_info->sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	}
	img.width = info->image_info->extent.width;
	img.height = info->image_info->extent.height;
	return img;
}

static void deallocate_image(
	struct vulkan_renderer *r,
	struct vulkan_image image
) {
	vmaDestroyImage(r->allocator, image.image, image.allocation);
	if (image.view != VK_NULL_HANDLE)
		vkDestroyImageView(r->device, image.view, nullptr);
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
	VkOffset3D offset,
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
		0, nullptr,
		0, nullptr,
		1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = target_image->image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = offset.z,
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
			.imageOffset = (VkOffset3D){ offset.x, offset.y, 0, },
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.imageSubresource.mipLevel = 0,
			.imageSubresource.baseArrayLayer = offset.z,
			.imageSubresource.layerCount = 1,
		}
	);
	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &(VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = target_image->image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = offset.z,
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

static void create_mesh(
	struct vulkan_renderer *r,
	const struct pshine_mesh_data *mesh_data,
	struct vulkan_mesh *mesh
) {
	// struct vulkan_mesh *mesh = calloc(1, sizeof(struct vulkan_mesh));
	PSHINE_DEBUG("mesh %zu vertices, %zu indices", mesh_data->index_count, mesh_data->vertex_count);
	mesh->index_buffer = allocate_buffer(
		r,
		&(struct vulkan_buffer_alloc_info){
			.size = mesh_data->index_count * sizeof(uint32_t),
			.buffer_usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
		}
	);

	size_t vertex_size = mesh_data->vertex_type == PSHINE_VERTEX_PLANET
		? sizeof(struct pshine_planet_vertex)
		: sizeof(struct pshine_static_mesh_vertex);

	mesh->vertex_buffer = allocate_buffer(
		r,
		&(struct vulkan_buffer_alloc_info){
			.size = mesh_data->vertex_count * vertex_size,
			.buffer_usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
		}
	);
	write_to_buffer_staged(r, &mesh->index_buffer,
		0, mesh_data->index_count * sizeof(uint32_t), mesh_data->indices);
	write_to_buffer_staged(r, &mesh->vertex_buffer,
		0, mesh_data->vertex_count * vertex_size, mesh_data->vertices);

	mesh->vertex_count = mesh_data->vertex_count;
	mesh->index_count = mesh_data->index_count;
	mesh->vertex_type = mesh_data->vertex_type;
}

static void destroy_mesh(struct vulkan_renderer *r, struct vulkan_mesh *mesh) {
	PSHINE_DEBUG("destroy_mesh: idx_buf=%p, vtx_buf=%p", mesh->vertex_buffer, mesh->index_buffer);
	deallocate_buffer(r, mesh->vertex_buffer);
	deallocate_buffer(r, mesh->index_buffer);
	//free(mesh);
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

enum vulkan_load_texture_flags {
	VULKAN_LOAD_TEXTURE_NORMAL = 0,
	VULKAN_LOAD_TEXTURE_CUBEMAP = 1,
};

static void *load_texture_data_from_file(
	const char *fpath,
	VkFormat format,
	VkExtent2D *size,
	int format_channels,
	int bytes_per_channel
) {
	int channels = 0;
	void *data = nullptr;
	if (bytes_per_channel == 1) {
		data = stbi_load(fpath, (int*)&size->width, (int*)&size->height, &channels, format_channels);
	} else {
		float *fs = stbi_loadf(fpath, (int*)&size->width, (int*)&size->height, &channels, format_channels);
		// Change image to correct format.
		switch (bytes_per_channel) {
			case 4: data = fs; break; // floats are already 4 byte
			case 2: {
				data = calloc(size->width * size->height * format_channels * bytes_per_channel, 1);
				uint16_t *u16s = data;
				if (data == nullptr)
					PSHINE_PANIC("out of memory; tried to alloc %zu",
						size->width * size->height * format_channels * bytes_per_channel);

				if (format != VK_FORMAT_R16G16B16A16_UNORM)
					PSHINE_PANIC("HDR formats other than R16G16B16A16_UNORM are not supported yet.");

				PSHINE_CHECK(format_channels == 4, "format has 4 channels but passed in format_channels != 4");

				for (size_t y = 0; y < size->height * size->height * format_channels; y += size->width * format_channels) {
					for (size_t x = 0; x < size->width * format_channels; x += format_channels) {
						u16s[y + x + 0] = fs[y + x + 0] * UINT16_MAX;
						u16s[y + x + 1] = fs[y + x + 1] * UINT16_MAX;
						u16s[y + x + 2] = fs[y + x + 2] * UINT16_MAX;
						u16s[y + x + 3] = fs[y + x + 3] * UINT16_MAX;
					}
				}
				free(fs);
			} break;
			case 1: unreachable(); // handled in the if above.
			default:
				PSHINE_PANIC("HDR bytes_per_channel == %d not supported (only 4, 2, and 1 are).", bytes_per_channel);
				break;
		}
	}
	if (data == nullptr) PSHINE_PANIC("failed to load texture data at '%s': %s", fpath, stbi_failure_reason());
	return data;
}

static struct vulkan_image load_texture_from_file(
	struct vulkan_renderer *r,
	const char *fpath,
	VkFormat format,
	int format_channels,
	int bytes_per_channel,
	enum vulkan_load_texture_flags flags
) {
	// PSHINE_INFO("Called load_texture_from_file(r, \"%s\", format=%d, format_channels=%d, bytes_per_channel=%d)",
	// 	fpath, (int)format, format_channels, bytes_per_channel);
	// TODO: cache parameters
	for (size_t i = 0; i < r->image_store.dyna.count; ++i) {
		if (strcmp(r->image_store.ptr[i].fpath_own, fpath) == 0) {
			return r->image_store.ptr[i].image;
		}
	}

	// Figure out the image extent.
	VkExtent2D size = {};
	if (flags & VULKAN_LOAD_TEXTURE_CUBEMAP) {
		size_t fpath_len = strlen(fpath);
		char fpath_side[fpath_len + 1] = {};
		fpath_side[fpath_len] = '\0';
		strncpy(fpath_side, fpath, fpath_len);

		char *found = strstr(fpath_side, "%s");
		if (found == nullptr) PSHINE_PANIC("Missing '%%s' in cubemap texture path");
		found[0] = 'p';
		found[1] = 'x';

		int ignored = 0;
		if (!stbi_info(fpath_side, (int*)&size.width, (int*)&size.height, &ignored)) {
			PSHINE_PANIC("Failed to load image at '%s': %s", fpath_side, stbi_failure_reason());
		}
	} else {
		int ignored = 0;
		if (!stbi_info(fpath, (int*)&size.width, (int*)&size.height, &ignored)) {
			PSHINE_PANIC("Failed to load image at '%s': %s", fpath, stbi_failure_reason());
		}
	}

	struct vulkan_image img = allocate_image(r, &(struct vulkan_image_alloc_info){
		.allocation_flags = 0,
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = 0,
		.required_memory_property_flags = 0,
		.out_allocation_info = nullptr,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = (flags & VULKAN_LOAD_TEXTURE_CUBEMAP) ? 6 : 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.extent = (VkExtent3D){
				.width = size.width,
				.height = size.height,
				.depth = 1,
			},
			.format = format,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.flags = (flags & VULKAN_LOAD_TEXTURE_CUBEMAP) ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = (flags & VULKAN_LOAD_TEXTURE_CUBEMAP)
				? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = (flags & VULKAN_LOAD_TEXTURE_CUBEMAP) ? 6 : 1,
				.levelCount = 1,
			}
		},
	});

	NAME_VK_OBJECT(r, img.image, VK_OBJECT_TYPE_IMAGE, "%s image", fpath);
	NAME_VK_OBJECT(r, img.view, VK_OBJECT_TYPE_IMAGE_VIEW, "%s image view", fpath);

	// Now load the actual data.
	if (flags & VULKAN_LOAD_TEXTURE_CUBEMAP) {
		size_t fpath_len = strlen(fpath);
		char fpath_side[fpath_len + 1] = {};
		strncpy(fpath_side, fpath, fpath_len);
		char *found = strstr(fpath_side, "%s");
		if (found == nullptr) PSHINE_PANIC("Missing '%%s' in cubemap texture path");
		for (size_t i = 0; i < 3; ++i) {
			found[1] = 'x' + i;
			for (size_t j = 0; j < 2; ++j) {
				found[0] = "pn"[j];
				VkExtent2D current_size = {};
				void *data = load_texture_data_from_file(fpath_side, format, &current_size, format_channels, bytes_per_channel);
				if (size.width != 0 && (current_size.width != size.width || current_size.height != size.height)) {
					PSHINE_PANIC("Incompatible cubemap sizes: expected %ux%u, got %ux%u",
						size.width, size.height, current_size.width, current_size.height);
				}
				write_to_image_staged(r, &img, (VkOffset3D){
					0, 0, i * 2 + j
				}, (VkExtent3D){
					size.width,
					size.height,
					1
				}, format, size.width * size.height * format_channels * bytes_per_channel, data);
				free(data);
			}
		}
	} else {
		VkExtent2D current_size = {};
		void *data = load_texture_data_from_file(fpath, format, &current_size, format_channels, bytes_per_channel);
		write_to_image_staged(r, &img, (VkOffset3D){ 0, 0, 0 }, (VkExtent3D){
			size.width,
			size.height,
			1
		}, format, size.width * size.height * format_channels * bytes_per_channel, data);
		free(data);
	}

	size_t idx = PSHINE_DYNA_ALLOC(r->image_store);
	r->image_store.ptr[idx]._alive_marker = (size_t)-1;
	r->image_store.ptr[idx].fpath_own = pshine_strdup(fpath);
	r->image_store.ptr[idx].image = img;
	return img;
}

struct vulkan_image_create_info {
	const char *name;
	VkExtent2D size;
	VkFormat format;
	size_t data_size;
	const void *data;
};

static struct vulkan_image create_image(
	struct vulkan_renderer *r,
	const struct vulkan_image_create_info *info
) {
	struct vulkan_image img = allocate_image(r, &(struct vulkan_image_alloc_info){
		.allocation_flags = 0,
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = 0,
		.required_memory_property_flags = 0,
		.out_allocation_info = nullptr,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.extent = (VkExtent3D){
				.width = info->size.width,
				.height = info->size.height,
				.depth = 1,
			},
			.format = info->format,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.flags = 0,
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = info->format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.baseMipLevel = 0,
				.layerCount = 1,
				.levelCount = 1,
			}
		},
	});

	NAME_VK_OBJECT(r, img.image, VK_OBJECT_TYPE_IMAGE, "%s image", info->name);
	NAME_VK_OBJECT(r, img.view, VK_OBJECT_TYPE_IMAGE_VIEW, "%s image view", info->name);

	write_to_image_staged(
		r,
		&img,
		(VkOffset3D){},
		(VkExtent3D){ .width = info->size.width, .height = info->size.height, .depth = 1 },
		info->format,
		info->data_size,
		info->data
	);

	return img;
}

static struct vulkan_image create_image_from_cgltf_texture_view(
	struct vulkan_renderer *r,
	cgltf_texture_view *v,
	int desired_channels,
	VkFormat format
) {
	if (v == nullptr) {
		struct vulkan_image img = create_image(r, &(struct vulkan_image_create_info){
			.name = v->texture->name,
			.size = (VkExtent2D){ .width = 1, .height = 1 },
			.format = format,
			.data = &(uint32_t){ 0 },
			.data_size = 1 * desired_channels,
		});
		return img;
	}
	cgltf_buffer_view *buf = v->texture->image->buffer_view;
	int width, height, channels;
	stbi_uc *data = stbi_load_from_memory(
		(uint8_t*)buf->buffer->data + buf->offset,
		buf->size, &width, &height, &channels, desired_channels
	);
	if (data == nullptr) {
		PSHINE_PANIC("Failed to load embedded image: %s", stbi_failure_reason());
	}
	struct vulkan_image img = create_image(r, &(struct vulkan_image_create_info){
		.name = v->texture->name,
		.size = (VkExtent2D){ .width = (uint32_t)width, .height = (uint32_t)height },
		.format = format,
		.data = data,
		.data_size = (size_t)width * (size_t)height * desired_channels,
	});
	stbi_image_free(data);
	return img;
}

static void load_mesh_model_from_gltf(
	struct vulkan_renderer *r,
	const char *fpath,
	struct vulkan_mesh_model *out
) {
	PSHINE_DEBUG("Loading model from %s", fpath);

	for (size_t i = 0; i < r->model_store.dyna.count; ++i) {
		if (strcmp(r->model_store.ptr[i].fpath_own, fpath) == 0) {
			*out = r->model_store.ptr[i].model;
			return;
		}
	}
	struct cgltf_data *data = nullptr;
	cgltf_result res;
	res = cgltf_parse_file(&(cgltf_options){
		.type = cgltf_file_type_glb,
	}, fpath, &data);

	if (res != cgltf_result_success)
		PSHINE_PANIC("Failed to load glb model at '%s': %s (%u)", fpath, pshine_cgltf_result_string(res), res);

	cgltf_load_buffers(&(cgltf_options){
		.type = cgltf_file_type_glb,
	}, data, fpath);

	size_t total_primitive_count = 0;
	for (size_t i = 0; i < data->meshes_count; ++i) {
		total_primitive_count += data->meshes[i].primitives_count;
	}

	out->part_count = total_primitive_count;
	out->parts_own = calloc(out->part_count, sizeof(*out->parts_own));
	for (size_t i = 0, current_part = 0; i < data->meshes_count; ++i) {
		cgltf_mesh *mesh = &data->meshes[i];
		for (size_t j = 0; j < mesh->primitives_count; ++j, ++current_part) {
			cgltf_primitive *prim = &mesh->primitives[j];
			
			enum : size_t { bad_attrib = (size_t)-1 };
			size_t position_attrib = bad_attrib;
			size_t normal_attrib = bad_attrib;
			size_t tangent_attrib = bad_attrib;
			size_t texcoord_attrib = bad_attrib;
			for (size_t k = 0; k < prim->attributes_count; ++k) {
				switch (prim->attributes[k].type) {
				case cgltf_attribute_type_position: position_attrib = k; break;
				case cgltf_attribute_type_normal: normal_attrib = k; break;
				case cgltf_attribute_type_tangent: tangent_attrib = k; break;
				case cgltf_attribute_type_texcoord: texcoord_attrib = k; break;
				default: break;
				}
			}
			if (
				position_attrib == bad_attrib ||
				normal_attrib == bad_attrib ||
				tangent_attrib == bad_attrib ||
				texcoord_attrib == bad_attrib
			) {
				PSHINE_PANIC(
					"Missing attributes in model\n"
					"\tposition: %zu\n"
					"\tnormal: %zu\n"
					"\ttangent: %zu\n"
					"\ttexcoord: %zu\n",
					position_attrib, normal_attrib,
					tangent_attrib, texcoord_attrib
				);
			}
			cgltf_accessor *position_acc = prim->attributes[position_attrib].data;
			PSHINE_CHECK(position_acc->type == cgltf_type_vec3,
				"Expected position attribute to be of type vec3.");
			PSHINE_CHECK(position_acc->component_type == cgltf_component_type_r_32f,
				"Expected position attribute to be of floats.");

			cgltf_accessor *normal_acc = prim->attributes[normal_attrib].data;
			PSHINE_CHECK(normal_acc->type == cgltf_type_vec3,
				"Expected normal attribute to be of type vec3.");
			PSHINE_CHECK(normal_acc->component_type == cgltf_component_type_r_32f,
				"Expected normal attribute to be of floats.");

			cgltf_accessor *tangent_acc = prim->attributes[tangent_attrib].data;
			PSHINE_CHECK(tangent_acc->type == cgltf_type_vec3 || tangent_acc->type == cgltf_type_vec4,
				"Expected tangent attribute to be of type vec3 or vec4.");
			PSHINE_CHECK(tangent_acc->component_type == cgltf_component_type_r_32f,
				"Expected tangent attribute to be of floats.");

			cgltf_accessor *texcoord_acc = prim->attributes[texcoord_attrib].data;
			PSHINE_CHECK(texcoord_acc->type == cgltf_type_vec2,
				"Expected texcoord attribute to be of type vec2.");
			PSHINE_CHECK(texcoord_acc->component_type == cgltf_component_type_r_32f,
				"Expected texcoord attribute to be of floats.");

			size_t vertex_count = position_acc->count;
			struct pshine_static_mesh_vertex *vertices = calloc(vertex_count, sizeof(*vertices));

			uint8_t *position_data = position_acc->buffer_view->buffer->data;
			position_data += position_acc->buffer_view->offset;
			position_data += position_acc->offset;

			uint8_t *normal_data = normal_acc->buffer_view->buffer->data;
			normal_data += normal_acc->buffer_view->offset;
			normal_data += normal_acc->offset;

			uint8_t *tangent_data = tangent_acc->buffer_view->buffer->data;
			tangent_data += tangent_acc->buffer_view->offset;
			tangent_data += tangent_acc->offset;

			uint8_t *texcoord_data = texcoord_acc->buffer_view->buffer->data;
			texcoord_data += texcoord_acc->buffer_view->offset;
			texcoord_data += texcoord_acc->offset;

			for (size_t k = 0; k < vertex_count; ++k) {
				*(float3*)vertices[k].position = *((float3*)(void*)position_data);
				vertices[k].tangent_dia = encode_tangent(
					*((float3*)(void*)normal_data),
					*((float3*)(void*)tangent_data)
				);
				*(float2*)vertices[k].normal_oct = float32x3_to_oct(*((float3*)(void*)normal_data));
				*(float2*)vertices[k].texcoord = *((float2*)(void*)texcoord_data);
				position_data += position_acc->stride;
				normal_data += normal_acc->stride;
				tangent_data += tangent_acc->stride;
				texcoord_data += texcoord_acc->stride;
			}

			size_t index_count = prim->indices->count;
			uint32_t *indices = calloc(index_count, sizeof(*indices));
			
			uint8_t *index_data = prim->indices->buffer_view->buffer->data;
			index_data += prim->indices->buffer_view->offset;
			index_data += prim->indices->offset;

			switch (prim->indices->component_type) {
				case cgltf_component_type_r_32u:
					for (size_t k = 0; k < index_count; ++k) {
						indices[k] = *(uint32_t*)(void*)index_data;
						index_data += prim->indices->stride;
					} break;
				case cgltf_component_type_r_16:
				case cgltf_component_type_r_16u:
					for (size_t k = 0; k < index_count; ++k) {
						indices[k] = (uint32_t)*(uint16_t*)(void*)index_data;
						index_data += prim->indices->stride;
					} break;
				case cgltf_component_type_r_8:
				case cgltf_component_type_r_8u:
					for (size_t k = 0; k < index_count; ++k) {
						indices[k] = (uint32_t)*(uint8_t*)(void*)index_data;
						index_data += prim->indices->stride;
					} break;
				default: PSHINE_PANIC("Bad index accessor component type, expected integers.");
			}

			out->parts_own[current_part].material_index = prim->material - data->materials;

			create_mesh(r, &(struct pshine_mesh_data){
				.vertex_type = PSHINE_VERTEX_STATIC_MESH,
				.vertex_count = vertex_count,
				.vertices = vertices,
				.index_count = index_count,
				.indices = indices,
			}, &out->parts_own[current_part].mesh);
		}
	}

	out->transform = double4x4_float4x4(*(float4x4*)data->nodes[0].matrix);

	out->material_count = data->materials_count;
	out->materials_own = calloc(out->material_count, sizeof(*out->materials_own));
	for (size_t i = 0; i < data->materials_count; ++i) {
		out->materials_own[i].images.normal
			= create_image_from_cgltf_texture_view(r, &data->materials[i].normal_texture, 4, VK_FORMAT_R8G8B8A8_UNORM);
		out->materials_own[i].images.emissive
		// 	= load_texture_from_file(r, "data/textures/1x1_black.png", VK_FORMAT_R8G8B8A8_UNORM, 4, 1, 0);
			= create_image_from_cgltf_texture_view(r, &data->materials[i].emissive_texture, 4, VK_FORMAT_R8G8B8A8_UNORM);
		out->materials_own[i].images.diffuse
			= create_image_from_cgltf_texture_view(r,
					&data->materials[i].pbr_metallic_roughness.base_color_texture, 4, VK_FORMAT_R8G8B8A8_SRGB);
		NAME_VK_OBJECT(r, out->materials_own[i].images.diffuse.image, VK_OBJECT_TYPE_IMAGE, "Diffuse for %s", fpath);
		NAME_VK_OBJECT(r, out->materials_own[i].images.emissive.image, VK_OBJECT_TYPE_IMAGE, "Emissive for %s", fpath);
		NAME_VK_OBJECT(r, out->materials_own[i].images.normal.image, VK_OBJECT_TYPE_IMAGE, "Normal for %s", fpath);

		// Combine the occlusion (R) and metallic+roughness (GB) textures into one.
		{
			cgltf_texture_view *occlusion = &data->materials[i].occlusion_texture;
			cgltf_texture_view *metallic_roughness = &data->materials[i].pbr_metallic_roughness.metallic_roughness_texture;
			cgltf_buffer_view *buf_gb = metallic_roughness->texture->image->buffer_view;
			int width, height, channels;
			
			// (Initially only has the green and blue channels.)
			// The alpha channel exists because most GPUs don't support R8G8B8 images.
			// We just ignore the channel.
			// TODO: Use R8G8B8 if the GPU supports it.
			stbi_uc *data_rgb = stbi_load_from_memory(
				(uint8_t*)buf_gb->buffer->data + buf_gb->offset,
				buf_gb->size, &width, &height, &channels, 4
			);
			if (data_rgb == nullptr) {
				PSHINE_PANIC("Failed to load embedded image: %s", stbi_failure_reason());
			}
			if (occlusion->texture != nullptr) {
				cgltf_buffer_view *buf_r = occlusion->texture->image->buffer_view;
				stbi_uc *data_r = stbi_load_from_memory(
					(uint8_t*)buf_r->buffer->data + buf_r->offset,
					buf_r->size, &width, &height, &channels, 1
				);
				if (data_r == nullptr) {
					PSHINE_PANIC("Failed to load embedded image: %s", stbi_failure_reason());
				}
				// Fill the empty R channel from `data_r`.
				for (size_t i = 0; i < (size_t)width * (size_t)height; ++i) {
					data_rgb[i * 4] = data_r[i];
				}
				stbi_image_free(data_r);
			}
			out->materials_own[i].images.ao_metallic_roughness = create_image(r, &(struct vulkan_image_create_info){
				.size = (VkExtent2D){ .width = (uint32_t)width, .height = (uint32_t)height },
				.format = VK_FORMAT_R8G8B8A8_UNORM,
				.data = data_rgb,
				.data_size = (size_t)width * (size_t)height * 4,
			});
			NAME_VK_OBJECT(r, out->materials_own[i].images.ao_metallic_roughness.image, VK_OBJECT_TYPE_IMAGE,
				"Occlusion+Metallic+Roughness for %s", fpath);
			stbi_image_free(data_rgb);
		}

		out->materials_own[i].uniform_buffer = allocate_buffer(r, &(struct vulkan_buffer_alloc_info){
			.size = get_padded_uniform_buffer_size(r, sizeof(struct std_mesh_uniform_data)) * FRAMES_IN_FLIGHT,
			.buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.required_memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
			.memory_usage = VMA_MEMORY_USAGE_AUTO,
		});
		NAME_VK_OBJECT(r, out->materials_own[i].uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER,
			"Material UB #%zu for %s", i, fpath);

		CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = r->descriptors.pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &r->descriptors.std_material_layout,
		}, &out->materials_own[i].descriptor_set));
		NAME_VK_OBJECT(r, out->materials_own[i].descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Material %zu of %s", i, fpath);

		vkUpdateDescriptorSets(r->device, 5, (VkWriteDescriptorSet[5]){
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.dstSet = out->materials_own[i].descriptor_set,
				.dstArrayElement = 0,
				.dstBinding = 0,
				.pBufferInfo = &(VkDescriptorBufferInfo){
					.buffer = out->materials_own[i].uniform_buffer.buffer,
					.offset = 0,
					.range = sizeof(struct std_material_uniform_data),
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = out->materials_own[i].descriptor_set,
				.dstArrayElement = 0,
				.dstBinding = 1,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = out->materials_own[i].images.diffuse.view,
					.sampler = r->material_texture_sampler,
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = out->materials_own[i].descriptor_set,
				.dstArrayElement = 0,
				.dstBinding = 2,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = out->materials_own[i].images.ao_metallic_roughness.view,
					.sampler = r->material_texture_sampler,
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = out->materials_own[i].descriptor_set,
				.dstArrayElement = 0,
				.dstBinding = 3,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = out->materials_own[i].images.normal.view,
					.sampler = r->material_texture_sampler,
				},
			},
			(VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = out->materials_own[i].descriptor_set,
				.dstArrayElement = 0,
				.dstBinding = 4,
				.pImageInfo = &(VkDescriptorImageInfo){
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.imageView = out->materials_own[i].images.emissive.view,
					.sampler = r->material_texture_sampler,
				},
			},
		}, 0, nullptr);
	}
	cgltf_free(data);

	size_t idx = PSHINE_DYNA_ALLOC(r->model_store);
	r->model_store.ptr[idx]._alive_marker = (size_t)-1;
	r->model_store.ptr[idx].model = *out;
	r->model_store.ptr[idx].fpath_own = pshine_strdup(fpath);
}

static void init_ship(struct vulkan_renderer *r, struct pshine_ship *ship) {
	ship->graphics_data = calloc(1, sizeof(*ship->graphics_data));
	load_mesh_model_from_gltf(r, ship->model_file_own, &ship->graphics_data->model);

	ship->graphics_data->uniform_buffer = allocate_buffer(r, &(struct vulkan_buffer_alloc_info){
		.size = get_padded_uniform_buffer_size(r, sizeof(struct std_mesh_uniform_data)) * FRAMES_IN_FLIGHT,
		.buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.required_memory_property_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.memory_usage = VMA_MEMORY_USAGE_AUTO,
	});
	NAME_VK_OBJECT(r, ship->graphics_data->uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "static mesh ub");

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.std_mesh_layout,
	}, &ship->graphics_data->descriptor_set));
	vkUpdateDescriptorSets(r->device, 1, (VkWriteDescriptorSet[1]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.dstSet = ship->graphics_data->descriptor_set,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = ship->graphics_data->uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct std_mesh_uniform_data),
			}
		},
	}, 0, nullptr);
}

static void init_star_system(struct vulkan_renderer *r, struct pshine_star_system *system) {
	for (uint32_t i = 0; i < system->body_count; ++i) {
		struct pshine_celestial_body *b = system->bodies_own[i];
		// PSHINE_DEBUG("creating graphics data for %s", b->name_own);
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			p->graphics_data = calloc(1, sizeof(struct pshine_planet_graphics_data));

			// Static mesh descriptors
			CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = r->descriptors.pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &r->descriptors.planet_mesh_layout,
			}, &p->graphics_data->descriptor_set));

			// Material descriptors
			CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = r->descriptors.pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &r->descriptors.planet_material_layout,
			}, &p->graphics_data->material_descriptor_set));

			// Atmosphere descriptors
			CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = r->descriptors.pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &r->descriptors.atmo_layout,
			}, &p->graphics_data->atmo_descriptor_set));

			// Atmosphere LUT compute shader descriptors
			CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = r->descriptors.pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &r->descriptors.atmo_lut_layout,
			}, &p->graphics_data->atmo_lut_descriptor_set));

			if (b->rings.has_rings) {
				// Rings shader descriptors
				CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
					.descriptorPool = r->descriptors.pool,
					.descriptorSetCount = 1,
					.pSetLayouts = &r->descriptors.rings_layout,
				}, &p->graphics_data->rings_descriptor_set));
			}

			struct vulkan_buffer_alloc_info common_alloc_info = {
				.size = 0,
				.buffer_usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				.required_memory_property_flags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				.allocation_flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
				.memory_usage = VMA_MEMORY_USAGE_AUTO,
			};

			common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct planet_mesh_uniform_data)) * FRAMES_IN_FLIGHT;
			p->graphics_data->uniform_buffer = allocate_buffer(r, &common_alloc_info);
			NAME_VK_OBJECT(r, p->graphics_data->uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "planet mesh ub");

			common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * FRAMES_IN_FLIGHT;
			p->graphics_data->atmo_uniform_buffer = allocate_buffer(r, &common_alloc_info);
			NAME_VK_OBJECT(r, p->graphics_data->atmo_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "atmo ub");

			common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct planet_material_uniform_data)) * FRAMES_IN_FLIGHT;
			p->graphics_data->material_uniform_buffer = allocate_buffer(r, &common_alloc_info);
			NAME_VK_OBJECT(r, p->graphics_data->material_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "material ub");

			if (b->rings.has_rings) {
				common_alloc_info.size = get_padded_uniform_buffer_size(r, sizeof(struct rings_uniform_data)) * FRAMES_IN_FLIGHT;
				p->graphics_data->rings_uniform_buffer = allocate_buffer(r, &common_alloc_info);
				NAME_VK_OBJECT(r, p->graphics_data->rings_uniform_buffer.buffer, VK_OBJECT_TYPE_BUFFER, "rings ub");
			}

			load_planet_texture(r, p);
			init_atmo_lut_compute(r, p);

			// TODO: Storage buffer with all mesh data and another with all atmosphere data.

			vkUpdateDescriptorSets(r->device, 9, (VkWriteDescriptorSet[9]){
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
						.range = sizeof(struct planet_mesh_uniform_data),
					}
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
					.dstSet = p->graphics_data->atmo_descriptor_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.pBufferInfo = &(VkDescriptorBufferInfo){
						.buffer = p->graphics_data->atmo_uniform_buffer.buffer,
						.offset = 0,
						.range = sizeof(struct atmo_uniform_data),
					}
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.dstSet = p->graphics_data->material_descriptor_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.pBufferInfo = &(VkDescriptorBufferInfo){
						.buffer = p->graphics_data->material_uniform_buffer.buffer,
						.offset = 0,
						.range = sizeof(struct planet_material_uniform_data),
					}
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.dstSet = p->graphics_data->atmo_descriptor_set,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.imageView = p->graphics_data->atmo_lut.view,
						.sampler = r->atmo_lut_sampler,
					},
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.dstSet = p->graphics_data->material_descriptor_set,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.imageView = p->graphics_data->surface_albedo.view,
						.sampler = r->material_texture_sampler,
					},
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.dstSet = p->graphics_data->material_descriptor_set,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.imageView = p->graphics_data->surface_bump.view,
						.sampler = r->material_texture_sampler,
					},
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.dstSet = p->graphics_data->material_descriptor_set,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.imageView = p->graphics_data->surface_specular.view,
						.sampler = r->material_texture_sampler,
					},
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
					.dstSet = p->graphics_data->atmo_descriptor_set,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_GENERAL, // SHADER_READ_ONLY_OPTIMAL
						.imageView = r->transients.color_0.view,
						.sampler = VK_NULL_HANDLE,
					}
				},
				(VkWriteDescriptorSet){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
					.dstSet = p->graphics_data->atmo_descriptor_set,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.pImageInfo = &(VkDescriptorImageInfo){
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.imageView = r->depth_image.view,
						.sampler = VK_NULL_HANDLE
					}
				},
			}, 0, nullptr);

			if (b->rings.has_rings) {
				vkUpdateDescriptorSets(r->device, 2, (VkWriteDescriptorSet[2]){
					(VkWriteDescriptorSet){
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
						.dstSet = p->graphics_data->rings_descriptor_set,
						.dstBinding = 0,
						.dstArrayElement = 0,
						.pBufferInfo = &(VkDescriptorBufferInfo){
							.buffer = p->graphics_data->rings_uniform_buffer.buffer,
							.offset = 0,
							.range = sizeof(struct rings_uniform_data),
						}
					},
					(VkWriteDescriptorSet){
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						.dstSet = p->graphics_data->rings_descriptor_set,
						.dstBinding = 1,
						.dstArrayElement = 0,
						.pImageInfo = &(VkDescriptorImageInfo){
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							.imageView = p->graphics_data->ring_slice_texture.view,
							.sampler = r->material_texture_sampler,
						}
					},
				}, 0, nullptr);
			}

			compute_atmo_lut(r, p, true);
		} else if (b->type == PSHINE_CELESTIAL_BODY_STAR) {
			struct pshine_star *p = (void *)b;
			p->graphics_data = calloc(1, sizeof(struct pshine_star_graphics_data));
			p->graphics_data->uniform_buffer = allocate_buffer(
				r,
				&(struct vulkan_buffer_alloc_info){
					.size = get_padded_uniform_buffer_size(r, sizeof(struct planet_mesh_uniform_data)) * FRAMES_IN_FLIGHT,
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
				.pSetLayouts = &r->descriptors.planet_mesh_layout,
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
						.range = sizeof(struct planet_mesh_uniform_data)
					}
				},
			}, 0, nullptr);
		}
	}
}

void pshine_init_renderer(struct pshine_renderer *renderer, struct pshine_game *game) {
	struct vulkan_renderer *r = (void*)renderer;
	r->game = game;
	r->as_base.name = "Vulkan Renderer";

	r->key_states = calloc(PSHINE_KEY_COUNT_, sizeof(uint8_t));

	r->lod_ranges[0] = 300'000.0;
	r->lod_ranges[1] = 25'000.0;
	r->lod_ranges[2] = 1'500.0;
	r->lod_ranges[3] = 290.0;

	r->opt_bloom = true;

	init_glfw(r);
	init_vulkan(r);
	init_swapchain(r);
	init_transients(r);
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
		PSHINE_INFO("GPU 64 bit int support:   %s", features.shaderInt64   ? "true" : "false");
	}

	{
		r->sphere_mesh_count = 5;
		r->own_sphere_meshes = calloc(r->sphere_mesh_count, sizeof(struct vulkan_mesh));
		for (size_t lod = 0; lod < r->sphere_mesh_count; ++lod) {
			struct pshine_mesh_data mesh_data = {
				.index_count = 0,
				.indices = nullptr,
				.vertex_count = 0,
				.vertices = nullptr,
				.vertex_type = PSHINE_VERTEX_PLANET
			};
			pshine_generate_planet_mesh(nullptr, &mesh_data, lod);
			create_mesh(r, &mesh_data, &r->own_sphere_meshes[lod]);
			free(mesh_data.indices);
			free(mesh_data.vertices);
		}
	}

	for (size_t i = 0; i < r->game->star_system_count; ++i) {
		init_star_system(r, &r->game->star_systems_own[i]);
	}

	for (size_t i = 0; i < r->game->ships.dyna.count; ++i) {
		if (r->game->ships.ptr[i]._alive_marker != (size_t)-1) continue;
		init_ship(r, &r->game->ships.ptr[i]);
	}

	PSHINE_DEBUG("Loading environment cubemap");
	r->skybox_image = load_texture_from_file(
		r,
		r->game->environment.texture_path_own,
		VK_FORMAT_R16G16B16A16_UNORM,
		4,
		2,
		VULKAN_LOAD_TEXTURE_CUBEMAP
	);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.skybox_layout
	}, &r->data.skybox_descriptor_set));
	NAME_VK_OBJECT(r, r->data.skybox_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "skybox ds");

	vkUpdateDescriptorSets(r->device, 1, (VkWriteDescriptorSet[1]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.dstSet = r->data.skybox_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->skybox_image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = r->skybox_sampler,
			},
		},
	}, 0, nullptr);
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
		0, 0, nullptr, 0, nullptr, 1, &(VkImageMemoryBarrier){
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
		&planet->graphics_data->atmo_lut_descriptor_set,
		0, nullptr
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
		0, 0, nullptr, 0, nullptr, 1, &(VkImageMemoryBarrier){
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
			},
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

static void load_planet_texture(struct vulkan_renderer *r, struct pshine_planet *planet) {
	planet->graphics_data->surface_albedo = load_texture_from_file(
		r, planet->as_body.surface.albedo_texture_path_own, VK_FORMAT_R8G8B8A8_SRGB, 4, 1, 0
	);
	// planet->graphics_data->surface_lights = load_texture_from_file(
	// 	r, planet->surface.lights_texture_path, VK_FORMAT_R8G8B8A8_SRGB, 4
	// );
	planet->graphics_data->surface_bump = load_texture_from_file(
		r, planet->as_body.surface.bump_texture_path_own, VK_FORMAT_R8_UNORM, 1, 1, 0
	);
	planet->graphics_data->surface_specular = load_texture_from_file(
		r, planet->as_body.surface.spec_texture_path_own, VK_FORMAT_R8_UNORM, 1, 1, 0
	);
	if (planet->as_body.rings.has_rings) {
		planet->graphics_data->ring_slice_texture = load_texture_from_file(
			r, planet->as_body.rings.slice_texture_path_own, VK_FORMAT_R8G8B8A8_SRGB, 4, 1, 0
		);
	}
}

static void init_atmo_lut_compute(struct vulkan_renderer *r, struct pshine_planet *planet) {
	struct vulkan_image img = allocate_image(r, &(struct vulkan_image_alloc_info){
		.allocation_flags = 0,
		.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		.preferred_memory_property_flags = 0,
		.required_memory_property_flags = 0,
		.out_allocation_info = nullptr,
		.image_info = &(VkImageCreateInfo){
			.imageType = VK_IMAGE_TYPE_2D,
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.extent = (VkExtent3D){ .width = atmo_lut_extent.width, .height = atmo_lut_extent.height, .depth = 1 },
			.format = atmo_lut_format,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
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
		.dstSet = planet->graphics_data->atmo_lut_descriptor_set, // TODO: cpu-side sync (thread-safety)
		.pImageInfo = &(VkDescriptorImageInfo){
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.imageView = img.view,
			.sampler = VK_NULL_HANDLE
		}
	}, 0, nullptr);

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

static void mouse_cb_glfw_(GLFWwindow *window, int button, int action, int mods) {
	struct vulkan_renderer *r = glfwGetWindowUserPointer(window);
	r->mouse_states[button] = action;
}

static void scroll_cb_glfw_(GLFWwindow *window, double x, double y) {
	struct vulkan_renderer *r = glfwGetWindowUserPointer(window);
	r->scroll_delta = double2xy(x, y);
}

static void init_glfw(struct vulkan_renderer *r) {
	glfwInitVulkanLoader(vkGetInstanceProcAddr);
	PSHINE_DEBUG("GLFW version: %s", glfwGetVersionString());
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__APPLE__)
	if (pshine_check_has_option("-x11"))
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	else
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif
	glfwSetErrorCallback(&error_cb_glfw_);
	if (!glfwInit()) PSHINE_PANIC("could not initialize GLFW");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	int window_width = 1920 / 1.5, window_height = 1080 / 1.5;
	GLFWmonitor *monitor = nullptr;
	if (pshine_check_has_option("--fullscreen")) {
		monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		window_width = mode->width;
		window_height = mode->height;
	}

	r->window = glfwCreateWindow(window_width, window_height, "pshine2", monitor, nullptr);
	glfwSetWindowUserPointer(r->window, r);
	if (r->window == nullptr) PSHINE_PANIC("could not create window");
	glfwSetKeyCallback(r->window, &key_cb_glfw_);
	glfwSetMouseButtonCallback(r->window, &mouse_cb_glfw_);
	glfwSetScrollCallback(r->window, &scroll_cb_glfw_);
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
#if defined(__APPLE__)
	{
		uint32_t count = 0;
		CHECKVK(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
		VkExtensionProperties properties[count];
		CHECKVK(vkEnumerateInstanceExtensionProperties(nullptr, &count, properties));
		for (uint32_t i = 0; i < count; ++i) {
			if (strcmp(properties[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
				have_portability_ext = true;
				break;
			}
		}
	}
#endif

	uint32_t ext_count = extension_count_glfw + 1 + have_portability_ext;
	const char **extensions = calloc(ext_count, sizeof(const char *));
	memcpy(extensions, extensions_glfw, sizeof(const char *) * extension_count_glfw);
	extensions[extension_count_glfw + 0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
	if (have_portability_ext)
		extensions[extension_count_glfw + 1] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;

	bool have_validation = false;

	{
		uint32_t count = 0;
		CHECKVK(vkEnumerateInstanceLayerProperties(&count, nullptr));
		VkLayerProperties props[count];
		CHECKVK(vkEnumerateInstanceLayerProperties(&count, props));
		for (uint32_t i = 0; i < count; ++i) {
			if (strcmp(props[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
				have_validation = true;
				break;
			}
		}
	}

	CHECKVK(vkCreateInstance(&(VkInstanceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.flags = have_portability_ext ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0,
		.pApplicationInfo = &(VkApplicationInfo){
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.apiVersion = VK_MAKE_API_VERSION(0, 1, 4, 0),
			.pApplicationName = "planetshine",
			.applicationVersion = VK_MAKE_VERSION(0, 2, 0),
			.pEngineName = "planetshine-engine",
			.engineVersion = VK_MAKE_VERSION(0, 3, 0)
		},
		.enabledExtensionCount = ext_count,
		.ppEnabledExtensionNames = extensions,
		.enabledLayerCount = have_validation ? 1 : 0, // VALIDATION_LAYERS
		.ppEnabledLayerNames = (const char *[1]) {
			"VK_LAYER_KHRONOS_validation"
		}
	}, nullptr, &r->instance));

	free(extensions);

	volkLoadInstanceOnly(r->instance);

	CHECKVK(glfwCreateWindowSurface(r->instance, r->window, nullptr, &r->surface));

	{
		uint32_t physical_device_count = 0;
		CHECKVK(vkEnumeratePhysicalDevices(r->instance, &physical_device_count, nullptr));
		VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
		CHECKVK(vkEnumeratePhysicalDevices(r->instance, &physical_device_count, physical_devices));
		r->physical_device = physical_devices[0]; // TODO: physical device selection
		free(physical_devices);
	}

	{
		uint32_t property_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &property_count, nullptr);
		VkQueueFamilyProperties properties[property_count];
		vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &property_count, properties);
		for (uint32_t i = 0; i < property_count; ++i) {
			// we can do this because Vulkan requires at least one queue that supports both graphics and compute to exist.
			if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
				r->queue_families[QUEUE_GRAPHICS] = i;
			}
			// TODO: prefer this queue to be separate from the graphics queue.
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
		VkPhysicalDeviceMaintenance3Properties *p3 = calloc(1, sizeof(*p3));
		p3->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
		r->physical_device_properties_own->pNext = p3;

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
			r->queue_families[2],
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
			queue_create_infos[i] = (VkDeviceQueueCreateInfo){
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.pQueuePriorities = &queuePriority,
				.queueCount = 1,
				.queueFamilyIndex = unique_queue_family_indices[i]
			};
		}

		bool have_portability_ext = false;
		{
			uint32_t count = 0;
			CHECKVK(vkEnumerateDeviceExtensionProperties(r->physical_device, nullptr, &count, nullptr));
			VkExtensionProperties properties[count];
			CHECKVK(vkEnumerateDeviceExtensionProperties(r->physical_device, nullptr, &count, properties));
			for (uint32_t i = 0; i < count; ++i) {
				if (strcmp(properties[i].extensionName, "VK_KHR_portability_subset") == 0) {
					have_portability_ext = true;
					break;
				}
			}
		}

		CHECKVK(vkCreateDevice(r->physical_device, &(VkDeviceCreateInfo){
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.pNext = &(VkPhysicalDeviceSynchronization2Features){
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
				.synchronization2 = true,
			},
			.queueCreateInfoCount = queue_create_info_count,
			.pQueueCreateInfos = queue_create_infos,
			.enabledExtensionCount = 2 + have_portability_ext,
			.ppEnabledExtensionNames = (const char *[]){
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
				"VK_KHR_portability_subset",
				// VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
			},
			.pEnabledFeatures = &(VkPhysicalDeviceFeatures){},
		}, nullptr, &r->device));
	}

	volkLoadDevice(r->device);

	// Sneaky
	vkCmdPipelineBarrier2 = vkCmdPipelineBarrier2KHR;

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

	vkDestroyDevice(r->device, nullptr);

	free(r->physical_device_properties_own);
	free(r->physical_device_features_own);

	vkDestroySurfaceKHR(r->instance, r->surface, nullptr);
	vkDestroyInstance(r->instance, nullptr);
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
	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, nullptr));
	VkSurfaceFormatKHR formats[format_count];
	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &format_count, formats));
	VkSurfaceFormatKHR found_format;
	bool found = false;
	for (uint32_t i = 0; i < format_count; ++i) {
		// PSHINE_INFO("format: %s", pshine_vk_format_string(formats[i].format));
		// PSHINE_INFO("color space: %s", pshine_vk_color_space_string(formats[i].colorSpace));
		if (formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
				found_format = formats[i];
				found = true;
			} else if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB) {
				found_format = formats[i];
				found = true;
			}
		}
	}
	if (!found) PSHINE_PANIC("did not find good surface format, checked %u formats.", format_count);

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

static void reinit_swapchain(struct vulkan_renderer *r) {
	// vkDeviceWaitIdle(r->device);
	// if (r->swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(r->device, r->swapchain, nullptr);
	int width, height;
	glfwGetFramebufferSize(r->window, &width, &height);
	r->swapchain_extent.width = width;
	r->swapchain_extent.height = height;
	// PSHINE_INFO("swapchain extent: %ux%u", r->swapchain_extent.width, r->swapchain_extent.height);
	CHECKVK(vkCreateSwapchainKHR(r->device, &(VkSwapchainCreateInfoKHR){
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = r->surface,
		.minImageCount = r->surface_capabilities.minImageCount,
		.imageFormat = r->surface_format.format,
		.imageArrayLayers = 1,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = r->swapchain_extent,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.queueFamilyIndexCount = r->queue_families[QUEUE_GRAPHICS] != r->queue_families[QUEUE_PRESENT] ? 2 : 0,
		.imageSharingMode = r->queue_families[QUEUE_GRAPHICS] != r->queue_families[QUEUE_PRESENT]
			? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.pQueueFamilyIndices = (uint32_t[]) { r->queue_families[QUEUE_GRAPHICS], r->queue_families[QUEUE_PRESENT] },
		.preTransform = r->surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.oldSwapchain = VK_NULL_HANDLE,
	}, nullptr, &r->swapchain));

	// for (size_t i = 0; i < r->swapchain_image_count; ++i) {
	// 	vkDestroyImageView(r->device, r->swapchain_image_views_own[i], nullptr);
	// }
	// free(r->swapchain_image_views_own);

	r->swapchain_image_count = 0;
	// free(r->swapchain_images_own);
	CHECKVK(vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchain_image_count, nullptr));
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
		}, nullptr, &r->swapchain_image_views_own[i]));
		NAME_VK_OBJECT(r, r->swapchain_image_views_own[i], VK_OBJECT_TYPE_IMAGE_VIEW, "swapchain image view #%u", i);
	}
}

static void init_swapchain(struct vulkan_renderer *r) {
	CHECKVK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r->physical_device, r->surface, &r->surface_capabilities));
	r->swapchain_extent = get_swapchain_extent(r, &r->surface_capabilities);
	r->surface_format = find_surface_format(r);
	r->depth_format = find_optimal_format(r, 1, (VkFormat[]){
		VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
	}, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL);
	reinit_swapchain(r);
	// {
	// 	uint32_t surface_format_count = 0;
	// 	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &surface_format_count, nullptr));
	// 	VkSurfaceFormatKHR surface_formats[surface_format_count];
	// 	CHECKVK(vkGetPhysicalDeviceSurfaceFormatsKHR(r->physical_device, r->surface, &surface_format_count, surface_formats));
	// 	for (uint32_t i = 0; i < surface_format_count; ++i) {
	// 		PSHINE_INFO("surface format: %d, color space: %d", surface_formats[i].format, surface_formats[i].colorSpace);
	// 	}
	// }

}

static void deinit_swapchain(struct vulkan_renderer *r) {
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i)
		vkDestroyImageView(r->device, r->swapchain_image_views_own[i], nullptr);

	free(r->swapchain_image_views_own);

	free(r->swapchain_images_own);

	vkDestroySwapchainKHR(r->device, r->swapchain, nullptr);
}


// Render passes

static void init_shadow_rpass(struct vulkan_renderer *r) {
	enum {
		main_subpass_index,
		subpass_count
	};
	enum {
		shadow_cascades_attachment_index,
		attachment_count
	};

	VkSubpassDependency subpass_dependencies[] = {
		// synchronize previous compute shader transient_color_attachment write
		// before current tonemap_subpass transient_color_attachment read.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = main_subpass_index,
			.srcStageMask = r->opt_bloom
				? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				: VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = r->opt_bloom
				? VK_ACCESS_SHADER_WRITE_BIT
				: VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		},
		// synchronize previous swapchain_attachment_index write
		// before current tonemap_subpass swapchain_attachment_index write.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = main_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
	};

	vkCreateRenderPass2(r->device, &(VkRenderPassCreateInfo2){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
		.pNext = nullptr,
		.subpassCount = 1,
		.pSubpasses = &(VkSubpassDescription2){
			.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
			.viewMask = 0,
			
		},
		.dependencyCount = 0,
		.pDependencies = nullptr,
		.attachmentCount = 1,
		.pAttachments = &(VkAttachmentDescription2){
			.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.format = r->depth_format,
			.flags = 0,
			.samples = VK_SAMPLE_COUNT_1_BIT,
		},
	}, nullptr, &r->render_passes.shadow_pass);
	// CHECKVK(vkCreateRenderPass(r->device, &(VkRenderPassCreateInfo){
	// 	.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
	// 	.pNext = nullptr,
	// 	.flags = 0,
	// 	.subpassCount = subpass_count,
	// 	.pSubpasses = (VkSubpassDescription[subpass_count]){
	// 		[main_subpass_index] = (VkSubpassDescription){
	// 			.colorAttachmentCount = 1,
	// 			.pColorAttachments = (VkAttachmentReference[]){
	// 				(VkAttachmentReference){
	// 					.attachment = main,
	// 					.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	// 				},
	// 			},
	// 			.inputAttachmentCount = 2,
	// 			.pInputAttachments = (VkAttachmentReference[]){
	// 				(VkAttachmentReference){
	// 					.attachment = transient_color_attachment_index,
	// 					.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 				},
	// 				(VkAttachmentReference){
	// 					.attachment = transient_depth_attachment_index,
	// 					.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 				},
	// 			},
	// 		},
	// 	},
	// 	.attachmentCount = attachment_count,
	// 	.pAttachments = (VkAttachmentDescription[attachment_count]){
	// 		[swapchain_attachment_index] = (VkAttachmentDescription){
	// 			.format = r->surface_format.format,
	// 			.samples = VK_SAMPLE_COUNT_1_BIT,
	// 			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	// 			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	// 			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	// 			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	// 			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	// 			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	// 		},
	// 		[transient_color_attachment_index] = (VkAttachmentDescription){
	// 			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
	// 			.samples = VK_SAMPLE_COUNT_1_BIT,
	// 			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
	// 			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	// 			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	// 			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	// 			.initialLayout = r->opt_bloom
	// 				? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	// 				: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // TODO: figure out if this is good
	// 		},
	// 		[transient_depth_attachment_index] = (VkAttachmentDescription){
	// 			.format = r->depth_format,
	// 			.samples = VK_SAMPLE_COUNT_1_BIT,
	// 			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
	// 			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	// 			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	// 			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
	// 			.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 		},
	// 	},
	// 	.dependencyCount = sizeof(subpass_dependencies) / sizeof(*subpass_dependencies),
	// 	.pDependencies = subpass_dependencies,
	// }, nullptr, &r->render_passes.sdr_pass));
	NAME_VK_OBJECT(r, r->render_passes.shadow_pass, VK_OBJECT_TYPE_RENDER_PASS, "sdr render pass");
}

static void init_hdr_rpass(struct vulkan_renderer *r) {
	enum {
		geometry_subpass_index,
		atmosphere_subpass_index,
		lighting_subpass_index,
		subpass_count
	};
	enum {
		transient_color_attachment_index,
		transient_depth_attachment_index,
		transient_gbuffer_attachment_index_start_,
		transient_gbuffer_attachment_index_end_
			= transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_COUNT_ - 1,
		attachment_count
	};

	VkSubpassDependency subpass_dependencies[] = {
		// synchronize previous geometry_subpass transient_color/gbuffer_attachment write
		// before current geometry_subpass transient_color/gbuffer_attachment write.
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
			.dstSubpass = atmosphere_subpass_index,
			.srcStageMask
				= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask
				= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, // earlier write/read
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // current write
		},
		// synchronize lighting_subpass transient_color_attachment write
		// before external bloom transient_color_attachment blit/transfer.
		(VkSubpassDependency){
			.srcSubpass = lighting_subpass_index,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // earlier write
			.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT, // later read
		},
		// synchronize lighting_subpass transient_depth_attachment write
		// before external bloom transient_depth_attachment blit/transfer.
		// (VkSubpassDependency){
		// 	.srcSubpass = atmosphere_subpass_index,
		// 	.dstSubpass = VK_SUBPASS_EXTERNAL,
		// 	.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		// 	.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, // earlier write
		// 	.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		// 	.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT, // later read
		// 	.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		// },
		// ...
		// (VkSubpassDependency){
		// 	.srcSubpass = geometry_subpass_index,
		// 	.dstSubpass = VK_SUBPASS_EXTERNAL,
		// 	.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		// 	.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // earlier write
		// 	.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		// 	.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, // later read
		// },
		// synchronize composite_subpass transient_color_attachment write
		// before composite_subpass transient_color_attachment read.
		(VkSubpassDependency){
			.srcSubpass = atmosphere_subpass_index,
			.dstSubpass = atmosphere_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // earlier write
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT, // later read
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		// synchronize geometry_subpass transient_color_attachment write
		// before composite_subpass transient_color_attachment (as input attachment) read.
		// synchronize geometry_subpass transient_depth_attachment write
		// before composite_subpass transient_depth_attachment (as input attachment) read
		(VkSubpassDependency){
			.srcSubpass = geometry_subpass_index,
			.dstSubpass = atmosphere_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask
				= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
				| VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		// ...
		(VkSubpassDependency){
			.srcSubpass = atmosphere_subpass_index,
			.dstSubpass = lighting_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask
				= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask
				= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		// ...
		(VkSubpassDependency){
			.srcSubpass = geometry_subpass_index,
			.dstSubpass = atmosphere_subpass_index,
			.srcStageMask
				= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
				| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask
				= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask
				= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
				| VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask
				= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		// ...
		(VkSubpassDependency){
			.srcSubpass = lighting_subpass_index,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstStageMask
				= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
				| VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstAccessMask
				= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
				| VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
	};

	CHECKVK(vkCreateRenderPass(r->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.subpassCount = subpass_count,
		.pSubpasses = (VkSubpassDescription[subpass_count]){
			[geometry_subpass_index] = (VkSubpassDescription){
				.colorAttachmentCount = 4,
				.pColorAttachments = (VkAttachmentReference[]){
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_DIFFUSE_O,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_NORMAL_R_M,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_EMISSIVE,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.pDepthStencilAttachment = &(VkAttachmentReference){
					.attachment = transient_depth_attachment_index,
					.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				},
			},
			[atmosphere_subpass_index] = (VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_GENERAL,
					},
				},
				.inputAttachmentCount = 2,
				.pInputAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_GENERAL,
					},
					(VkAttachmentReference){
						.attachment = transient_depth_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
				}
			},
			[lighting_subpass_index] = (VkSubpassDescription){
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.inputAttachmentCount = 4,
				.pInputAttachments = (VkAttachmentReference[]) {
					(VkAttachmentReference){
						.attachment = transient_depth_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_DIFFUSE_O,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_NORMAL_R_M,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_EMISSIVE,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
				}
			},
		},
		.attachmentCount = attachment_count,
		.pAttachments = (VkAttachmentDescription[]){
			[transient_color_attachment_index] = (VkAttachmentDescription){
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
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
			[transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_DIFFUSE_O] = (VkAttachmentDescription){
				.format = r->transients.gbuffer[GBUFFER_IMAGE_DIFFUSE_O].format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
			[transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_NORMAL_R_M] = (VkAttachmentDescription){
				.format = r->transients.gbuffer[GBUFFER_IMAGE_NORMAL_R_M].format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
			[transient_gbuffer_attachment_index_start_ + GBUFFER_IMAGE_EMISSIVE] = (VkAttachmentDescription){
				.format = r->transients.gbuffer[GBUFFER_IMAGE_EMISSIVE].format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		},
		.dependencyCount = sizeof(subpass_dependencies) / sizeof(*subpass_dependencies),
		.pDependencies = subpass_dependencies,
	}, nullptr, &r->render_passes.hdr_pass));
	NAME_VK_OBJECT(r, r->render_passes.hdr_pass, VK_OBJECT_TYPE_RENDER_PASS, "hdr render pass");
}

static void init_sdr_rpass(struct vulkan_renderer *r) {
	enum {
		tonemap_subpass_index,
		subpass_count
	};
	enum {
		swapchain_attachment_index,
		transient_color_attachment_index,
		transient_depth_attachment_index,
		attachment_count
	};

	VkSubpassDependency subpass_dependencies[] = {
		// synchronize previous compute shader transient_color_attachment write
		// before current tonemap_subpass transient_color_attachment read.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = tonemap_subpass_index,
			.srcStageMask = r->opt_bloom
				? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
				: VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = r->opt_bloom
				? VK_ACCESS_SHADER_WRITE_BIT
				: VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
		},
		// synchronize previous swapchain_attachment_index write
		// before current tonemap_subpass swapchain_attachment_index write.
		(VkSubpassDependency){
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = tonemap_subpass_index,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
	};

	CHECKVK(vkCreateRenderPass(r->device, &(VkRenderPassCreateInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.subpassCount = subpass_count,
		.pSubpasses = (VkSubpassDescription[subpass_count]){
			[tonemap_subpass_index] = (VkSubpassDescription){
				.colorAttachmentCount = 1,
				.pColorAttachments = (VkAttachmentReference[]){
					(VkAttachmentReference){
						.attachment = swapchain_attachment_index,
						.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					},
				},
				.inputAttachmentCount = 2,
				.pInputAttachments = (VkAttachmentReference[]){
					(VkAttachmentReference){
						.attachment = transient_color_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
					(VkAttachmentReference){
						.attachment = transient_depth_attachment_index,
						.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					},
				},
			},
		},
		.attachmentCount = attachment_count,
		.pAttachments = (VkAttachmentDescription[attachment_count]){
			[swapchain_attachment_index] = (VkAttachmentDescription){
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
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = r->opt_bloom
					? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // TODO: figure out if this is good
			},
			[transient_depth_attachment_index] = (VkAttachmentDescription){
				.format = r->depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		},
		.dependencyCount = sizeof(subpass_dependencies) / sizeof(*subpass_dependencies),
		.pDependencies = subpass_dependencies,
	}, nullptr, &r->render_passes.sdr_pass));
	NAME_VK_OBJECT(r, r->render_passes.sdr_pass, VK_OBJECT_TYPE_RENDER_PASS, "sdr render pass");
}

static void init_rpasses(struct vulkan_renderer *r) {
	init_shadow_rpass(r);
	init_hdr_rpass(r);
	init_sdr_rpass(r);
}

static void deinit_rpasses(struct vulkan_renderer *r) {
	vkDestroyRenderPass(r->device, r->render_passes.sdr_pass, nullptr);
	vkDestroyRenderPass(r->device, r->render_passes.hdr_pass, nullptr);
}

static void init_transients(struct vulkan_renderer *r) {
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
				= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT // the the geometry rendering
				| VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT // for the atmosphere rendering
				| VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // for the downsampling blit
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

	r->transients.gbuffer[GBUFFER_IMAGE_DIFFUSE_O] = (struct gbuffer_image){
		.name = "Diffuse+AO",
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.image = {}, // set later
	};
	r->transients.gbuffer[GBUFFER_IMAGE_NORMAL_R_M] = (struct gbuffer_image){
		.name = "Normal+Roughness+Metallic",
		.format = VK_FORMAT_R8G8B8A8_SNORM,
		.image = {}, // set later
	};
	r->transients.gbuffer[GBUFFER_IMAGE_EMISSIVE] = (struct gbuffer_image){
		.name = "Emissive",
		.format = VK_FORMAT_R8G8B8A8_SRGB,
		.image = {}, // set later
	};

	for (size_t i = 0; i < GBUFFER_IMAGE_COUNT_; ++i) {
		r->transients.gbuffer[i].image = allocate_image(r, &(struct vulkan_image_alloc_info){
			.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			.preferred_memory_property_flags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
			.image_info = &(VkImageCreateInfo){
				.imageType = VK_IMAGE_TYPE_2D,
				.arrayLayers = 1,
				.extent = {
					.width = r->swapchain_extent.width,
					.height = r->swapchain_extent.height,
					.depth = 1,
				},
				.format = r->transients.gbuffer[i].format,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.mipLevels = 1,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage
					= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT // for atmosphere rendering and for the tonemap
					| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT // for the geometry rendering
			},
			.view_info = &(VkImageViewCreateInfo){
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = r->transients.gbuffer[i].format,
				.subresourceRange = (VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseArrayLayer = 0,
					.layerCount = 1,
					.baseMipLevel = 0,
					.levelCount = 1,
				},
			},
		});
		NAME_VK_OBJECT(r, r->transients.gbuffer[i].image.image, VK_OBJECT_TYPE_IMAGE, "GBuffer %s image",
			r->transients.gbuffer[i].name);
		NAME_VK_OBJECT(r, r->transients.gbuffer[i].image.view, VK_OBJECT_TYPE_IMAGE_VIEW, "GBuffer %s image view",
			r->transients.gbuffer[i].name);
	}

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
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.mipLevels = 1,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage
				= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT // for atmosphere rendering and for the tonemap
				| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT // for the geometry rendering
				| VK_IMAGE_USAGE_TRANSFER_SRC_BIT // for the downsample blit operation
				| VK_IMAGE_USAGE_STORAGE_BIT // for the bloom/upsample&composite bloom shader
				| VK_IMAGE_USAGE_SAMPLED_BIT, // for the downsample compute
		},
		.view_info = &(VkImageViewCreateInfo){
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseArrayLayer = 0,
				.layerCount = 1,
				.baseMipLevel = 0,
				.levelCount = 1,
			},
		},
	});

	VkExtent2D bloom_stage_extent = r->swapchain_extent;
	for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
		bloom_stage_extent.width /= 2;
		bloom_stage_extent.height /= 2;
		r->transients.bloom[i] = allocate_image(r, &(struct vulkan_image_alloc_info){
			.memory_usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
			// .preferred_memory_property_flags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
			// .allocation_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
			.image_info = &(VkImageCreateInfo){
				.imageType = VK_IMAGE_TYPE_2D,
				.arrayLayers = 1,
				.extent = {
					.width = bloom_stage_extent.width,
					.height = bloom_stage_extent.height,
					.depth = 1,
				},
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.mipLevels = 1,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage
					= VK_IMAGE_USAGE_STORAGE_BIT // for the upsample&bloom compute shader
					| VK_IMAGE_USAGE_TRANSFER_SRC_BIT // for the downsample op
					| VK_IMAGE_USAGE_TRANSFER_DST_BIT // for the downsample op
					| VK_IMAGE_USAGE_SAMPLED_BIT // for the upsample&bloom compute shader,
			},
			.view_info = &(VkImageViewCreateInfo){
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.subresourceRange = (VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseArrayLayer = 0,
					.layerCount = 1,
					.baseMipLevel = 0,
					.levelCount = 1,
				}
			},
		});
		NAME_VK_OBJECT(r, r->transients.bloom[i].image, VK_OBJECT_TYPE_IMAGE, "transient bloom #%zu image", i);
		NAME_VK_OBJECT(r, r->transients.bloom[i].view, VK_OBJECT_TYPE_IMAGE_VIEW, "transient bloom #%zu image view", i);
	}
	// PSHINE_DEBUG("initializting transients");
	NAME_VK_OBJECT(r, r->transients.color_0.image, VK_OBJECT_TYPE_IMAGE, "transient color0 image");
	NAME_VK_OBJECT(r, r->transients.color_0.view, VK_OBJECT_TYPE_IMAGE_VIEW, "transient color0 image view");
	NAME_VK_OBJECT(r, r->depth_image.image, VK_OBJECT_TYPE_IMAGE, "transient depth0 image");
	NAME_VK_OBJECT(r, r->depth_image.view, VK_OBJECT_TYPE_IMAGE_VIEW, "transient depth0 image view");
	// PSHINE_DEBUG("initiadsadsalizting transients");
}

static void deinit_transients(struct vulkan_renderer *r) {
	for (size_t i = 0; i < GBUFFER_IMAGE_COUNT_; ++i)
		deallocate_image(r, r->transients.gbuffer[i].image);
	deallocate_image(r, r->depth_image);
	deallocate_image(r, r->transients.color_0);
	for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i)
		deallocate_image(r, r->transients.bloom[i]);
}

// Framebuffers
static void init_fbufs(struct vulkan_renderer *r) {
	r->sdr_framebuffers_own = calloc(r->swapchain_image_count, sizeof(VkFramebuffer));
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		CHECKVK(vkCreateFramebuffer(r->device, &(VkFramebufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.attachmentCount = 3,
			.pAttachments = (VkImageView[]){
				r->swapchain_image_views_own[i],
				r->transients.color_0.view,
				r->depth_image.view,
			},
			.renderPass = r->render_passes.sdr_pass,
			.width = r->swapchain_extent.width,
			.height = r->swapchain_extent.height,
			.layers = 1,
		}, nullptr, &r->sdr_framebuffers_own[i]));
	}
	r->hdr_framebuffers_own = calloc(r->swapchain_image_count, sizeof(VkFramebuffer));
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		CHECKVK(vkCreateFramebuffer(r->device, &(VkFramebufferCreateInfo){
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.attachmentCount = 5,
			.pAttachments = (VkImageView[]){
				r->transients.color_0.view,
				r->depth_image.view,
				r->transients.gbuffer[GBUFFER_IMAGE_DIFFUSE_O].image.view,
				r->transients.gbuffer[GBUFFER_IMAGE_NORMAL_R_M].image.view,
				r->transients.gbuffer[GBUFFER_IMAGE_EMISSIVE].image.view,
			},
			.renderPass = r->render_passes.hdr_pass,
			.width = r->swapchain_extent.width,
			.height = r->swapchain_extent.height,
			.layers = 1,
		}, nullptr, &r->hdr_framebuffers_own[i]));
	}
}

static void deinit_fbufs(struct vulkan_renderer *r) {
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		vkDestroyFramebuffer(r->device, r->hdr_framebuffers_own[i], nullptr);
	}
	free(r->hdr_framebuffers_own);
	for (uint32_t i = 0; i < r->swapchain_image_count; ++i) {
		vkDestroyFramebuffer(r->device, r->sdr_framebuffers_own[i], nullptr);
	}
	free(r->sdr_framebuffers_own);
}

static void init_view_dep_data(struct vulkan_renderer *r, bool resize);

static void handle_swapchain_resize(struct vulkan_renderer *r) {
	vkDeviceWaitIdle(r->device);
	deinit_fbufs(r);
	deinit_transients(r);
	deinit_swapchain(r);
	reinit_swapchain(r);
	init_transients(r);
	init_fbufs(r);
	init_view_dep_data(r, true);
	for (size_t i = 0; i < r->game->star_system_count; ++i) {
		struct pshine_star_system *system = &r->game->star_systems_own[i];
		for (size_t j = 0; j < system->body_count; ++j) {
			struct pshine_celestial_body *body = system->bodies_own[j];
			if (body->type == PSHINE_CELESTIAL_BODY_PLANET) {
				struct pshine_planet *p = (void*)body;
				vkUpdateDescriptorSets(r->device, 2, (VkWriteDescriptorSet[]){
					(VkWriteDescriptorSet){
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
						.dstSet = p->graphics_data->atmo_descriptor_set,
						.dstBinding = 1,
						.dstArrayElement = 0,
						.pImageInfo = &(VkDescriptorImageInfo){
							.imageLayout = VK_IMAGE_LAYOUT_GENERAL, // SHADER_READ_ONLY_OPTIMAL
							.imageView = r->transients.color_0.view,
							.sampler = VK_NULL_HANDLE,
						}
					},
					(VkWriteDescriptorSet){
						.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						.descriptorCount = 1,
						.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
						.dstSet = p->graphics_data->atmo_descriptor_set,
						.dstBinding = 2,
						.dstArrayElement = 0,
						.pImageInfo = &(VkDescriptorImageInfo){
							.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							.imageView = r->depth_image.view,
							.sampler = VK_NULL_HANDLE
						}
					},
				}, 0, nullptr);
			}
		}
	}
}

// Pipelines

static VkShaderModule create_shader_module(struct vulkan_renderer *r, size_t size, const char *src, const char *name) {
	VkShaderModule shader_module = VK_NULL_HANDLE;
	CHECKVK(vkCreateShaderModule(r->device, &(VkShaderModuleCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = (const uint32_t*)src,
	}, nullptr, &shader_module));
	NAME_VK_OBJECT(r, shader_module, VK_OBJECT_TYPE_SHADER_MODULE, "%s", name);
	return shader_module;
}

static VkShaderModule create_shader_module_file(struct vulkan_renderer *r, const char *fname) {
	size_t size = 0;
	char *src = pshine_read_file(fname, &size);
	VkShaderModule module = create_shader_module(r, size, src, fname);
	free(src);
	return module;
}

enum graphics_pipline_output {
	GRAPHICS_PIPELINE_OUTPUT_COLOR,
	GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
	GRAPHICS_PIPELINE_OUTPUT_SHADOW,
};

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
	enum pshine_vertex_type vertex_kind;
	bool lines;
	bool triangle_strip;
	enum graphics_pipline_output output_kind;
	// int planet_specialization;
};

struct vulkan_pipeline {
	VkPipelineLayout layout;
	VkPipeline pipeline;
};

static const VkPipelineVertexInputStateCreateInfo vulkan_planet_vertex_input_state = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
		.binding = 0,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		.stride = sizeof(struct pshine_planet_vertex)
	},
	.vertexAttributeDescriptionCount = 3,
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
		},
	},
};

static const VkPipelineVertexInputStateCreateInfo vulkan_std_vertex_input_state = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = &(VkVertexInputBindingDescription){
		.binding = 0,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		.stride = sizeof(struct pshine_static_mesh_vertex)
	},
	.vertexAttributeDescriptionCount = 4,
	.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[4]){
		(VkVertexInputAttributeDescription){
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.location = 0,
			.offset = offsetof(struct pshine_static_mesh_vertex, position)
		},
		(VkVertexInputAttributeDescription){
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.location = 1,
			.offset = offsetof(struct pshine_static_mesh_vertex, normal_oct)
		},
		(VkVertexInputAttributeDescription){
			.binding = 0,
			.format = VK_FORMAT_R32_SFLOAT,
			.location = 2,
			.offset = offsetof(struct pshine_static_mesh_vertex, tangent_dia)
		},
		(VkVertexInputAttributeDescription){
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.location = 3,
			.offset = offsetof(struct pshine_static_mesh_vertex, texcoord)
		},
	},
};
static const VkPipelineVertexInputStateCreateInfo vulkan_no_vertex_input_state = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 0,
	.vertexAttributeDescriptionCount = 0,
};

static struct vulkan_pipeline create_graphics_pipeline(
	struct vulkan_renderer *r,
	const struct graphics_pipeline_info *info
) {
	VkShaderModule vert_shader_module = create_shader_module_file(r, info->vert_fname);
	// 	vert_shader_module = ;
	// 	if (info->planet_specialization) {
	// 	size_t size = 0;
	// 	char *src = pshine_read_file(info->vert_fname, &size);
	// 	CHECKVK(vkCreateShaderModule(r->device, &(VkShaderModuleCreateInfo){
	// 		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
	// 		.codeSize = size,
	// 		.pCode = (const uint32_t*)src,
	// 	}, nullptr, &vert_shader_module));
	// 	free(src);
	// } else {
	// }
	VkShaderModule frag_shader_module = create_shader_module_file(r, info->frag_fname);

	VkPipelineLayout layout;
	VkPipeline pipeline;

	CHECKVK(vkCreatePipelineLayout(r->device, &(VkPipelineLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = info->set_layout_count,
		.pSetLayouts = info->set_layouts,
		.pushConstantRangeCount = info->push_constant_range_count,
		.pPushConstantRanges = info->push_constant_ranges,
	}, nullptr, &layout));

	if (info->layout_name != nullptr)
		NAME_VK_OBJECT(r, layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, info->layout_name);

	const VkPipelineVertexInputStateCreateInfo *vertex_input_state = nullptr;
	switch (info->vertex_kind) {
	case PSHINE_VERTEX_NONE: vertex_input_state = &vulkan_no_vertex_input_state; break;
	case PSHINE_VERTEX_PLANET: vertex_input_state = &vulkan_planet_vertex_input_state; break;
	case PSHINE_VERTEX_STATIC_MESH: vertex_input_state = &vulkan_std_vertex_input_state; break;
	default: PSHINE_PANIC("vertex kind %d not implemented.", info->vertex_kind);
	}

	VkPipelineColorBlendAttachmentState forward_attachment = {
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
	};

	VkPipelineColorBlendAttachmentState gbuffer_attachment = {
		.colorWriteMask
			= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};
	VkPipelineColorBlendAttachmentState gbuffer_attachments[1 + GBUFFER_IMAGE_COUNT_] = {
		[0] = gbuffer_attachment,
		[1 + GBUFFER_IMAGE_DIFFUSE_O] = gbuffer_attachment,
		[1 + GBUFFER_IMAGE_NORMAL_R_M] = gbuffer_attachment,
		[1 + GBUFFER_IMAGE_EMISSIVE] = gbuffer_attachment,
	};

	CHECKVK(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &(VkGraphicsPipelineCreateInfo){
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pVertexInputState = vertex_input_state,
		.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.primitiveRestartEnable = VK_FALSE,
			.topology = info->triangle_strip
				? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
				: VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		},
		.pViewportState = &(VkPipelineViewportStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1, // Dynamic
			.scissorCount = 1, // Dynamic
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
			.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
			.maxDepthBounds = 1.0f,
			.minDepthBounds = 0.0f,
			.back = {},
			.front = {}
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount
				= info->output_kind == GRAPHICS_PIPELINE_OUTPUT_GBUFFER
				? 1 + GBUFFER_IMAGE_COUNT_
				: info->output_kind == GRAPHICS_PIPELINE_OUTPUT_SHADOW
				? 0
				: 1,
			.pAttachments
				= info->output_kind == GRAPHICS_PIPELINE_OUTPUT_GBUFFER
				? gbuffer_attachments
				: info->output_kind == GRAPHICS_PIPELINE_OUTPUT_SHADOW
				? nullptr
				: &forward_attachment,
		},
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]){
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vert_shader_module,
				// .pSpecializationInfo = info->planet_specialization != 0
				// 	? &(VkSpecializationInfo){
				// 		.dataSize = sizeof(int32_t),
				// 		.pData = &(int32_t){ info->planet_specialization },
				// 		.mapEntryCount = 1,
				// 		.pMapEntries = &(VkSpecializationMapEntry){
				// 			.constantID = 0,
				// 			.offset = 0,
				// 			.size = sizeof(int32_t),
				// 		},
				// 	}
				// 	: nullptr,
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = frag_shader_module,
				.pName = "main",
			},
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = (VkDynamicState[]){
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
			},
		},
		.layout = layout,
		.renderPass = info->render_pass,
		.subpass = info->subpass
	}, nullptr, &pipeline));

	if (info->pipeline_name != nullptr)
		NAME_VK_OBJECT(r, pipeline, VK_OBJECT_TYPE_PIPELINE, info->pipeline_name);

	vkDestroyShaderModule(r->device, vert_shader_module, nullptr);
	vkDestroyShaderModule(r->device, frag_shader_module, nullptr);

	return (struct vulkan_pipeline){ .pipeline = pipeline, .layout = layout };
}

static void init_pipelines(struct vulkan_renderer *r) {
	struct vulkan_pipeline mesh_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/mesh.2.vert.spv",
		.frag_fname = "build/pshine/data/shaders/mesh.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 0,
		.push_constant_range_count = 0,
		.set_layout_count = 3,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.planet_material_layout,
			r->descriptors.planet_mesh_layout,
		},
		.blend = false,
		.depth_test = true,
		.vertex_kind = PSHINE_VERTEX_PLANET,
		.layout_name = "planet mesh pipeline layout",
		.pipeline_name = "planet mesh pipeline",
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
	});
	r->pipelines.planet_mesh_pipeline = mesh_pipeline.pipeline;
	r->pipelines.planet_mesh_layout = mesh_pipeline.layout;

	struct vulkan_pipeline color_mesh_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/mesh.1.vert.spv",
		.frag_fname = "build/pshine/data/shaders/solid_color.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 0,
		.push_constant_range_count = 1,
		.push_constant_ranges = (VkPushConstantRange[]){
			(VkPushConstantRange){
				.offset = 0,
				.size = sizeof(float4),
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			}
		},
		.set_layout_count = 2,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.planet_mesh_layout,
		},
		.blend = false,
		.depth_test = true,
		.vertex_kind = PSHINE_VERTEX_PLANET,
		.layout_name = "solid color planet mesh pipeline layout",
		.pipeline_name = "solid color planet mesh pipeline",
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
	});
	r->pipelines.planet_color_mesh_pipeline = color_mesh_pipeline.pipeline;
	r->pipelines.planet_color_mesh_layout = color_mesh_pipeline.layout;
	PSHINE_DEBUG("created color_mesh_pipeline");

	struct vulkan_pipeline rings_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/rings.vert.spv",
		.frag_fname = "build/pshine/data/shaders/rings.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 0,
		.push_constant_range_count = 0,
		.set_layout_count = 2,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.rings_layout,
		},
		.blend = true,
		.depth_test = true,
		.vertex_kind = PSHINE_VERTEX_NONE,
		.layout_name = "rings pipeline layout",
		.pipeline_name = "rings pipeline",
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
	});
	r->pipelines.rings_pipeline = rings_pipeline.pipeline;
	r->pipelines.rings_layout = rings_pipeline.layout;
	PSHINE_DEBUG("created rings_pipeline");

	// struct vulkan_pipeline std_mesh_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
	// 	.vert_fname = "build/pshine/data/shaders/std_mesh.vert.spv",
	// 	.frag_fname = "build/pshine/data/shaders/std_mesh.frag.spv",
	// 	.render_pass = r->render_passes.hdr_pass,
	// 	.subpass = 0,
	// 	.push_constant_range_count = 0,
	// 	.set_layout_count = 3,
	// 	.set_layouts = (VkDescriptorSetLayout[]){
	// 		r->descriptors.global_layout,
	// 		r->descriptors.std_material_layout,
	// 		r->descriptors.std_mesh_layout,
	// 	},
	// 	.triangle_strip = false,
	// 	.blend = true,
	// 	.depth_test = true,
	// 	.vertex_kind = PSHINE_VERTEX_STATIC_MESH,
	// 	.layout_name = "std mesh pipeline layout",
	// 	.pipeline_name = "std mesh pipeline",
	// 	.gbuffer_output = true,
	// });
	r->pipelines.std_mesh_pipeline = VK_NULL_HANDLE; // std_mesh_pipeline.pipeline;
	r->pipelines.std_mesh_layout = VK_NULL_HANDLE; // std_mesh_pipeline.layout;
	PSHINE_DEBUG("created std_mesh_pipeline");

	struct vulkan_pipeline std_mesh_de_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/std_mesh.vert.spv",
		.frag_fname = "build/pshine/data/shaders/std_mesh.de.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 0,
		.push_constant_range_count = 0,
		.set_layout_count = 3,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.std_material_layout,
			r->descriptors.std_mesh_layout,
		},
		.triangle_strip = false,
		.blend = true,
		.depth_test = true,
		.vertex_kind = PSHINE_VERTEX_STATIC_MESH,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
		.layout_name = "std mesh deferred pipeline layout",
		.pipeline_name = "std mesh deferred pipeline",
	});
	r->pipelines.std_mesh_de_pipeline = std_mesh_de_pipeline.pipeline;
	r->pipelines.std_mesh_de_layout = std_mesh_de_pipeline.layout;
	PSHINE_DEBUG("created std_mesh_de_pipeline");

	struct vulkan_pipeline std_mesh_shadow_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/std_mesh.shadow.vert.spv",
		.frag_fname = "build/pshine/data/shaders/std_mesh.shadow.frag.spv",
		.render_pass = r->render_passes.shadow_pass,
		.subpass = 0,
		.push_constant_range_count = 0,
		.set_layout_count = 1,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.std_mesh_layout,
		},
		.triangle_strip = false,
		.blend = false,
		.depth_test = true,
		.vertex_kind = PSHINE_VERTEX_STATIC_MESH,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_SHADOW,
		.layout_name = "std mesh shadow pipeline layout",
		.pipeline_name = "std mesh shadow pipeline",
	});
	r->pipelines.std_mesh_shadow_pipeline = std_mesh_shadow_pipeline.pipeline;
	r->pipelines.std_mesh_shadow_layout = std_mesh_shadow_pipeline.layout;
	PSHINE_DEBUG("created std_mesh_shadow_pipeline");

	struct vulkan_pipeline light_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/blit.vert.spv",
		.frag_fname = "build/pshine/data/shaders/light.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 2,
		.push_constant_range_count = 0,
		.set_layout_count = 1,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.light_layout,
		},
		.triangle_strip = false,
		.blend = false,
		.depth_test = false,
		.vertex_kind = PSHINE_VERTEX_NONE,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_COLOR,
		.layout_name = "deferred light pipeline layout",
		.pipeline_name = "deferred light pipeline",
	});
	r->pipelines.light_pipeline = light_pipeline.pipeline;
	r->pipelines.light_layout = light_pipeline.layout;
	PSHINE_DEBUG("created light_pipeline");

	struct vulkan_pipeline skybox_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/skybox.vert.spv",
		.frag_fname = "build/pshine/data/shaders/skybox.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 0,
		.push_constant_range_count = 1,
		.push_constant_ranges = (VkPushConstantRange[]) {
			(VkPushConstantRange){
				.offset = 0,
				.size = sizeof(float4x4) * 2,
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
			},
		},
		.set_layout_count = 1,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.skybox_layout,
		},
		.blend = false,
		.depth_test = true,
		.vertex_kind = false,
		.lines = false,
		.triangle_strip = true,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_GBUFFER,
		.layout_name = "skybox pipeline layout",
		.pipeline_name = "skybox pipeline",
	});
	r->pipelines.skybox_layout = skybox_pipeline.layout;
	r->pipelines.skybox_pipeline = skybox_pipeline.pipeline;

	struct vulkan_pipeline atmo_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/atmo.vert.spv",
		.frag_fname = "build/pshine/data/shaders/atmo.frag.spv",
		.render_pass = r->render_passes.hdr_pass,
		.subpass = 1,
		.push_constant_range_count = 0,
		.set_layout_count = 3,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.global_layout,
			r->descriptors.planet_material_layout,
			r->descriptors.atmo_layout,
		},
		.blend = false,
		.depth_test = false,
		.vertex_kind = false,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_COLOR,
		.layout_name = "atmosphere pipeline layout",
		.pipeline_name = "atmosphere pipeline",
	});
	r->pipelines.atmo_pipeline = atmo_pipeline.pipeline;
	r->pipelines.atmo_layout = atmo_pipeline.layout;
	PSHINE_DEBUG("created atmo_pipeline");

	struct vulkan_pipeline blit_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
		.vert_fname = "build/pshine/data/shaders/blit.vert.spv",
		.frag_fname = "build/pshine/data/shaders/blit.frag.spv",
		.render_pass = r->render_passes.sdr_pass,
		.subpass = 0,
		.push_constant_range_count = 1,
		.push_constant_ranges = (VkPushConstantRange[]) {
			(VkPushConstantRange){
				.offset = 0,
				.size = sizeof(struct pshine_graphics_settings),
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		},
		.set_layout_count = 1,
		.set_layouts = (VkDescriptorSetLayout[]){
			r->descriptors.blit_layout,
		},
		.blend = false,
		.depth_test = false,
		.vertex_kind = false,
		.output_kind = GRAPHICS_PIPELINE_OUTPUT_COLOR,
		.layout_name = "blit pipeline layout",
		.pipeline_name = "blit pipeline",
	});
	r->pipelines.blit_pipeline = blit_pipeline.pipeline;
	r->pipelines.blit_layout = blit_pipeline.layout;

	// struct vulkan_pipeline line_gizmo_pipeline = create_graphics_pipeline(r, &(struct graphics_pipeline_info){
	// 	.vert_fname = "build/pshine/data/line_gizmo.vert.spv",
	// 	.frag_fname = "build/pshine/data/line_gizmo.frag.spv",
	// 	.render_pass = r->render_passes.main_pass,
	// 	.subpass = 1,
	// 	.push_constant_range_count = 0,
	// 	.set_layout_count = 3,
	// 	.set_layouts = (VkDescriptorSetLayout[]){
	// 		r->descriptors.global_layout,
	// 		r->descriptors.static_mesh_layout,
	// 	},
	// 	.blend = false,
	// 	.depth_test = false,
	// 	.vertex_input = true,
	// 	.lines = true,
	// 	.layout_name = "line gizmo pipeline layout",
	// 	.pipeline_name = "line gizmo pipeline",
	// });
	// r->pipelines.line_gizmo_layout = line_gizmo_pipeline.layout;
	// r->pipelines.line_gizmo_pipeline = line_gizmo_pipeline.pipeline;

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
		}, nullptr, &r->pipelines.atmo_lut_layout);

		VkShaderModule comp_shader_module = create_shader_module_file(r, "build/pshine/data/shaders/atmo_lut.comp.spv");
		vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.layout = r->pipelines.atmo_lut_layout,
			.stage = (VkPipelineShaderStageCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pSpecializationInfo = nullptr,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = comp_shader_module,
				.pName = "main",
			},
		}, nullptr, &r->pipelines.atmo_lut_pipeline);
		NAME_VK_OBJECT(r, r->pipelines.atmo_lut_pipeline, VK_OBJECT_TYPE_PIPELINE, "atmo lut pipeline");
		vkDestroyShaderModule(r->device, comp_shader_module, nullptr);
	}
	{
		vkCreatePipelineLayout(r->device, &(VkPipelineLayoutCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &r->descriptors.upsample_bloom_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = (VkPushConstantRange[]){
				(VkPushConstantRange){
					.offset = 0,
					.size = sizeof(struct pshine_graphics_settings),
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				},
			},
		}, nullptr, &r->pipelines.upsample_bloom_layout);
		NAME_VK_OBJECT(r, r->pipelines.upsample_bloom_layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "upsample&bloom pipeline layout");

		VkShaderModule comp_shader_module = create_shader_module_file(r, "build/pshine/data/shaders/bloom_upsample.comp.spv");
		vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.layout = r->pipelines.upsample_bloom_layout,
			.stage = (VkPipelineShaderStageCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pSpecializationInfo = nullptr,
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = comp_shader_module,
				.pName = "main",
			},
		}, nullptr, &r->pipelines.upsample_bloom_pipeline);
		NAME_VK_OBJECT(r, r->pipelines.upsample_bloom_pipeline, VK_OBJECT_TYPE_PIPELINE, "upsample&bloom pipeline");
		vkDestroyShaderModule(r->device, comp_shader_module, nullptr);
	}
	{
		vkCreatePipelineLayout(r->device, &(VkPipelineLayoutCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &r->descriptors.downsample_bloom_layout,
			.pushConstantRangeCount = 1,
			.pPushConstantRanges = (VkPushConstantRange[]){
				(VkPushConstantRange){
					.offset = 0,
					.size = sizeof(struct pshine_graphics_settings),
					.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				},
			},
		}, nullptr, &r->pipelines.downsample_bloom_layout);
		NAME_VK_OBJECT(r, r->pipelines.downsample_bloom_layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "downsample&bloom pipeline layout");

		VkShaderModule comp_shader_module = create_shader_module_file(r, "build/pshine/data/shaders/bloom_downsample.comp.spv");
		vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.layout = r->pipelines.downsample_bloom_layout,
			.stage = (VkPipelineShaderStageCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pSpecializationInfo = &(VkSpecializationInfo){
					.mapEntryCount = 1,
					.pMapEntries = (VkSpecializationMapEntry[]){
						(VkSpecializationMapEntry){ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) },
					},
					.dataSize = sizeof(uint32_t),
					.pData = &(int){ 1 },
				},
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = comp_shader_module,
				.pName = "main",
			},
		}, nullptr, &r->pipelines.first_downsample_bloom_pipeline);
		NAME_VK_OBJECT(r, r->pipelines.first_downsample_bloom_pipeline, VK_OBJECT_TYPE_PIPELINE, "first-downsample&bloom pipeline");

		vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &(VkComputePipelineCreateInfo){
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.layout = r->pipelines.downsample_bloom_layout,
			.stage = (VkPipelineShaderStageCreateInfo){
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.pSpecializationInfo = &(VkSpecializationInfo){
					.mapEntryCount = 1,
					.pMapEntries = (VkSpecializationMapEntry[]){
						(VkSpecializationMapEntry){ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) },
					},
					.dataSize = sizeof(uint32_t),
					.pData = &(int){ 0 },
				},
				.stage = VK_SHADER_STAGE_COMPUTE_BIT,
				.module = comp_shader_module,
				.pName = "main",
			},
		}, nullptr, &r->pipelines.downsample_bloom_pipeline);
		NAME_VK_OBJECT(r, r->pipelines.downsample_bloom_pipeline, VK_OBJECT_TYPE_PIPELINE, "downsample&bloom pipeline");
		vkDestroyShaderModule(r->device, comp_shader_module, nullptr);
	}
}

static void deinit_pipelines(struct vulkan_renderer *r) {
	vkDestroyPipeline(r->device, r->pipelines.planet_mesh_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.planet_mesh_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.planet_color_mesh_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.planet_color_mesh_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.atmo_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.atmo_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.atmo_lut_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.atmo_lut_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.blit_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.blit_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.light_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.light_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.rings_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.rings_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.skybox_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.skybox_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.std_mesh_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.std_mesh_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.std_mesh_de_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.std_mesh_de_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.std_mesh_shadow_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.std_mesh_shadow_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.upsample_bloom_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.upsample_bloom_layout, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.first_downsample_bloom_pipeline, nullptr);
	vkDestroyPipeline(r->device, r->pipelines.downsample_bloom_pipeline, nullptr);
	vkDestroyPipelineLayout(r->device, r->pipelines.downsample_bloom_layout, nullptr);
}


// Command buffers

static void init_cmdbufs(struct vulkan_renderer *r) {
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
	}, nullptr, &r->command_pool_graphics));
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
	}, nullptr, &r->command_pool_transfer));
	CHECKVK(vkCreateCommandPool(r->device, &(VkCommandPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		// TODO: make sure that this flag is removed if doing per frame compute
		// maybe create another pool for transient compute?
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = r->queue_families[QUEUE_COMPUTE],
	}, nullptr, &r->command_pool_compute));
}

static void deinit_cmdbufs(struct vulkan_renderer *r) {
	vkDestroyCommandPool(r->device, r->command_pool_graphics, nullptr);
	vkDestroyCommandPool(r->device, r->command_pool_transfer, nullptr);
	vkDestroyCommandPool(r->device, r->command_pool_compute, nullptr);
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
	}, nullptr, &r->descriptors.pool);
	NAME_VK_OBJECT(r, r->descriptors.pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "descriptor pool 1");
	vkCreateDescriptorPool(r->device, &(VkDescriptorPoolCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1024,
		.poolSizeCount = 1,
		.pPoolSizes = (VkDescriptorPoolSize[]){
			(VkDescriptorPoolSize){ .descriptorCount = 256, .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
		}
	}, nullptr, &r->descriptors.pool_imgui);
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
	}, nullptr, &r->descriptors.global_layout);
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
	}, nullptr, &r->descriptors.planet_material_layout);
	NAME_VK_OBJECT(r, r->descriptors.planet_material_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "planet material descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	}, nullptr, &r->descriptors.planet_mesh_layout);
	NAME_VK_OBJECT(r, r->descriptors.planet_mesh_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "planet static mesh descriptor set layout");

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
	}, nullptr, &r->descriptors.atmo_layout);
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
	}, nullptr, &r->descriptors.atmo_lut_layout);
	NAME_VK_OBJECT(r, r->descriptors.atmo_lut_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "atmosphere lut descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){ // color0
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // depth0
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // global_uniforms
				.binding = 2,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, nullptr, &r->descriptors.blit_layout);
	NAME_VK_OBJECT(r, r->descriptors.blit_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "blit descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 5,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){ // depth0
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // diffuse_o
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // normal_r_m
				.binding = 2,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // emissive
				.binding = 3,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			(VkDescriptorSetLayoutBinding){ // global_uniforms
				.binding = 4,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, nullptr, &r->descriptors.light_layout);
	NAME_VK_OBJECT(r, r->descriptors.light_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "light descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, nullptr, &r->descriptors.rings_layout);
	NAME_VK_OBJECT(r, r->descriptors.rings_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "rings descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, nullptr, &r->descriptors.skybox_layout);
	NAME_VK_OBJECT(r, r->descriptors.skybox_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "skybox descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		}
	}, nullptr, &r->descriptors.upsample_bloom_layout);
	NAME_VK_OBJECT(r, r->descriptors.upsample_bloom_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "upsample&bloom descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
			},
		}
	}, nullptr, &r->descriptors.downsample_bloom_layout);
	NAME_VK_OBJECT(r, r->descriptors.downsample_bloom_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "downsample&bloom descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 5,
		.pBindings = (VkDescriptorSetLayoutBinding[5]){
			(VkDescriptorSetLayoutBinding){
				.binding = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			// Diffuse
			(VkDescriptorSetLayoutBinding){
				.binding = 1,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			// Roughness + Metallic + Occlusion
			(VkDescriptorSetLayoutBinding){
				.binding = 2,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			// Normal
			(VkDescriptorSetLayoutBinding){
				.binding = 3,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
			// Emission
			(VkDescriptorSetLayoutBinding){
				.binding = 4,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			},
		}
	}, nullptr, &r->descriptors.std_material_layout);
	NAME_VK_OBJECT(r, r->descriptors.std_material_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "std material descriptor set layout");

	vkCreateDescriptorSetLayout(r->device, &(VkDescriptorSetLayoutCreateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &(VkDescriptorSetLayoutBinding){
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		}
	}, nullptr, &r->descriptors.std_mesh_layout);
	NAME_VK_OBJECT(r, r->descriptors.std_mesh_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "std static mesh descriptor set layout");
}

static void deinit_descriptors(struct vulkan_renderer *r) {
	vkDestroyDescriptorPool(r->device, r->descriptors.pool, nullptr);
	vkDestroyDescriptorPool(r->device, r->descriptors.pool_imgui, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.global_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.planet_material_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.planet_mesh_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.atmo_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.atmo_lut_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.blit_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.rings_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.skybox_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.downsample_bloom_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.upsample_bloom_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.std_material_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.std_mesh_layout, nullptr);
	vkDestroyDescriptorSetLayout(r->device, r->descriptors.light_layout, nullptr);
}


/// Initialize data that's dependent on the swapchain (stuff that we need to redo when resizing the window)
static void init_view_dep_data(struct vulkan_renderer *r, bool resize) {
	vkUpdateDescriptorSets(r->device, 3, (VkWriteDescriptorSet[3]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.blit_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->transients.color_0.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.blit_descriptor_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->depth_image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = r->data.blit_descriptor_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = r->data.global_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct global_uniform_data),
			},
		},
	}, 0, nullptr);

	vkUpdateDescriptorSets(r->device, 5, (VkWriteDescriptorSet[5]){
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.light_descriptor_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->depth_image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.light_descriptor_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->transients.gbuffer[GBUFFER_IMAGE_DIFFUSE_O].image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.light_descriptor_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->transients.gbuffer[GBUFFER_IMAGE_NORMAL_R_M].image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
			.dstSet = r->data.light_descriptor_set,
			.dstBinding = 3,
			.dstArrayElement = 0,
			.pImageInfo = &(VkDescriptorImageInfo){
				.imageView = r->transients.gbuffer[GBUFFER_IMAGE_EMISSIVE].image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.sampler = VK_NULL_HANDLE,
			},
		},
		(VkWriteDescriptorSet){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.dstSet = r->data.light_descriptor_set,
			.dstBinding = 4,
			.dstArrayElement = 0,
			.pBufferInfo = &(VkDescriptorBufferInfo){
				.buffer = r->data.global_uniform_buffer.buffer,
				.offset = 0,
				.range = sizeof(struct global_uniform_data),
			},
		},
	}, 0, nullptr);

	VkDescriptorSetLayout upsample_bloom_layout_copies[BLOOM_STAGE_COUNT];
	for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
		upsample_bloom_layout_copies[i] = r->descriptors.upsample_bloom_layout;
	}

	VkDescriptorSetLayout downsample_bloom_layout_copies[BLOOM_STAGE_COUNT];
	for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
		downsample_bloom_layout_copies[i] = r->descriptors.downsample_bloom_layout;
	}

	if (!resize) {
		CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = r->descriptors.pool,
			.descriptorSetCount = BLOOM_STAGE_COUNT,
			.pSetLayouts = upsample_bloom_layout_copies,
		}, r->data.upsample_bloom_descriptor_sets));

		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i)
			NAME_VK_OBJECT(r, r->data.upsample_bloom_descriptor_sets[i], VK_OBJECT_TYPE_DESCRIPTOR_SET, "upsample&bloom #%zu ds", i);

		CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = r->descriptors.pool,
			.descriptorSetCount = BLOOM_STAGE_COUNT,
			.pSetLayouts = downsample_bloom_layout_copies,
		}, r->data.downsample_bloom_descriptor_sets));

		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i)
			NAME_VK_OBJECT(r, r->data.downsample_bloom_descriptor_sets[i], VK_OBJECT_TYPE_DESCRIPTOR_SET, "downsample&bloom #%zu ds", i);
	}

	{
		// ds[i] reads from i and writes to i-1
		// VkDescriptorImageInfo
		VkWriteDescriptorSet bloom_ds_writes[BLOOM_STAGE_COUNT * 2] = {};
		VkDescriptorImageInfo bloom_ds_images[BLOOM_STAGE_COUNT * 2] = {};
		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
			bloom_ds_writes[2 * i + 0] = (VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.upsample_bloom_descriptor_sets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.pImageInfo = (bloom_ds_images[2 * i + 0] = (VkDescriptorImageInfo){
					.imageView = r->transients.bloom[i].view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.sampler = r->bloom_mipmap_sampler,
				}, &bloom_ds_images[2 * i + 0]),
			};
			bloom_ds_writes[2 * i + 1] = (VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.dstSet = r->data.upsample_bloom_descriptor_sets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.pImageInfo = (bloom_ds_images[2 * i + 1] = (VkDescriptorImageInfo){
					.imageView = i == 0 ? r->transients.color_0.view : r->transients.bloom[i - 1].view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.sampler = VK_NULL_HANDLE,
				}, &bloom_ds_images[2 * i + 1]),
			};
			// PSHINE_DEBUG("ds write: #%zu<-%p %zu<-%p", 2*i+0, r->transients.bloom[i].view, 2*i+1,i==0?r->transients.color_0.view: r->transients.bloom[i-1].view);
		}
		vkUpdateDescriptorSets(r->device, BLOOM_STAGE_COUNT * 2, bloom_ds_writes, 0, nullptr);
	}

	{
		// ds[i] reads from i and writes to i+1
		VkWriteDescriptorSet bloom_ds_writes[BLOOM_STAGE_COUNT * 2] = {};
		VkDescriptorImageInfo bloom_ds_images[BLOOM_STAGE_COUNT * 2] = {};
		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
			bloom_ds_writes[2 * i + 0] = (VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.dstSet = r->data.downsample_bloom_descriptor_sets[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.pImageInfo = (bloom_ds_images[2 * i + 0] = (VkDescriptorImageInfo){
					.imageView = i == 0 ? r->transients.color_0.view : r->transients.bloom[i - 1].view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.sampler = r->bloom_mipmap_sampler,
				}, &bloom_ds_images[2 * i + 0]),
			};
			bloom_ds_writes[2 * i + 1] = (VkWriteDescriptorSet){
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.dstSet = r->data.downsample_bloom_descriptor_sets[i],
				.dstBinding = 1,
				.dstArrayElement = 0,
				.pImageInfo = (bloom_ds_images[2 * i + 1] = (VkDescriptorImageInfo){
					.imageView = r->transients.bloom[i].view,
					.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
					.sampler = VK_NULL_HANDLE,
				}, &bloom_ds_images[2 * i + 1]),
			};
			// PSHINE_DEBUG("ds write: #%zu<-%p %zu<-%p", 2*i+0, r->transients.bloom[i].view, 2*i+1,i==0?r->transients.color_0.view: r->transients.bloom[i-1].view);
		}

		vkUpdateDescriptorSets(r->device, BLOOM_STAGE_COUNT * 2, bloom_ds_writes, 0, nullptr);
	}
}

// Game data

static void init_data(struct vulkan_renderer *r) {
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
	}, nullptr, &r->atmo_lut_sampler);
	NAME_VK_OBJECT(r, r->atmo_lut_sampler, VK_OBJECT_TYPE_SAMPLER, "atmo lut sampler");

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
	}, nullptr, &r->material_texture_sampler);
	NAME_VK_OBJECT(r, r->atmo_lut_sampler, VK_OBJECT_TYPE_SAMPLER, "material texture sampler");

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
	}, nullptr, &r->skybox_sampler);
	NAME_VK_OBJECT(r, r->atmo_lut_sampler, VK_OBJECT_TYPE_SAMPLER, "skybox sampler");

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
	}, nullptr, &r->bloom_mipmap_sampler);
	NAME_VK_OBJECT(r, r->atmo_lut_sampler, VK_OBJECT_TYPE_SAMPLER, "bloom mipmap sampler");

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

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.global_layout
	}, &r->data.global_descriptor_set));
	NAME_VK_OBJECT(r, r->data.global_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "global ds");

	vkUpdateDescriptorSets(r->device, 1, (VkWriteDescriptorSet[1]){
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
	}, 0, nullptr);

	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.blit_layout
	}, &r->data.blit_descriptor_set));
	NAME_VK_OBJECT(r, r->data.blit_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "blit ds");


	CHECKVK(vkAllocateDescriptorSets(r->device, &(VkDescriptorSetAllocateInfo){
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptors.pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->descriptors.light_layout
	}, &r->data.light_descriptor_set));
	NAME_VK_OBJECT(r, r->data.light_descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, "blit ds");

	init_view_dep_data(r, false);
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
	}, nullptr, &f->sync.image_avail_semaphore));
	NAME_VK_OBJECT(r, f->sync.image_avail_semaphore, VK_OBJECT_TYPE_SEMAPHORE, "image avail semaphore for frame %u", frame_index);

	CHECKVK(vkCreateSemaphore(r->device, &(VkSemaphoreCreateInfo){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	}, nullptr, &f->sync.render_finish_semaphore));
	NAME_VK_OBJECT(r, f->sync.render_finish_semaphore, VK_OBJECT_TYPE_SEMAPHORE, "render finish semaphore for frame %u", frame_index);

	CHECKVK(vkCreateFence(r->device, &(VkFenceCreateInfo){
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT
	}, nullptr, &f->sync.in_flight_fence));
	NAME_VK_OBJECT(r, f->sync.in_flight_fence, VK_OBJECT_TYPE_FENCE, "in flight fence for frame %u", frame_index);
}

void deinit_frame(struct vulkan_renderer *r, struct per_frame_data *f) {
	vkDestroyFence(r->device, f->sync.in_flight_fence, nullptr);
	vkDestroySemaphore(r->device, f->sync.render_finish_semaphore, nullptr);
	vkDestroySemaphore(r->device, f->sync.image_avail_semaphore, nullptr);
}

static void init_frames(struct vulkan_renderer *r) {
	init_data(r);
	size_t planet_count = 0;
	for (size_t i = 0; i < r->game->star_system_count; ++i) {
		struct pshine_star_system *system = &r->game->star_systems_own[i];
		for (size_t j = 0; j < system->body_count; ++j) {
			struct pshine_celestial_body *b = system->bodies_own[j];
			planet_count += b->type == PSHINE_CELESTIAL_BODY_PLANET;
		}
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
	// deallocate_buffer(r, r->data.material_uniform_buffer);
	// deallocate_buffer(r, r->data.atmo_uniform_buffer);
}

// ImGui

static void check_vk_result_imgui(VkResult res) { CHECKVK(res); }

static PFN_vkVoidFunction vulkan_loader_func_imgui(const char *name, void *user) {
	struct vulkan_renderer *r = user;
	PFN_vkVoidFunction instanceAddr = vkGetInstanceProcAddr(r->instance, name);
	if (instanceAddr) return instanceAddr;
	PFN_vkVoidFunction deviceAddr = vkGetDeviceProcAddr(r->device, name);
	return deviceAddr;
}

static void init_imgui(struct vulkan_renderer *r) {
	ImGuiContext *ctx = ImGui_CreateContext(nullptr);
	ImGui_SetCurrentContext(ctx);
	ImGuiIO *io = ImGui_GetIO();
	io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io->ConfigDockingTransparentPayload = true;

	ImGui_StyleColorsDark(nullptr);

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
		.Subpass = 0,
		.MinImageCount = FRAMES_IN_FLIGHT,
		.ImageCount = FRAMES_IN_FLIGHT,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Allocator = nullptr,
		.CheckVkResultFn = &check_vk_result_imgui,
		.RenderPass = r->render_passes.sdr_pass
	});
}

static void deinit_imgui(struct vulkan_renderer *r) {
	cImGui_ImplVulkan_Shutdown();
	cImGui_ImplGlfw_Shutdown();
	ImGui_DestroyContext(ImGui_GetCurrentContext());
}

static void deinit_star_system(struct vulkan_renderer *r, struct pshine_star_system *system) {

	for (uint32_t i = 0; i < system->body_count; ++i) {
		struct pshine_celestial_body *b = system->bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			deallocate_buffer(r, p->graphics_data->uniform_buffer);
			deallocate_buffer(r, p->graphics_data->atmo_uniform_buffer);
			deallocate_buffer(r, p->graphics_data->material_uniform_buffer);
			deallocate_image(r, p->graphics_data->atmo_lut);
			if (b->rings.has_rings) deallocate_buffer(r, p->graphics_data->rings_uniform_buffer);
			vkFreeCommandBuffers(r->device, r->command_pool_compute, 1, &p->graphics_data->compute_cmdbuf);
			free(p->graphics_data);
		} else if (b->type == PSHINE_CELESTIAL_BODY_STAR) {
			struct pshine_star *p = (void *)b;
			deallocate_buffer(r, p->graphics_data->uniform_buffer);
			free(p->graphics_data);
		}
	}
}

void pshine_deinit_renderer(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;

	vkDeviceWaitIdle(r->device);
	cImGui_ImplVulkan_DestroyFontsTexture();

	for (size_t i = 0; i < r->game->star_system_count; ++i) {
		deinit_star_system(r, &r->game->star_systems_own[i]);
	}
	
	for (size_t i = 0; i < r->game->ships.dyna.count; ++i) {
		if (r->game->ships.ptr[i]._alive_marker != (size_t)-1) continue;
		deallocate_buffer(r, r->game->ships.ptr[i].graphics_data->uniform_buffer);
	}

	vkDestroySampler(r->device, r->skybox_sampler, nullptr);
	vkDestroySampler(r->device, r->atmo_lut_sampler, nullptr);
	vkDestroySampler(r->device, r->material_texture_sampler, nullptr);
	vkDestroySampler(r->device, r->bloom_mipmap_sampler, nullptr);

	for (size_t i = 0; i < r->sphere_mesh_count; ++i)
		destroy_mesh(r, &r->own_sphere_meshes[i]);
	free(r->own_sphere_meshes);

	for (size_t i = 0; i < r->model_store.dyna.count; ++i) {
		if (r->model_store.ptr[i]._alive_marker != (size_t)-1) continue;
		struct vulkan_mesh_model *model = &r->model_store.ptr[i].model;
		for (size_t j = 0; j < model->part_count; ++j) {
			destroy_mesh(r, &model->parts_own[j].mesh);
		}
		free(model->parts_own);
		for (size_t j = 0; j < model->material_count; ++j) {
			deallocate_buffer(r, model->materials_own[j].uniform_buffer);
			deallocate_image(r, model->materials_own[j].images.ao_metallic_roughness);
			deallocate_image(r, model->materials_own[j].images.diffuse);
			deallocate_image(r, model->materials_own[j].images.emissive);
			deallocate_image(r, model->materials_own[j].images.normal);
		}
		free(model->materials_own);
		free(r->model_store.ptr[i].fpath_own);
		r->model_store.ptr[i].fpath_own = nullptr;
	}
	pshine_free_dyna_(&r->model_store.dyna);

	for (size_t i = 0; i < r->image_store.dyna.count; ++i) {
		if (r->image_store.ptr[i]._alive_marker != (size_t)-1) continue;
		deallocate_image(r, r->image_store.ptr[i].image);
		free(r->image_store.ptr[i].fpath_own);
		r->image_store.ptr[i].fpath_own = nullptr;
	}
	pshine_free_dyna_(&r->image_store.dyna);

	deinit_imgui(r);
	deinit_frames(r);
	deinit_ubufs(r);
	deinit_sync(r);
	deinit_cmdbufs(r);
	deinit_fbufs(r);
	deinit_transients(r);
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

static inline size_t select_celestial_body_lod(
	struct vulkan_renderer *r,
	struct pshine_celestial_body *b,
	double3 cam_pos
) {
	double3 body_pos = SCSd3_WCSp3(b->position);
	double radius = SCSd_WCSd(b->radius);
	double d = double3mag(double3sub(body_pos, cam_pos));
	double a = radius / fabs(d - radius);
	if (a <= SCSd_WCSd(r->lod_ranges[3])) return 4;
	if (a <= SCSd_WCSd(r->lod_ranges[2])) return 3;
	if (a <= SCSd_WCSd(r->lod_ranges[1])) return 2;
	if (a <= SCSd_WCSd(r->lod_ranges[0])) return 1;
	return 0;
}

// static void render_celestial_body(
// 	struct vulkan_renderer *r,
// 	struct pshine_celestial_body *b,
// 	struct per_frame_data *f,
// 	uint32_t current_frame,
// 	double3 camera_pos_scs,
// 	uint32_t dset_index,
// 	VkDescriptorSet dset
// ) {
// }

static void do_frame(
	struct vulkan_renderer *r,
	uint32_t current_frame,
	uint32_t image_index,
	size_t frame_number
) {
	struct per_frame_data *f = &r->frames[current_frame];

	double3 camera_pos_scs = SCSd3_WCSp3(r->game->camera_position);

	double aspect_ratio = r->swapchain_extent.width /(double) r->swapchain_extent.height;

	float3x3 cam_rot_3mat32;
	setfloat3x3rotationR(&cam_rot_3mat32, floatRinverse(floatRvs(r->game->camera_orientation.values)));

	// view matrix with world (scs) transform
	double4x4 view_mat = {};
	{
		double4x4 rot_mat;
		rot_mat.v4s[0] = double4xyz3w(double3_float3(cam_rot_3mat32.v3s[0]), 0.0);
		rot_mat.v4s[1] = double4xyz3w(double3_float3(cam_rot_3mat32.v3s[1]), 0.0);
		rot_mat.v4s[2] = double4xyz3w(double3_float3(cam_rot_3mat32.v3s[2]), 0.0);
		rot_mat.v4s[3] = double4xyz3w(double3v0(), 1.0);

		double4x4 trans_mat;
		setdouble4x4trans(&trans_mat, double3neg(camera_pos_scs));

		view_mat = trans_mat;
		double4x4mul(&view_mat, &rot_mat);
	}
	
	// view matrix at ⟨0,0,0⟩
	float4x4 local_view_mat32 = {};
	{
		float4x4 rot_mat32;
		rot_mat32.v4s[0] = float4xyz3w(cam_rot_3mat32.v3s[0], 0.0);
		rot_mat32.v4s[1] = float4xyz3w(cam_rot_3mat32.v3s[1], 0.0);
		rot_mat32.v4s[2] = float4xyz3w(cam_rot_3mat32.v3s[2], 0.0);
		rot_mat32.v4s[3] = float4xyz3w(float3v0(), 1.0);
		local_view_mat32 = rot_mat32;
	}
	// setdouble4x4lookat(
	// 	&view_mat,
	// 	camera_pos_scs,
	// 	double3add(camera_pos_scs, double3vs(r->game->camera_forward.values)),
	// 	double3xyz(0.0, 1.0, 0.0)
	// );

	// float4x4trans(&view_mat, float3neg(float3vs(r->game->camera_position.values)));
	double4x4 near_proj_mat = {};
	setdouble4x4persp(&near_proj_mat, r->game->actual_camera_fov, aspect_ratio, 0.0001);
	float4x4 near_proj_mat32 = float4x4_double4x4(near_proj_mat);

	double4x4 proj_mat = {};
	struct double4x4persp_info persp_info = setdouble4x4persp(
		&proj_mat, r->game->actual_camera_fov, aspect_ratio, 0.0001);
	float4x4 proj_mat32 = float4x4_double4x4(proj_mat);

	float4x4 inv_proj_mat32 = {};
	float4x4invert(proj_mat32.vvs, inv_proj_mat32.vvs);
	float4x4 inv_view_mat32 = {};
	float4x4invert(local_view_mat32.vvs, inv_view_mat32.vvs);

	{
		float3 cam_x = floatRapply(floatRvs(r->game->camera_orientation.values), float3xyz(1, 0, 0));
		float3 cam_y = floatRapply(floatRvs(r->game->camera_orientation.values), float3xyz(0, 1, 0));
		// float3 cam_z = floatRapply(floatRvs(r->game->camera_orientation.values), float3xyz(0, 0, 1));
		double3 cam_pos = camera_pos_scs;
		double3 sun_pos = double3v0();
		struct global_uniform_data new_data = {
			.camera = float4xyz3w(float3_double3(cam_pos), persp_info.znear),
			.camera_right = float4xyz3w(cam_x, persp_info.plane.x),
			.camera_up = float4xyz3w(cam_y, persp_info.plane.y),
			.sun = float4xyz3w(float3_double3(double3norm(double3sub(sun_pos, cam_pos))), 1.0f),
			.inv_proj = inv_proj_mat32,
			.inv_view = inv_view_mat32,
			.local_view = local_view_mat32,
		};
		char *data;
		vmaMapMemory(r->allocator, r->data.global_uniform_buffer.allocation, (void**)&data);
		data += get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame;
		memcpy(data, &new_data, sizeof(new_data));
		vmaUnmapMemory(r->allocator, r->data.global_uniform_buffer.allocation);
	}

	for (size_t i = 0; i < r->game->ships.dyna.count; ++i) {
		if (r->game->ships.ptr[i]._alive_marker != (size_t)-1) continue;
		struct pshine_ship *ship = &r->game->ships.ptr[i];
		double3 body_pos_scs = SCSd3_WCSp3(ship->position);

		struct std_mesh_uniform_data new_data = {};

		float3x3 model_rot_3mat;
		setfloat3x3rotationR(&model_rot_3mat, floatRvs(ship->orientation.values));
		float4x4 model_rot_mat32;
		model_rot_mat32.v4s[0] = float4xyz3w(model_rot_3mat.v3s[0], 0.0f);
		model_rot_mat32.v4s[1] = float4xyz3w(model_rot_3mat.v3s[1], 0.0f);
		model_rot_mat32.v4s[2] = float4xyz3w(model_rot_3mat.v3s[2], 0.0f);
		model_rot_mat32.v4s[3] = float4xyz3w(float3v0(), 1.0f);
		
		double4x4 model_rot_mat = double4x4_float4x4(model_rot_mat32);
		double4x4 model_trans_mat;
		setdouble4x4trans(&model_trans_mat, SCSd3_WCSp3(ship->position));
		double4x4 model_scale_mat;
		setdouble4x4scale(&model_scale_mat, double3v(ship->scale * PSHINE_SCS_FACTOR));

		double4x4 model_mat = {};
		setdouble4x4iden(&model_mat);
		double4x4mul(&model_mat, &ship->graphics_data->model.transform);
		double4x4mul(&model_mat, &model_rot_mat);
		double4x4mul(&model_mat, &model_scale_mat);
		double4x4mul(&model_mat, &model_trans_mat);

		double4x4 model_view_mat = model_mat;
		double4x4mul(&model_view_mat, &view_mat);

		float4x4 unscaled_model_mat32 =
			float4x4_double4x4(ship->graphics_data->model.transform);
		float4x4mul(&unscaled_model_mat32, &model_rot_mat32);
		// float4x4mul(&unscaled_model_mat32, &local_view_mat32);
		// unscaled_model_mat32.v4s[3] = float4xyz3w(
		// 	float3_double3(double3sub(body_pos_scs, camera_pos_scs)),
		// 	1.0
		// );

		new_data.model_view = float4x4_double4x4(model_view_mat);
		new_data.model = float4x4_double4x4(model_mat);
		new_data.unscaled_model = unscaled_model_mat32;
		new_data.proj = near_proj_mat32;

		double3 sun_pos = double3v0();
		new_data.sun = float4xyz3w(float3_double3(double3norm(double3sub(sun_pos, body_pos_scs))), 1.0f);

		new_data.rel_cam_pos = float4xyz3w(float3_double3(double3sub(camera_pos_scs, body_pos_scs)), 0.0);

		vmaCopyMemoryToAllocation(
			r->allocator, &new_data, ship->graphics_data->uniform_buffer.allocation,
			get_padded_uniform_buffer_size(r, sizeof(struct std_mesh_uniform_data)) * current_frame,
			sizeof(new_data)
		);
	}
	
	struct pshine_star_system *current_system = &r->game->star_systems_own[r->game->current_star_system];

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];

		// Upload static mesh data.
		{
			struct planet_mesh_uniform_data new_data = {};
			double4x4 model_mat = {};
			setdouble4x4iden(&model_mat);
			double4x4 model_rot_mat = {0};

			{
				double a = b->rotation, c = cosf(a), s = sinf(a), C = 1 - c;
				double3 axis = double3norm(double3vs(b->rotation_axis.values));

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

			double scs_planet_radius = SCSd_WCSd(b->radius);

			double4x4 model_scale_mat;
			setdouble4x4scale(&model_scale_mat, double3v(scs_planet_radius));

			double4x4 model_trans_mat;
			setdouble4x4trans(&model_trans_mat, SCSd3_WCSp3(b->position));

			double4x4mul(&model_mat, &model_rot_mat);
			double4x4mul(&model_mat, &model_scale_mat);
			double4x4mul(&model_mat, &model_trans_mat);

			// double4x4trans(&model_mat, (SCSd3_WCSp3(p->as_body.position)));

			// PSHINE_DEBUG("⎡ %f %f %f %f ⎤", model_mat.vs[0][0], model_mat.vs[1][0], model_mat.vs[2][0], model_mat.vs[3][0]);
			// PSHINE_DEBUG("⎢ %f %f %f %f ⎥", model_mat.vs[0][1], model_mat.vs[1][1], model_mat.vs[2][1], model_mat.vs[3][1]);
			// PSHINE_DEBUG("⎢ %f %f %f %f ⎥", model_mat.vs[0][2], model_mat.vs[1][2], model_mat.vs[2][2], model_mat.vs[3][2]);
			// PSHINE_DEBUG("⎣ %f %f %f %f ⎦", model_mat.vs[0][3], model_mat.vs[1][3], model_mat.vs[2][3], model_mat.vs[3][3]);

			new_data.proj = proj_mat32;


			double4x4 model_view_mat = model_mat;
			double4x4mul(&model_view_mat, &view_mat);
			new_data.model_view = float4x4_double4x4(model_view_mat);
			new_data.model = float4x4_double4x4(model_mat);

			double3 sun_pos = double3v0();
			double3 body_pos = SCSd3_WCSp3(b->position);
			new_data.sun = float4xyz3w(float3_double3(double3norm(double3sub(sun_pos, body_pos))), 1.0f);

			struct vulkan_buffer *uniform_buffer = nullptr;
			struct vulkan_buffer *rings_uniform_buffer = nullptr;
			if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
				struct pshine_planet *p = (void *)b;
				uniform_buffer = &p->graphics_data->uniform_buffer;
				rings_uniform_buffer = &p->graphics_data->rings_uniform_buffer;
			} else if (b->type == PSHINE_CELESTIAL_BODY_STAR) {
				struct pshine_star *p = (void *)b;
				uniform_buffer = &p->graphics_data->uniform_buffer;
				rings_uniform_buffer = &p->graphics_data->rings_uniform_buffer;
			} else {
				PSHINE_ERROR("Unknown body type: %d", b->type);
			}
			char *data_raw;
			vmaMapMemory(r->allocator, uniform_buffer->allocation, (void**)&data_raw);
			data_raw += get_padded_uniform_buffer_size(r, sizeof(struct planet_mesh_uniform_data)) * current_frame;
			memcpy(data_raw, &new_data, sizeof(new_data));
			vmaUnmapMemory(r->allocator, uniform_buffer->allocation);
			// Rings
			if (b->rings.has_rings) {
				double4x4 model_scale_mat;
				double scs_outer_radius = SCSd_WCSd(b->rings.outer_radius);
				setdouble4x4scale(&model_scale_mat, double3v(scs_outer_radius));
				double4x4 model_mat;
				setdouble4x4iden(&model_mat);
				// double4x4mul(&model_mat, &model_rot_mat);
				double4x4mul(&model_mat, &model_scale_mat);
				double4x4mul(&model_mat, &model_trans_mat);
				double4x4mul(&model_mat, &view_mat);
				struct rings_uniform_data new_data_rings = {
					.proj = new_data.proj,
					.model_view = float4x4_double4x4(model_mat),
					.inner_radius = (float)b->rings.inner_radius,
					.outer_radius = (float)b->rings.outer_radius,
					.rel_planet_radius = (float)(scs_planet_radius / scs_outer_radius),
					.sun = new_data.sun,
					.shadow_smoothing = b->rings.shadow_smoothing,
				};
				vmaCopyMemoryToAllocation(r->allocator, &new_data_rings, rings_uniform_buffer->allocation,
					get_padded_uniform_buffer_size(r, sizeof(struct rings_uniform_data)) * current_frame,
					sizeof(new_data));
			}
		}
	}

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void*)b;

			// float3 wavelengths = float3rgb(
			// 	powf(400.0f / p->atmosphere.wavelengths[0], 4) * p->atmosphere.scattering_strength,
			// 	powf(400.0f / p->atmosphere.wavelengths[1], 4) * p->atmosphere.scattering_strength,
			// 	powf(400.0f / p->atmosphere.wavelengths[2], 4) * p->atmosphere.scattering_strength
			// );

			{
				double scs_atmo_h = SCSd_WCSd(p->atmosphere.height);
				double scs_body_r = SCSd_WCSd(p->as_body.radius);

				double scale_fact = scs_body_r + scs_atmo_h;

				double3 scs_body_pos = SCSd3_WCSp3(p->as_body.position);
				double3 scs_body_pos_scaled = double3div(scs_body_pos, scale_fact);

				double scs_body_r_scaled = scs_body_r / scale_fact;
				double3 scs_cam = camera_pos_scs;
				double3 scs_cam_scaled = double3div(scs_cam, scale_fact);

				double3 sun_pos = double3v0();
				struct atmo_uniform_data new_data = {
					.planet = float4xyz3w(
						float3v(0.0f),
						scs_body_r_scaled
					),
					.radius = 1.0f,
					.camera = float4xyz3w(float3_double3(double3sub(scs_cam_scaled, scs_body_pos_scaled)), 0.0f),
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
					.sun = float3_double3(double3norm(double3sub(sun_pos, scs_body_pos))),
					.scale_factor = scale_fact,
				};
				char *data_raw;
				vmaMapMemory(r->allocator, p->graphics_data->atmo_uniform_buffer.allocation, (void**)&data_raw);
				data_raw += get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame;
				memcpy(data_raw, &new_data, sizeof(new_data));
				vmaUnmapMemory(r->allocator, p->graphics_data->atmo_uniform_buffer.allocation);
			}

			// Upload material data.
			// TODO: check if this access is valid (why no current_frame)
			{
				struct planet_material_uniform_data new_data = {
					.color = float4rgba(0.8f, 0.3f, 0.1f, 1.0f),
					.view_dir = floatRapply(floatRvs(r->game->camera_orientation.values), float3xyz(0, 0, 1)),
					.smoothness = r->game->material_smoothness_,
				};
				struct planet_material_uniform_data *data;
				vmaMapMemory(r->allocator, p->graphics_data->material_uniform_buffer.allocation, (void**)&data);
				memcpy(data, &new_data, sizeof(new_data));
				vmaUnmapMemory(r->allocator, p->graphics_data->material_uniform_buffer.allocation);
			}
		}
	}

	CHECKVK(vkBeginCommandBuffer(f->command_buffer, &(VkCommandBufferBeginInfo){
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	}));

	// Image transitions for the bloom, as early as possible.
	// transients-color0 is shader-read-only-optimal, which is what we need for the compute shaders already.
	// so no need to transition that. but we do need to transition the transient bloom images to general for
	// the compute shader to write to them.
	{
		VkImageMemoryBarrier2 bloom_image_barriers[BLOOM_STAGE_COUNT];
		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
			bloom_image_barriers[i] = (VkImageMemoryBarrier2){
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.image = r->transients.bloom[i].image,
				.srcStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.subresourceRange = (VkImageSubresourceRange){
					.levelCount = 1,
					.layerCount = 1,
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.baseArrayLayer = 0,
				},
				.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
				.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
			};
		}
		vkCmdPipelineBarrier2(f->command_buffer, &(VkDependencyInfo){
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.dependencyFlags = 0,
			.imageMemoryBarrierCount = BLOOM_STAGE_COUNT,
			.pImageMemoryBarriers = bloom_image_barriers,
		});
	}

	vkCmdBeginRenderPass(f->command_buffer, &(VkRenderPassBeginInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = r->render_passes.hdr_pass,
		.framebuffer = r->hdr_framebuffers_own[image_index],
		.renderArea = (VkRect2D){
			.offset = { 0, 0 },
			.extent = r->swapchain_extent
		},
		.clearValueCount = 5,
		.pClearValues = (VkClearValue[]){
			// (VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
			(VkClearValue){ .depthStencil = (VkClearDepthStencilValue){ .depth = 0.0f, .stencil = 0 } },
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },
		},
	}, VK_SUBPASS_CONTENTS_INLINE);

	// vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.tri_pipeline);
	// vkCmdDraw(f->command_buffer, 3, 1, 0, 0);

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.planet_mesh_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.planet_mesh_layout,
				0,
				2,
				(VkDescriptorSet[]){ r->data.global_descriptor_set, p->graphics_data->material_descriptor_set },
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
				}
			);

			size_t lod = select_celestial_body_lod(r, b, camera_pos_scs);
			if (lod >= r->sphere_mesh_count) lod = r->sphere_mesh_count - 1;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.planet_mesh_layout,
				2,
				1,
				&p->graphics_data->descriptor_set,
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct planet_mesh_uniform_data)) * current_frame
				}
			);
			vkCmdBindVertexBuffers(f->command_buffer, 0, 1, &r->own_sphere_meshes[lod].vertex_buffer.buffer, &(VkDeviceSize){0});
			vkCmdBindIndexBuffer(f->command_buffer, r->own_sphere_meshes[lod].index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(f->command_buffer, r->own_sphere_meshes[lod].index_count, 1, 0, 0, 0);
		}
	}

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.planet_color_mesh_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });
	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.planet_mesh_layout,
		0,
		1,
		(VkDescriptorSet[]){ r->data.global_descriptor_set },
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
		}
	);

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_STAR) {
			struct pshine_star *p = (void *)b;
			pshine_color_rgb rgb = pshine_blackbody_temp_to_rgb(p->temperature);
			float4 color = float4xyz3w(
				float3add(
					float3xyz(rgb.rgb.r, rgb.rgb.g, rgb.rgb.b),
					float3v(0.0f)
				),
				p->temperature / 20.0f
			);
			vkCmdPushConstants(f->command_buffer, r->pipelines.planet_color_mesh_layout,
				VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float4), &color);
			size_t lod = select_celestial_body_lod(r, b, camera_pos_scs);
			if (lod >= r->sphere_mesh_count) lod = r->sphere_mesh_count - 1;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.planet_color_mesh_layout,
				1,
				1,
				&p->graphics_data->descriptor_set,
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct planet_mesh_uniform_data)) * current_frame
				}
			);
			vkCmdBindVertexBuffers(f->command_buffer, 0, 1, &r->own_sphere_meshes[lod].vertex_buffer.buffer, &(VkDeviceSize){0});
			vkCmdBindIndexBuffer(f->command_buffer, r->own_sphere_meshes[lod].index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(f->command_buffer, r->own_sphere_meshes[lod].index_count, 1, 0, 0, 0);
		}
	}

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.rings_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });
	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];
		if (!b->rings.has_rings) continue;
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.rings_layout,
				1,
				1,
				(VkDescriptorSet[]){ p->graphics_data->rings_descriptor_set },
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct rings_uniform_data)) * current_frame
				}
			);
			vkCmdDraw(f->command_buffer, 6, 1, 0, 0);
		}
	}

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.std_mesh_de_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });
	for (size_t i = 0; i < r->game->ships.dyna.count; ++i) {
		/// Technically, accessing this might be UB if this is not a valid marker,
		/// but no compilers care because most code relies on this being non-UB.
		if (r->game->ships.ptr[i]._alive_marker != (size_t)-1) {
			continue;
		}
		struct pshine_ship *ship = &r->game->ships.ptr[i];
		vkCmdBindDescriptorSets(
			f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.std_mesh_de_layout,
			2, 1, (VkDescriptorSet[]){ ship->graphics_data->descriptor_set },
			1, (uint32_t[]){
				get_padded_uniform_buffer_size(r, sizeof(struct std_mesh_uniform_data)) * current_frame
			}
		);
		for (size_t j = 0; j < ship->graphics_data->model.material_count; ++j) {
			vkCmdBindDescriptorSets(
				f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.std_mesh_de_layout,
				1, 1, (VkDescriptorSet[]){ ship->graphics_data->model.materials_own[j].descriptor_set },
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct std_material_uniform_data)) * current_frame
				}
			);
			for (size_t k = 0; k < ship->graphics_data->model.part_count; ++k) {
				if (ship->graphics_data->model.parts_own[k].material_index != j) continue;
				struct vulkan_mesh *mesh = &ship->graphics_data->model.parts_own[k].mesh;
				vkCmdBindIndexBuffer(f->command_buffer, mesh->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdBindVertexBuffers(f->command_buffer, 0, 1, &mesh->vertex_buffer.buffer, &(VkDeviceSize){0});
				vkCmdDrawIndexed(f->command_buffer, mesh->index_count, 1, 0, 0, 0);
			}
		}
	}

	// Skybox
	{
		float4x4 data[2] = {
			proj_mat32,
			float4x4_double4x4(view_mat),
		};

		// Remove the translation
		data[1].v4s[3].x = 0.0f;
		data[1].v4s[3].y = 0.0f;
		data[1].v4s[3].z = 0.0f;

		vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.skybox_pipeline);
		vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
			.x = 0.0f,
			.y = 0.0f,
			.width = (float)r->swapchain_extent.width,
			.height = (float)r->swapchain_extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		});
		vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });
		vkCmdPushConstants(f->command_buffer, r->pipelines.skybox_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
			sizeof(float4x4) * 2, &data);

		vkCmdBindDescriptorSets(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			r->pipelines.skybox_layout, 0, 1, &r->data.skybox_descriptor_set, 0, nullptr);

		vkCmdDraw(f->command_buffer, 14, 1, 0, 0);
	}



	//:---[ NEXT SUBPASS ]--------------://
	vkCmdNextSubpass(f->command_buffer, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.atmo_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });

	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.atmo_layout,
		0,
		1,
		(VkDescriptorSet[]){ r->data.global_descriptor_set },
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
		}
	);

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];
		if (b->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *p = (void *)b;
			if (!p->has_atmosphere) continue;
			vkCmdBindDescriptorSets(
				f->command_buffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				r->pipelines.atmo_layout,
				2,
				1,
				&p->graphics_data->atmo_descriptor_set,
				1, (uint32_t[]){
					get_padded_uniform_buffer_size(r, sizeof(struct atmo_uniform_data)) * current_frame
				}
			);
			vkCmdDraw(f->command_buffer, 3, 1, 0, 0);
			vkCmdPipelineBarrier2(f->command_buffer, &(VkDependencyInfo){
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
					(VkImageMemoryBarrier2){
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.image = r->transients.color_0.image,
						.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, // | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
						.dstAccessMask = VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT, // | VK_ACCESS_2_MEMORY_READ_BIT,
						.subresourceRange = (VkImageSubresourceRange){
							.levelCount = 1,
							.layerCount = 1,
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.baseArrayLayer = 0,
						},
						.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
						.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
					},
				},
			});
		}
	}

	//:---[ NEXT SUBPASS ]--------------://
	vkCmdNextSubpass(f->command_buffer, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.light_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });

	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.light_layout,
		0,
		1,
		(VkDescriptorSet[]){ r->data.light_descriptor_set },
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
		}
	);

	vkCmdDraw(f->command_buffer, 3, 1, 0, 0);


	//:---[ BLOOM ]--------------://
	vkCmdEndRenderPass(f->command_buffer);

	// Bloom
	{
		vkCmdPipelineBarrier2(f->command_buffer, &(VkDependencyInfo){
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
				(VkImageMemoryBarrier2){
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.image = r->transients.color_0.image,
					.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
					.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.subresourceRange = (VkImageSubresourceRange){
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseArrayLayer = 0,
						.baseMipLevel = 0,
						.layerCount = 1,
						.levelCount = 1,
					},
					.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
					.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
				},
			},
		});

		// now we do the downsampling compute.
		vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.first_downsample_bloom_pipeline);
		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
			struct vulkan_image *src_image = i == 0 ? &r->transients.color_0 : &r->transients.bloom[i - 1];
			struct vulkan_image *dst_image = &r->transients.bloom[i];

			vkCmdBindDescriptorSets(f->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.downsample_bloom_layout,
				0, 1, &r->data.downsample_bloom_descriptor_sets[i], 0, nullptr
			);

			vkCmdPushConstants(f->command_buffer, r->pipelines.downsample_bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT,
				0, sizeof(struct pshine_graphics_settings), &r->game->graphics_settings
			);

				vkCmdDispatch(f->command_buffer, 128, 128, 1);

			if (i == 1) vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.downsample_bloom_pipeline);

			// now we need to transition the image from general to read-only-optimal so that the next compute shader invocation
			// can read from it. this isn't *necessary*, and might even have worse performance (TODO benchmark!) but it's better
			// to do it anyway, so that stuff is correct.
			vkCmdPipelineBarrier2(f->command_buffer, &(VkDependencyInfo){
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.imageMemoryBarrierCount = 2,
				.pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
					(VkImageMemoryBarrier2){
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.image = dst_image->image,
						.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
						.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.subresourceRange = (VkImageSubresourceRange){
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseArrayLayer = 0,
							.baseMipLevel = 0,
							.layerCount = 1,
							.levelCount = 1,
						},
						.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
						.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
					},
					// we also can transition the previous image back to general, as it will be written to when upsampling.
					(VkImageMemoryBarrier2){
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.image = src_image->image,
						.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
						.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.subresourceRange = (VkImageSubresourceRange){
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseArrayLayer = 0,
							.baseMipLevel = 0,
							.layerCount = 1,
							.levelCount = 1,
						},
						.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
						.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
					},
				},
			});
		}

		// now all the images should be in the correct layout

		vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.upsample_bloom_pipeline);
		for (size_t i = 0; i < BLOOM_STAGE_COUNT; ++i) {
			bool is_last = i == BLOOM_STAGE_COUNT - 1;
			struct vulkan_image *dst_image = is_last ? &r->transients.color_0 : &r->transients.bloom[BLOOM_STAGE_COUNT - 2 - i];
			VkDescriptorSet ds = r->data.upsample_bloom_descriptor_sets[BLOOM_STAGE_COUNT - 1 - i];
			vkCmdBindDescriptorSets(
				f->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.upsample_bloom_layout, 0,
				1, (VkDescriptorSet[]){ ds }, 0, nullptr
			);
			vkCmdPushConstants(f->command_buffer, r->pipelines.upsample_bloom_layout, VK_SHADER_STAGE_COMPUTE_BIT,
				0, sizeof(struct pshine_graphics_settings), &r->game->graphics_settings);
			vkCmdDispatch(f->command_buffer, 128, 128, 1);
			// we do a similar transition to the downsampling stage stuff. the last image (transient-color0) is read by
			// the loadOp=LOAD in the next renderpass, so we make sure the barrier is correct that way.
			// we might also want to transition the images back to something else for the next frame.
			if (is_last) vkCmdPipelineBarrier2(f->command_buffer, &(VkDependencyInfo){
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.imageMemoryBarrierCount = 1,
				.pImageMemoryBarriers = (VkImageMemoryBarrier2[]){
					(VkImageMemoryBarrier2){
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
						.image = dst_image->image,
						.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT,
						.dstStageMask = is_last ? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
							: VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
						.dstAccessMask = is_last ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT : VK_ACCESS_2_MEMORY_READ_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
						.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						.subresourceRange = (VkImageSubresourceRange){
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseArrayLayer = 0,
							.baseMipLevel = 0,
							.layerCount = 1,
							.levelCount = 1,
						},
						.srcQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
						.dstQueueFamilyIndex = r->queue_families[QUEUE_GRAPHICS],
					},
				},
			});
		}
	}

	vkCmdBeginRenderPass(f->command_buffer, &(VkRenderPassBeginInfo){
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = r->render_passes.sdr_pass,
		.renderArea = (VkRect2D){ { 0, 0, }, r->swapchain_extent },
		.framebuffer = r->sdr_framebuffers_own[image_index],
		.clearValueCount = 1,
		.pClearValues = (VkClearValue[]){
			(VkClearValue){ .color = (VkClearColorValue){ .float32 = { 1.0f, 0.0f, 1.0f, 1.0f } } },
		}
	}, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(f->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.blit_pipeline);
	vkCmdSetViewport(f->command_buffer, 0, 1, &(VkViewport){
		.x = 0.0f,
		.y = 0.0f,
		.width = (float)r->swapchain_extent.width,
		.height = (float)r->swapchain_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	});
	vkCmdSetScissor(f->command_buffer, 0, 1, &(VkRect2D){ .offset = { 0, 0 }, .extent = r->swapchain_extent });
	vkCmdPushConstants(f->command_buffer, r->pipelines.blit_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(struct pshine_graphics_settings), &r->game->graphics_settings);
	vkCmdBindDescriptorSets(
		f->command_buffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		r->pipelines.blit_layout,
		0,
		1,
		(VkDescriptorSet[]){ r->data.blit_descriptor_set },
		1, (uint32_t[]){
			get_padded_uniform_buffer_size(r, sizeof(struct global_uniform_data)) * current_frame
		}
	);
	vkCmdDraw(f->command_buffer, 3, 1, 0, 0);

	cImGui_ImplVulkan_RenderDrawData(ImGui_GetDrawData(), f->command_buffer);

	vkCmdEndRenderPass(f->command_buffer);
	CHECKVK(vkEndCommandBuffer(f->command_buffer));
}

static void render(struct vulkan_renderer *r, uint32_t current_frame, size_t frame_number) {
	struct per_frame_data *f = &r->frames[current_frame];
	vkWaitForFences(r->device, 1, &f->sync.in_flight_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(r->device, 1, &f->sync.in_flight_fence);
	uint32_t image_index = 0;
	VkResult acquireImageRes = vkAcquireNextImageKHR(
		r->device, r->swapchain,
		UINT64_MAX,
		f->sync.image_avail_semaphore, VK_NULL_HANDLE,
		&image_index
	);
	if (acquireImageRes == VK_ERROR_OUT_OF_DATE_KHR || acquireImageRes == VK_SUBOPTIMAL_KHR) {
		reinit_swapchain(r);
		deinit_fbufs(r);
		init_fbufs(r);
		CHECKVK(vkAcquireNextImageKHR(
			r->device, r->swapchain,
			UINT64_MAX,
			f->sync.image_avail_semaphore, VK_NULL_HANDLE,
			&image_index
		));
	} else if (acquireImageRes != VK_SUCCESS) {
		CHECKVK(acquireImageRes);
	}
	CHECKVK(vkResetCommandBuffer(f->command_buffer, 0));
	do_frame(r, current_frame, image_index, frame_number);
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
		.pResults = nullptr
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
	if (r->game->ui_dont_render_windows) return;
	if (ImGui_Begin("Stats", nullptr, 0)) {
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
	if (r->game->ui_dont_render_windows) return;
	if (ImGui_Begin("Utils", nullptr, 0)) {
		ImGui_BeginDisabled(r->currently_recomputing_luts);
		if (ImGui_Button("Recompute Atmo LUTs")) {
			for (size_t i = 0; i < r->game->star_system_count; ++i) {
				struct pshine_star_system *system = &r->game->star_systems_own[i];
				for (size_t j = 0; j < system->body_count; ++j) {
					struct pshine_celestial_body *b = system->bodies_own[j];
					if (b->type == PSHINE_CELESTIAL_BODY_PLANET)
						compute_atmo_lut(r, (void*)b, false);
				}
			}
		}
		ImGui_EndDisabled();
		ImGui_BeginGroup();
		double lod_min = 0.0;
		double lod_max = 1'000'000'000.0;
		ImGui_SliderScalar("LOD3", ImGuiDataType_Double, &r->lod_ranges[3], &lod_min, &r->lod_ranges[2]);
		ImGui_SliderScalar("LOD2", ImGuiDataType_Double, &r->lod_ranges[2], &r->lod_ranges[3], &r->lod_ranges[1]);
		ImGui_SliderScalar("LOD1", ImGuiDataType_Double, &r->lod_ranges[1], &r->lod_ranges[2], &r->lod_ranges[0]);
		ImGui_SliderScalar("LOD0", ImGuiDataType_Double, &r->lod_ranges[0], &r->lod_ranges[1], &lod_max);
		ImGui_EndGroup();
	}
	ImGui_End();
}

static void set_gpu_mem_usage(struct vulkan_renderer *r, struct renderer_stats *stats) {
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(r->allocator, budgets);
	stats->gpu_memory.total_usage = (size_t)(budgets[0].usage / 1024) / 1024.0f;
	stats->gpu_memory.allocation_count = budgets[0].statistics.allocationCount;
}

struct gizmo_renderer {
	double4x4 screen_mat;
	double znear;
	double2 screen_size;
};

static double4 project_world_to_ndc(struct gizmo_renderer *g, double3 p) {
	return double4x4mulv(&g->screen_mat, double4xyz3w(p, 1.0));
}

struct clipped_line {
	bool ok;
	double2 p1, p2;
};

static double2 project_ndc_to_screen(
	struct gizmo_renderer *g,
	double4 p
) {
	return double2xy(
		(p.x / p.w * 0.5 + 0.5) * g->screen_size.x,
		(p.y / p.w * 0.5 + 0.5) * g->screen_size.y
	);
}

static struct clipped_line clip_ndc_line_to_screen(
	struct gizmo_renderer *g,
	double4 p1,
	double4 p2
) {
	// Thanks https://stackoverflow.com/a/20180585

	if (p1.w > g->znear && p2.w > g->znear) {
		return (struct clipped_line){
			.ok = true,
			.p1 = project_ndc_to_screen(g, p1),
			.p2 = project_ndc_to_screen(g, p2),
		};
	} else if (p1.w <= g->znear && p2.w <= g->znear) {
		return (struct clipped_line){ .ok = false };
	}
	if (p2.w <= g->znear) {
		double4 tmp = p2;
		p2 = p1;
		p1 = tmp;
	}
	double n = (g->znear - p2.w) / (p1.w - p2.w);
	double4 pc = double4xyzw(
		(n * p1.x) + ((1.0 - n) * p2.x),
		(n * p1.y) + ((1.0 - n) * p2.y),
		(n * p1.z) + ((1.0 - n) * p2.z),
		g->znear
	);
	return (struct clipped_line){
		.ok = true,
		.p1 = project_ndc_to_screen(g, pc),
		.p2 = project_ndc_to_screen(g, p2),
	};
}

static void show_gizmos(struct vulkan_renderer *r) {
	if (r->game->ui_dont_render_gizmos) return;
	ImVec2 display_size = ImGui_GetIO()->DisplaySize;
	double2 screen_size = double2xy(display_size.x, display_size.y);
	double aspect_ratio = screen_size.x / screen_size.y;
	double3 camera_pos_scs = SCSd3_WCSp3(r->game->camera_position);

	float3x3 rot_mat_32;
	setfloat3x3rotationR(&rot_mat_32, floatRinverse(floatRvs(r->game->camera_orientation.values)));
	double4x4 rot_mat;
	rot_mat.v4s[0] = double4xyz3w(double3_float3(rot_mat_32.v3s[0]), 0.0);
	rot_mat.v4s[1] = double4xyz3w(double3_float3(rot_mat_32.v3s[1]), 0.0);
	rot_mat.v4s[2] = double4xyz3w(double3_float3(rot_mat_32.v3s[2]), 0.0);
	rot_mat.v4s[3] = double4xyz3w(double3v0(), 1.0);

	double4x4 trans_mat;
	setdouble4x4trans(&trans_mat, double3neg(camera_pos_scs));

	double4x4 view_mat = trans_mat;
	double4x4mul(&view_mat, &rot_mat);

	double4x4 proj_mat = {};
	setdouble4x4iden(&proj_mat);
	double znear = 0.01;
	setdouble4x4persp(&proj_mat, r->game->actual_camera_fov, aspect_ratio, znear);

	double4x4 screen_mat = view_mat;
	double4x4mul(&screen_mat, &proj_mat);

	struct gizmo_renderer giz = {
		.screen_mat = screen_mat,
		.screen_size = screen_size,
		.znear = znear,
	};

	struct pshine_star_system *current_system = &r->game->star_systems_own[r->game->current_star_system];

	for (size_t i = 0; i < current_system->body_count; ++i) {
		struct pshine_celestial_body *b = current_system->bodies_own[i];

		double3 body_pos_scs = SCSd3_WCSp3(b->position);
		double3 parent_pos_scs = double3v0();
		bool should_render_name = true;
		if (b->parent_ref != nullptr) {
			parent_pos_scs = SCSd3_WCSp3(b->parent_ref->position);
			double d = sqrt(double3mag2(double3sub(parent_pos_scs, body_pos_scs)) /
				double3mag2(double3sub(camera_pos_scs, body_pos_scs)));
			if (d < 0.01) should_render_name = false;
		}

		// Planet name
		if (should_render_name) {
			double4 pos_ndc = project_world_to_ndc(&giz, body_pos_scs);
			if (pos_ndc.w > znear) {
				double2 pos_screen = project_ndc_to_screen(&giz, pos_ndc);
				// size_t lod = select_celestial_body_lod(r, b, camera_pos_scs);
				static char str[64];
				snprintf(str, sizeof str, "%s", b->name_own);
				ImDrawList_AddText(
					ImGui_GetBackgroundDrawList(),
					(ImVec2){ pos_screen.x, pos_screen.y },
					0xff000000 | b->gizmo_color,
					str
				);
			}
		}

		// Orbit
		if (b->orbit.cached_point_count > 0) {
			double4 prev_ndc = project_world_to_ndc(&giz, double3add(parent_pos_scs, double3vs(
				b->orbit.cached_points_own[b->orbit.cached_point_count - 1].values
			)));
			for (size_t i = 0; i < b->orbit.cached_point_count; ++i) {
				double4 curr_ndc = project_world_to_ndc(
					&giz,
					double3add(parent_pos_scs, double3vs(b->orbit.cached_points_own[i].values))
				);
				struct clipped_line line = clip_ndc_line_to_screen(&giz, prev_ndc, curr_ndc);
				if (line.ok) {
					ImDrawList_AddLineEx(
						ImGui_GetBackgroundDrawList(),
						(ImVec2){ .x = line.p1.x, .y = line.p1.y },
						(ImVec2){ .x = line.p2.x, .y = line.p2.y },
						0x10000000 | b->gizmo_color,
						1.0
					);
				}
				prev_ndc = curr_ndc;
			}
		}
	}
}

void pshine_main_loop(struct pshine_game *game, struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;

	cImGui_ImplVulkan_CreateFontsTexture();

	float last_time = glfwGetTime();

	/// This is mod `FRAMES_IN_FLIGHT`.
	uint32_t current_frame = 0;
	/// This is the total count.
	size_t frame_number = 0;

	/// This is for computing the average dt.
	size_t frames_since_dt_reset = 0;
	float delta_time_sum = 0.0f;

	int last_width = 0, last_height = 0;
	glfwGetFramebufferSize(r->window, &last_width, &last_height);
	while (!glfwWindowShouldClose(r->window)) {
		++frame_number; ++frames_since_dt_reset;
		float current_time = glfwGetTime();
		float delta_time = current_time - last_time;
		r->scroll_delta = double2v0();
		glfwPollEvents();

		int current_width = 0, current_height = 0;
		glfwGetFramebufferSize(r->window, &current_width, &current_height);

		if (current_width != last_width || current_height != last_height) {
			handle_swapchain_resize(r);
		}

		last_width = current_width;
		last_height = current_height;

		cImGui_ImplVulkan_NewFrame();
		cImGui_ImplGlfw_NewFrame();
		ImGui_NewFrame();
		pshine_update_game(game, delta_time);

		if (!game->ui_dont_render_windows)
			ImGui_ShowDemoWindow(nullptr);

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
		show_gizmos(r);
		show_utils_window(r);
		ImGui_Render();
		render(r, current_frame, frame_number);
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

const uint8_t *pshine_get_mouse_states(struct pshine_renderer *renderer) {
	struct vulkan_renderer *r = (void*)renderer;
	return r->mouse_states;
}

void pshine_get_mouse_position(struct pshine_renderer *renderer, double *x, double *y) {
	struct vulkan_renderer *r = (void*)renderer;
	glfwGetCursorPos(r->window, x, y);
}

void pshine_get_mouse_scroll_delta(struct pshine_renderer *renderer, double *x, double *y) {
	struct vulkan_renderer *r = (void*)renderer;
	if (x != nullptr) *x = r->scroll_delta.x;
	if (y != nullptr) *y = r->scroll_delta.y;
}
