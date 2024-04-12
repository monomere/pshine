#ifndef PSHINE_IMPL_VK_UTIL_H_
#define PSHINE_IMPL_VK_UTIL_H_
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

const char *pshine_vk_result_string(VkResult result) {
	switch (result) {
	case VK_SUCCESS: return "Success";
	case VK_NOT_READY: return "Not Ready";
	case VK_TIMEOUT: return "Timeout";
	case VK_EVENT_SET: return "Event Set";
	case VK_EVENT_RESET: return "Event Reset";
	case VK_INCOMPLETE: return "Incomplete";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "Error: Out Of Host Memory";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "Error: Out Of Device Memory";
	case VK_ERROR_INITIALIZATION_FAILED: return "Error: Initialization Failed";
	case VK_ERROR_DEVICE_LOST: return "Error: Device Lost";
	case VK_ERROR_MEMORY_MAP_FAILED: return "Error: Memory Map Failed";
	case VK_ERROR_LAYER_NOT_PRESENT: return "Error: Layer Not Present";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "Error: Extension Not Present";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "Error: Feature Not Present";
	case VK_ERROR_INCOMPATIBLE_DRIVER: return "Error: Incompatible Driver";
	case VK_ERROR_TOO_MANY_OBJECTS: return "Error: Too Many Objects";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "Error: Format Not Supported";
	case VK_ERROR_FRAGMENTED_POOL: return "Error: Fragmented Pool";
	case VK_ERROR_UNKNOWN: return "Error: Unknown";
	case VK_ERROR_OUT_OF_POOL_MEMORY: return "Error: Out Of Pool Memory";
	case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "Error: Invalid External Handle";
	case VK_ERROR_FRAGMENTATION: return "Error: Fragmentation";
	case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "Error: Invalid Opaque Capture Address";
	case VK_PIPELINE_COMPILE_REQUIRED: return "Pipeline Compile Required";
	case VK_ERROR_SURFACE_LOST_KHR: return "Error: Surface Lost";
	case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "Error: Native Window In Use";
	case VK_SUBOPTIMAL_KHR: return "Suboptimal";
	case VK_ERROR_OUT_OF_DATE_KHR: return "Error: Out Of Date";
	case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "Error: Incompatible Display";
	case VK_ERROR_VALIDATION_FAILED_EXT: return "Error: Validation Failed";
	default: return "Unknown";
	}
}

#endif // PSHINE_IMPL_VK_UTIL_H_
