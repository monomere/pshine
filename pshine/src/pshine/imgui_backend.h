#ifndef PSHINE_IMGUI_BACKEND_H_
#define PSHINE_IMGUI_BACKEND_H_

#include <volk.h>

#define GLFW_INCLUDE_NONE 1
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pshine_imgui_backend_init_info {
	VkInstance instance;
	VkPhysicalDevice physical_device;
	VkDevice device;
	uint32_t queue_family;
	VkQueue queue;
	VkPipelineCache pipeline_cache;
	VkDescriptorPool descriptor_pool;
	uint32_t min_image_count;
	uint32_t image_count;
	VkAllocationCallbacks *allocator;
	void (*check_vk_result_fn)(VkResult);
	VkSampleCountFlagBits msaa_samples;
	VkPipelineRenderingCreateInfo main_pipeline;
	GLFWwindow *window;
	void *user;
	PFN_vkVoidFunction (*loader_fn)(const char *name, void *user);
};

void pshine_imgui_backend_init(const struct pshine_imgui_backend_init_info *info);
void pshine_imgui_backend_shutdown();

VkDescriptorSet pshine_imgui_backend_add_texture(VkSampler sampler, VkImageView view, VkImageLayout layout);
void pshine_imgui_backend_new_frame();
void pshine_imgui_backend_render_draw_data(VkCommandBuffer command_buffer);

#ifdef __cplusplus
}
#endif

#endif
