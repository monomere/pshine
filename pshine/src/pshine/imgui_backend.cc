#include "imgui_backend.h"

#define VULKAN_NO_PROTOTYPES 1
#define IMGUI_IMPL_VULKAN_USE_VOLK 1

#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>

void pshine_imgui_backend_init(const pshine_imgui_backend_init_info *info) {
	ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, info->loader_fn, info->user);
	ImGui_ImplGlfw_InitForVulkan(info->window, true);

	ImGui_ImplVulkan_InitInfo vulkan_init_info {
		.Instance = info->instance,
		.PhysicalDevice = info->physical_device,
		.Device = info->device,
		.QueueFamily = info->queue_family,
		.Queue = info->queue,
		.DescriptorPool = info->descriptor_pool,
		.MinImageCount = info->min_image_count,
		.ImageCount = info->image_count,
		.PipelineCache = info->pipeline_cache,
		.PipelineInfoMain = {
			.MSAASamples = info->msaa_samples,
			.PipelineRenderingCreateInfo = info->main_pipeline,
		},
		.UseDynamicRendering = true,
		.Allocator = info->allocator,
		.CheckVkResultFn = info->check_vk_result_fn,
	};
	ImGui_ImplVulkan_Init(&vulkan_init_info);
}

void pshine_imgui_backend_shutdown() {
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
}

VkDescriptorSet pshine_imgui_backend_add_texture(VkSampler sampler, VkImageView view, VkImageLayout layout) {
	return ImGui_ImplVulkan_AddTexture(sampler, view, layout);
}

void pshine_imgui_backend_new_frame() {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::GetIO().DisplayFramebufferScale = ImVec2 { 1.0f, 1.0f };
}

void pshine_imgui_backend_render_draw_data(VkCommandBuffer command_buffer) {
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

