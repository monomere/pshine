#ifdef __clang__
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wswitch-default"
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#endif

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vulkan/vulkan.h>
#define VMA_DYNAMIC_FUNCTIONS
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
