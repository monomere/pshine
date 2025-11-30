/// Copyright © 2025 monomere
///
/// This Source Code Form is subject to the terms of the Mozilla Public
/// License, v. 2.0. If a copy of the MPL was not distributed with this
/// file, You can obtain one at https://mozilla.org/MPL/2.0/.
/// 
/// Vulkan 1.4 dynamic rendering render graph implementation with support
/// for dynamic_rendering_local_read.
///
/// Follows the "stb header" format: the first part of the file is the
/// header, the second part of the files is the implementation.
/// So there should be a single C file with the following contents:
///   #define RG_IMPLEMENTATION
///   #define RG_DEBUG 1                                     (optional)
///   #define RG_DEBUG_PIPELINE_BARRIER_CALLS 1              (optional)
///   #define RG_DEBUG_PRINTF my_printf                      (optional)
///   #define RG_CHECK my_assert                             (optional)
///   #include "vkrgraph.h"
///
/// To enable debugging, define RG_DEBUG to be non-zero in the C file,
/// and use `rg_enable_debugging(true/false)`. When debugging is enabled,
/// you can also enable logging of the actual inserted pipeline barriers
/// with RG_DEBUG_PIPELINE_BARRIER_CALLS set to 1. To customize the print
/// function, set RG_DEBUG_PRINTF (rg_enable_debugging is ignored).
///
/// There are also a couple of assertions/checks using the RG_CHECK macro.
/// Used with (bool expression, const char *message), should fail on false.
/// 
/// Originally made for PShine (https://github.com/monomere/pshine), but
/// I realized that this could work as a standalone library too.
///
/// The general sequence of function calls to use this library is:
///   rg_build_graph(&(struct rg_graph_spec){...}, &graph)
///   {every frame}
///     rg_graph_begin_frame(&graph, ...)
///     {each specified render pass}
///       rg_graph_begin_pass(&graph)
///         {rendering commands}
///       rg_graph_end_pass(&graph)
///     rg_graph_end_frame(&graph)
///   rg_free_graph(&graph)
///
/// For an example graph specification setup, see the example function at
/// the end of this file. (If it doesn't work, make an issue in the codeberg
/// repository, and check out pshine's source code (pshine/src/pshine/vk.c))
///
/// The documentation is not very good right now, so you might need to
/// take a look at the implementation and/or examples. Enabling the debugging
/// can also help a lot.
///
/// Hopefully someone finds this useful.
///
/// '-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'-,-'

#ifndef VKRGRAPH_H_
#define VKRGRAPH_H_
#include <limits.h>
#include <vulkan/vulkan.h>

struct rg_graph;

/// Currently unused, TODO.
struct rg_graph_commands {
	/// vkCmdBeginRendering wrapper.
	void (*begin_rendering)(struct rg_graph *graph, const VkRenderingInfo *info);
	/// vkCmdEndRendering wrapper.
	void (*end_rendering)(struct rg_graph *graph);
	/// vkCmdPipelineBarrier2 wrapper.
	void (*barrier)(struct rg_graph *graph, const VkDependencyInfo *info);
};

struct rg_graph_image_spec {
	const char *name;
	VkImage image;
	VkImageView image_view;
	VkFormat format;
	VkImageAspectFlags aspect;
};

struct rg_graph_spec {
	const struct rg_pass_spec *passes;
	const struct rg_graph_image_spec *images;
	uint32_t pass_count;
	uint32_t image_count;
};

/// "Newtype" for a render graph image id. `~(rg_image_id)0` is a special value.
/// Ideally this would be a struct, but it is hard to use in that case.
typedef uint32_t rg_image_id;

enum : rg_image_id {
	/// Current swapchain image.
	RG_IMAGE_SWAPCHAIN = ~(rg_image_id)0,
};

typedef uint32_t rg_image_ref_use_flags;
enum : rg_image_ref_use_flags {
	/// Unspecified.
	RG_IMAGE_USE_OTHER                = 0b00000000,
	/// Used as a sampled image.
	RG_IMAGE_USE_SAMPLED_BIT          = 0b00000001,
	/// Used as a color attachment.
	RG_IMAGE_USE_COLOR_ATTACHMENT_BIT = 0b00000010,
	/// Used as a depth attachment.
	RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT = 0b00000100,
	/// Used as an input attachment.
	RG_IMAGE_USE_INPUT_ATTACHMENT_BIT = 0b00001000,
	/// Not actually used (for merging passes).
	RG_IMAGE_USE_FAKE_BIT             = 0b10000000,
	/// The `loadOp` is `CLEAR`.
	RG_IMAGE_USE_CLEARED_BIT          = 0b01000000,
	/// The `loadOp` is `DONT_CARE`.
	RG_IMAGE_USE_NO_READ_BIT          = 0b00100000,
};

/// Reference to an image in the render graph.
struct rg_image_ref_spec {
	/// The image that is referenced. Index in `rg_graph_spec::images`;
	rg_image_id image_id;
	/// (optional) How the image is used.
	rg_image_ref_use_flags usage;
	/// (optional) The layout the image should be in for this pass.
	/// If the layout is `UNDEFINED`:
	/// - if used as color attachment, the layout will be `COLOR_ATTACHMENT_OPTIMAL`,
	/// - if used as depth attachment, the layout will be `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`,
	/// - if used as input attachment, the layout will be `RENDERING_LOCAL_READ`,
	/// - if used as sampled image, the layout will be `SHADER_READ_ONLY_OPTIMAL`.
	VkImageLayout layout;
	/// (optional) The layout this image is in after the pass.
	/// If `UNDEFINED`, set equal to `layout`.
	VkImageLayout final_layout;
	/// (optional) The stage the image is used in.
	/// If `0`, determined from `layout`.
	VkPipelineStageFlags2 stage;
	/// (optional) The access flags for the image use.
	/// If `0`:
	/// - if used as color attachment, the access will have `COLOR_ATTACHMENT_WRITE`,
	/// - if used as depth attachment, the access will have `DEPTH_STENCIL_ATTACHMENT_WRITE`,
	/// - if used as input attachment, the access will have `INPUT_ATTACHMENT_READ`,
	/// - if used as sampled image, the layout is `SHADER_SAMPLED_READ`.
	VkAccessFlags2 access;
	/// Used when `usage` has the `CLEARED` bit.
	VkClearValue clear_value;
};

struct rg_pass_spec {
	const char *name;
	const struct rg_image_ref_spec *image_refs;
	uint32_t image_ref_count;
	VkRect2D render_area;
	bool compute;
};

struct rg_graph_image_use {
	uint32_t pass_index;
	uint32_t ref_index;
};

struct rg_image_ref {
	uint32_t image_index;
	VkImageLayout initial_layout;
	VkImageLayout final_layout;
	VkPipelineStageFlags2 stage_flags;
	VkAccessFlags2 access_flags;
	VkAttachmentLoadOp load_op;
	VkAttachmentStoreOp store_op;
	VkClearValue clear;
};

struct rg_graph_image {
	const char *name;
	VkImage image;
	VkImageView image_view;
	VkFormat format;
	// indexed by pass index (+ 1, 0 is for the frame start),
	// stores the next use after that pass.
	struct rg_graph_image_use *pass_use_map_own;
	struct rg_image_ref last_use_ref;
	VkImageAspectFlags aspect;
};

struct rg_pass {
	const char *name;
	struct rg_image_ref *image_refs_own;
	uint32_t image_ref_count;
	uint32_t color_attachment_count;
	uint32_t color_attachments[8];
	uint32_t input_attachment_count;
	uint32_t input_attachments[8];
	uint32_t depth_attachment;
	VkRect2D render_area;
	bool has_depth_attachment;
	bool compute;
	bool merged_with_next;
	bool merged_with_prev;
};

struct rg_graph_impl {
	uint32_t pass_index;
	VkRect2D render_area;
	struct rg_graph_image swapchain_image;
	VkCommandBuffer command_buffer;
	uint32_t queue_family_index;
};

struct rg_graph {
	struct rg_graph_image *images_own;
	struct rg_graph_impl current;
	struct rg_pass *passes_own;
	uint32_t image_count;
	uint32_t pass_count;
};

/// Can be called at any point.
void rg_enable_debugging(bool enabled);
/// Initialize a render graph based on the specification.
void rg_build_graph(const struct rg_graph_spec *spec, struct rg_graph *graph);
/// Free the dynamically allocated memory used by the graph.
void rg_free_graph(struct rg_graph *graph);
/// Begin frame and set per-frame data.
void rg_graph_begin_frame(
	struct rg_graph *graph,
	VkRect2D render_area,
	uint32_t queue_family_index,
	VkImage swapchain_image,
	VkImageView swapchain_image_view,
	VkCommandBuffer command_buffer
);
/// Does not submit commands or do anything at all really.
void rg_graph_end_frame(struct rg_graph *graph);
/// Begin current pass. Inserts the necessary pipeline barriers.
void rg_graph_begin_pass(struct rg_graph *graph);
/// End current pass. Inserts the necessary pipeline barriers.
void rg_graph_end_pass(struct rg_graph *graph);

/// Not currently implemented.
void rg_graph_pass_last_use(
	struct rg_graph *graph,
	rg_image_id image
);


#ifdef RG_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef RG_DEBUG
# define RG_DEBUG 0
#else
# if (0-RG_DEBUG-1)==1 && (RG_DEBUG-0)!=-2 // check for empty
#  undef RG_DEBUG
#  define RG_DEBUG 1
# endif
#endif

#ifndef RG_DEBUG_PIPELINE_BARRIER_CALLS
# define RG_DEBUG_PIPELINE_BARRIER_CALLS 0
#else
# if (0-RG_DEBUG_PIPELINE_BARRIER_CALLS-1)==1 && \
	(RG_DEBUG_PIPELINE_BARRIER_CALLS-0)!=-2
#  undef RG_DEBUG_PIPELINE_BARRIER_CALLS
#  define RG_DEBUG_PIPELINE_BARRIER_CALLS 1
# endif
#endif

#ifndef RG_DEBUG_PRINTF
# if RG_DEBUG
#  define RG_DEBUG_PRINTF(msg, ...) \
	rg_i_current_debug_printf(stderr, (msg) __VA_OPT__(,) __VA_ARGS__);
# else
#  define RG_DEBUG_PRINTF(...)
# endif
#endif


#if RG_DEBUG

static int rg_i_debug_noop(FILE *fout, const char *fmt, ...) {
	(void)fout; (void)fmt;
	return 0;
}

static int (*rg_i_current_debug_printf)(FILE *fout, const char *fmt, ...)
= fprintf;

void rg_enable_debugging(bool enabled) {
	if (enabled) rg_i_current_debug_printf = fprintf;
	else rg_i_current_debug_printf = rg_i_debug_noop;
}

#else
// TODO: move to header part if empty
void rg_enable_debugging(bool enabled) { (void)enabled; }
#endif

#ifndef RG_CHECK
# ifdef RG_BUILTIN_CHECK_
#  undef RG_BUILTIN_CHECK_
# endif
# define RG_BUILTIN_CHECK_ 1
# define RG_CHECK(EXPR, MSG) \
	rg_i_default_check_impl_((EXPR), #EXPR, (MSG), __LINE__, __FILE__, __func__)
#else
# ifndef RG_BUILTIN_CHECK_
#  define RG_BUILTIN_CHECK_ 0
# endif
#endif

#if RG_BUILTIN_CHECK_
void rg_i_default_check_impl_(
	bool expr,
	const char *str_expr,
	const char *msg,
	int line,
	const char *file,
	const char *func
) {
	if (!expr) {
		fprintf(stderr, "RG_CHECK failed at %s:%d (%s): %s\n  '%s'\n",
			file, line, func, msg, str_expr);
		abort();
	}
}
#endif

[[maybe_unused]]
static const char *rg_i_vk_stage_bit_string(VkPipelineStageFlagBits2 bit) {
	switch (bit) {
		case VK_PIPELINE_STAGE_2_NONE: return "NONE";
		case VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT: return "TOP_OF_PIPE";
		case VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT: return "DRAW_INDIRECT";
		case VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT: return "VERTEX_INPUT";
		case VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT: return "VERTEX_SHADER";
		case VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT: return "TESSELLATION_CONTROL_SHADER";
		case VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT:
			return "TESSELLATION_EVALUATION_SHADER";
		case VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT: return "GEOMETRY_SHADER";
		case VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT: return "FRAGMENT_SHADER";
		case VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT: return "EARLY_FRAGMENT_TESTS";
		case VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT: return "LATE_FRAGMENT_TESTS";
		case VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT: return "COLOR_ATTACHMENT_OUTPUT";
		case VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT: return "COMPUTE_SHADER";
		case VK_PIPELINE_STAGE_2_TRANSFER_BIT: return "TRANSFER";
		case VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT: return "BOTTOM_OF_PIPE";
		case VK_PIPELINE_STAGE_2_HOST_BIT: return "HOST";
		case VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT: return "ALL_GRAPHICS";
		case VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT: return "ALL_COMMANDS";
		case VK_PIPELINE_STAGE_2_COPY_BIT: return "COPY";
		case VK_PIPELINE_STAGE_2_RESOLVE_BIT: return "RESOLVE";
		case VK_PIPELINE_STAGE_2_BLIT_BIT: return "BLIT";
		case VK_PIPELINE_STAGE_2_CLEAR_BIT: return "CLEAR";
		case VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT: return "INDEX_INPUT";
		case VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT: return "VERTEX_ATTRIBUTE_INPUT";
		case VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT: return "PRE_RASTERIZATION_SHADERS";
		default: return "?";
	}
}

[[maybe_unused]]
static const char *rg_i_vk_access_bit_string(VkAccessFlagBits2 bit) {
	switch (bit) {
		case VK_ACCESS_2_NONE: return "NONE";
		case VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT: return "INDIRECT_COMMAND_READ";
		case VK_ACCESS_2_INDEX_READ_BIT: return "INDEX_READ";
		case VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT: return "VERTEX_ATTRIBUTE_READ";
		case VK_ACCESS_2_UNIFORM_READ_BIT: return "UNIFORM_READ";
		case VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT: return "INPUT_ATTACHMENT_READ";
		case VK_ACCESS_2_SHADER_READ_BIT: return "SHADER_READ";
		case VK_ACCESS_2_SHADER_WRITE_BIT: return "SHADER_WRITE";
		case VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT: return "COLOR_ATTACHMENT_READ";
		case VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT: return "COLOR_ATTACHMENT_WRITE";
		case VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT: return "DEPTH_STENCIL_ATTACHMENT_READ";
		case VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT: return "DEPTH_STENCIL_ATTACHMENT_WRITE";
		case VK_ACCESS_2_TRANSFER_READ_BIT: return "TRANSFER_READ";
		case VK_ACCESS_2_TRANSFER_WRITE_BIT: return "TRANSFER_WRITE";
		case VK_ACCESS_2_HOST_READ_BIT: return "HOST_READ";
		case VK_ACCESS_2_HOST_WRITE_BIT: return "HOST_WRITE";
		case VK_ACCESS_2_MEMORY_READ_BIT: return "MEMORY_READ";
		case VK_ACCESS_2_MEMORY_WRITE_BIT: return "MEMORY_WRITE";
		case VK_ACCESS_2_SHADER_SAMPLED_READ_BIT: return "SHADER_SAMPLED_READ";
		case VK_ACCESS_2_SHADER_STORAGE_READ_BIT: return "SHADER_STORAGE_READ";
		case VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT: return "SHADER_STORAGE_WRITE";
		default: return "?";
	}
}


[[maybe_unused]]
static const char *rg_i_vk_layout_string(VkImageLayout layout) {
	switch (layout) {
		case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
    case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
    case VK_IMAGE_LAYOUT_PREINITIALIZED: return "PREINITIALIZED";
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL:
			return "DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL:
			return "DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL: return "DEPTH_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL: return "DEPTH_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL: return "STENCIL_ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL: return "STENCIL_READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL: return "READ_ONLY_OPTIMAL";
    case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL: return "ATTACHMENT_OPTIMAL";
    case VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ: return "RENDERING_LOCAL_READ";
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR: return "VIDEO_DECODE_DST_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_DECODE_SRC_KHR: return "VIDEO_DECODE_SRC_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR: return "VIDEO_DECODE_DPB_KHR";
    case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR: return "SHARED_PRESENT_KHR";
    case VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT:
			return "FRAGMENT_DENSITY_MAP_OPTIMAL_EXT";
    case VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR:
			return "FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_ENCODE_DST_KHR: return "VIDEO_ENCODE_DST_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR: return "VIDEO_ENCODE_SRC_KHR";
    case VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR: return "VIDEO_ENCODE_DPB_KHR";
    case VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT:
			return "ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT";
    case VK_IMAGE_LAYOUT_TENSOR_ALIASING_ARM: return "TENSOR_ALIASING_ARM";
    case VK_IMAGE_LAYOUT_VIDEO_ENCODE_QUANTIZATION_MAP_KHR:
			return "VIDEO_ENCODE_QUANTIZATION_MAP_KHR";
    case VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT: return "ZERO_INITIALIZED_EXT";
		default: return "?";
	}
}


static void rg_impl_build_merge_passes(struct rg_graph *graph) {
	for (uint32_t i = 1; i < graph->pass_count; ++i) {
		struct rg_pass *src_pass = &graph->passes_own[i - 1];
		struct rg_pass *dst_pass = &graph->passes_own[i];
		struct rg_pass *small_pass = src_pass, *big_pass = dst_pass;
		
		if (small_pass->image_ref_count > big_pass->image_ref_count) {
			small_pass = dst_pass;
			big_pass = src_pass;
		}

		for (uint32_t j = 0; j < small_pass->image_ref_count; ++j) {
			struct rg_image_ref *small_ref = &small_pass->image_refs_own[j];
			struct rg_image_ref *big_ref = &big_pass->image_refs_own[j];
			
			if (small_ref->image_index != big_ref->image_index) {
				RG_DEBUG_PRINTF("Did not merge pass %s and %s because they have an incompatible "
					"image reference (index %u).\n", small_pass->name, big_pass->name, j);
				RG_DEBUG_PRINTF("Pass %s has %u references, pass %s has %u.\n",
					small_pass->name, small_pass->image_ref_count, big_pass->name, big_pass->image_ref_count);
				goto merge_failed; // continue outer loop
			}

			[[maybe_unused]]
			struct rg_graph_image *image = small_ref->image_index != UINT32_MAX
				? &graph->images_own[small_ref->image_index]
				: &graph->current.swapchain_image
				;

			if (
				small_ref->access_flags & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT &&
				(
					(big_ref->access_flags & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) ||
					(big_ref->access_flags & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
				)
			) {
				RG_DEBUG_PRINTF("Did not merge pass %s and %s because %s has a sampled read from %s, "
					"which %s uses as an attachment.\n",
					small_pass->name, big_pass->name, small_pass->name, image->name, big_pass->name);
				goto merge_failed;
			}
			if (
				big_ref->access_flags & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT &&
				(
					(small_ref->access_flags & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) ||
					(small_ref->access_flags & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
				)
			) {
				RG_DEBUG_PRINTF("Did not merge pass %s and %s because %s has a sampled read from %s, "
					"which %s uses as an attachment.\n",
					small_pass->name, big_pass->name, big_pass->name, image->name, small_pass->name);
				goto merge_failed;
			}
		}

		// merge the color attachments form src_pass and dst_pass
		// the indices are the same because the ref arrays are the same.
		uint32_t color_attachment_count = 0;
		uint32_t color_attachments[8] = {};
		{
			uint32_t src_j = 0, dst_j = 0;
			while (
				src_j < src_pass->color_attachment_count &&
				dst_j < dst_pass->color_attachment_count
			) {
				RG_CHECK(color_attachment_count < 8, "what");
				uint32_t src_v = src_pass->color_attachments[src_j];
				uint32_t dst_v = dst_pass->color_attachments[dst_j];
				if (src_v < dst_v) {
					color_attachments[color_attachment_count++] = src_v;
					src_j += 1;
				} else if (src_v > dst_v) {
					color_attachments[color_attachment_count++] = dst_v;
					dst_j += 1;
				} else { // dst_v == src_v
					color_attachments[color_attachment_count++] = dst_v;
					src_j += 1;
					dst_j += 1;
				}
			}
			while (src_j < src_pass->color_attachment_count)
				color_attachments[color_attachment_count++]
					= src_pass->color_attachments[src_j++];

			while (dst_j < dst_pass->color_attachment_count)
				color_attachments[color_attachment_count++]
					= dst_pass->color_attachments[dst_j++];
		}

		// merge the input attachments form src_pass and dst_pass
		// the indices are the same because the ref arrays are the same.
		uint32_t input_attachment_count = 0;
		uint32_t input_attachments[8] = {};
		{
			uint32_t src_j = 0, dst_j = 0;
			while (
				src_j < src_pass->input_attachment_count &&
				dst_j < dst_pass->input_attachment_count
			) {
				uint32_t src_v = src_pass->input_attachments[src_j];
				uint32_t dst_v = dst_pass->input_attachments[dst_j];
				if (src_v < dst_v) {
					input_attachments[input_attachment_count++] = src_v;
					src_j += 1;
				} else if (src_v > dst_v) {
					input_attachments[input_attachment_count++] = dst_v;
					dst_j += 1;
				} else { // dst_v == src_v
					input_attachments[input_attachment_count++] = dst_v;
					src_j += 1;
					dst_j += 1;
				}
			}
			while (src_j < src_pass->input_attachment_count)
				input_attachments[input_attachment_count++]
					= src_pass->color_attachments[src_j++];
			while (dst_j < dst_pass->input_attachment_count)
				input_attachments[input_attachment_count++]
					= dst_pass->input_attachments[dst_j++];
		}

		src_pass->color_attachment_count = color_attachment_count;
		dst_pass->color_attachment_count = color_attachment_count;
		memcpy(src_pass->color_attachments, color_attachments, sizeof(color_attachments));
		memcpy(dst_pass->color_attachments, color_attachments, sizeof(color_attachments));
		src_pass->input_attachment_count = input_attachment_count;
		dst_pass->input_attachment_count = input_attachment_count;
		memcpy(src_pass->input_attachments, input_attachments, sizeof(input_attachments));
		memcpy(dst_pass->input_attachments, input_attachments, sizeof(input_attachments));
		
		if (src_pass->has_depth_attachment) {
			dst_pass->has_depth_attachment = true;
			dst_pass->depth_attachment = src_pass->depth_attachment;
		} else if (dst_pass->has_depth_attachment) {
			src_pass->has_depth_attachment = true;
			src_pass->depth_attachment = dst_pass->depth_attachment;
		}
		small_pass->image_refs_own = realloc(
			small_pass->image_refs_own,
			sizeof(*small_pass->image_refs_own) *
				big_pass->image_ref_count
		);
		for (uint32_t j = small_pass->image_ref_count; j < big_pass->image_ref_count; ++j) {
			small_pass->image_refs_own[j] = big_pass->image_refs_own[j];
		}
		small_pass->image_ref_count = big_pass->image_ref_count;

		src_pass->merged_with_next = true;
		dst_pass->merged_with_prev = true;

		// Pass on the local_read layout to the all of the merged passes.
		for (uint32_t k = i; k >= 0 && graph->passes_own[k].merged_with_prev; --k) {
			struct rg_pass *a_pass = &graph->passes_own[k];
			struct rg_pass *b_pass = &graph->passes_own[k - 1];
			for (uint32_t j = 0; j < small_pass->image_ref_count; ++j) {
				bool a_is_input = small_pass->image_refs_own[j].access_flags
					& VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
				bool b_is_input = big_pass->image_refs_own[j].access_flags
					& VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
				if (a_is_input || b_is_input) {
					a_pass->image_refs_own[j].initial_layout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
					a_pass->image_refs_own[j].final_layout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
					b_pass->image_refs_own[j].initial_layout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
					b_pass->image_refs_own[j].final_layout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ;
				}
			}
		}
	
	merge_failed:
	}
}

void rg_build_graph(const struct rg_graph_spec *spec, struct rg_graph *graph) {
	// graph->commands = spec->commands;
	graph->image_count = spec->image_count;
	graph->images_own = calloc(graph->image_count, sizeof(*graph->images_own));
	for (uint32_t i = 0; i < graph->image_count; ++i) {
		graph->images_own[i].image_view = spec->images[i].image_view;
		graph->images_own[i].image = spec->images[i].image;
		graph->images_own[i].name = spec->images[i].name;
		graph->images_own[i].format = spec->images[i].format;
		graph->images_own[i].aspect = spec->images[i].aspect;
		graph->images_own[i].pass_use_map_own = nullptr; // initialied later in this function
	}

	graph->pass_count = spec->pass_count;
	graph->passes_own = calloc(graph->pass_count, sizeof(*graph->passes_own));
	for (uint32_t i = 0; i < graph->pass_count; ++i) {
		const struct rg_pass_spec *pass_spec = &spec->passes[i];
		struct rg_pass *pass = &graph->passes_own[i];
		pass->name = pass_spec->name;
		pass->compute = pass_spec->compute;
		pass->render_area = pass_spec->render_area;
		pass->image_ref_count = pass_spec->image_ref_count;
		pass->image_refs_own = calloc(pass->image_ref_count, sizeof(*pass->image_refs_own));

		uint32_t color_attachment_count = 0;
		uint32_t input_attachment_count = 0;
		bool has_depth_attachment = false;
		uint32_t depth_attachment_index = 0;
		for (uint32_t j = 0; j < pass_spec->image_ref_count; ++j) {
			const struct rg_image_ref_spec *ref_spec = &pass_spec->image_refs[j];
			// RG_CHECK(ref_spec->image_id != RG_IMAGE_NONE,
			// 	"rg_pass_spec::image_refs[].image_id must not be RG_IMAGE_NONE");

			RG_CHECK(!(
				(ref_spec->usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT) &&
				(ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT)
			), "an image cannot be used as both a color and a depth attachment");

			RG_CHECK(!(
				(ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT) &&
				has_depth_attachment
			), "a pass cannot have more than one depth attachment");
			
			color_attachment_count += !!(ref_spec->usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT);
			input_attachment_count += !!(ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT);
			if (ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT) {
				depth_attachment_index = j;
				has_depth_attachment = true;
			}

			VkAccessFlags2 access_flags = ref_spec->access != 0
				? ref_spec->access
				: (ref_spec->usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT
						? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT
						? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT
						? VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_SAMPLED_BIT
						? VK_ACCESS_2_SHADER_SAMPLED_READ_BIT : 0)
				;
			VkPipelineStageFlags2 stage_flags = ref_spec->stage != 0
				? ref_spec->stage
				: (ref_spec->usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT
						? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT
						? VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
							| VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT
						? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : 0)
				| (ref_spec->usage & RG_IMAGE_USE_SAMPLED_BIT
						? VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT : 0)
				;
			
			VkImageLayout layout = ref_spec->layout != VK_IMAGE_LAYOUT_UNDEFINED
				? ref_spec->layout
				: ref_spec->usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT
				? (ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT
					? VK_IMAGE_LAYOUT_GENERAL
					: VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
				: ref_spec->usage & RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT
				? (ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT
					? VK_IMAGE_LAYOUT_GENERAL
					: VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
				: ref_spec->usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT
				? VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ
				: ref_spec->usage & RG_IMAGE_USE_SAMPLED_BIT
				? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				: VK_IMAGE_LAYOUT_UNDEFINED
				;

			pass->image_refs_own[j] = (struct rg_image_ref){
				.image_index = ref_spec->image_id == RG_IMAGE_SWAPCHAIN
					? UINT32_MAX : ref_spec->image_id,
				.initial_layout = layout,
				.final_layout = ref_spec->final_layout == VK_IMAGE_LAYOUT_UNDEFINED
					? layout : ref_spec->final_layout,
				.access_flags = access_flags,
				.stage_flags = stage_flags,
				.load_op
					= ref_spec->usage & RG_IMAGE_USE_CLEARED_BIT
					? VK_ATTACHMENT_LOAD_OP_CLEAR
					: ref_spec->usage & RG_IMAGE_USE_NO_READ_BIT
					? VK_ATTACHMENT_LOAD_OP_DONT_CARE
					: VK_ATTACHMENT_LOAD_OP_LOAD,
				.store_op = VK_ATTACHMENT_STORE_OP_STORE,
				.clear = ref_spec->clear_value,
			};
		}
		pass->color_attachment_count = color_attachment_count;
		RG_CHECK(color_attachment_count < 8, "too many color attachments");
		for (uint32_t j = 0, k = 0; j < pass_spec->image_ref_count; ++j) {
			if (pass_spec->image_refs[j].usage & RG_IMAGE_USE_COLOR_ATTACHMENT_BIT)
				pass->color_attachments[k++] = j;
		}

		pass->has_depth_attachment = has_depth_attachment;
		pass->depth_attachment = depth_attachment_index;

		pass->input_attachment_count = input_attachment_count;
		RG_CHECK(input_attachment_count < 8, "too many input attachments");
		// pass->input_attachments_own
		// 	= calloc(pass->input_attachment_count, sizeof(*pass->input_attachments_own));
		for (uint32_t j = 0, k = 0; j < pass_spec->image_ref_count; ++j) {
			if (pass_spec->image_refs[j].usage & RG_IMAGE_USE_INPUT_ATTACHMENT_BIT)
				pass->input_attachments[k++] = j;
		}
	}

	graph->current.swapchain_image.name = "Swapchain";
	graph->current.swapchain_image.image = VK_NULL_HANDLE;
	graph->current.swapchain_image.image_view = VK_NULL_HANDLE;
	graph->current.swapchain_image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	// Build image pass usage maps:

	for (size_t i = 0; i < graph->image_count + 1; ++i) {
		uint32_t image_index = i == graph->image_count ? UINT32_MAX : i;
		struct rg_graph_image *image = image_index == UINT32_MAX
			? &graph->current.swapchain_image
			: &graph->images_own[i];

		image->pass_use_map_own = calloc(graph->pass_count + 1, sizeof(*image->pass_use_map_own));
		memset(image->pass_use_map_own, 0xFF,
			(graph->pass_count + 1) * sizeof(*image->pass_use_map_own));
		uint32_t last_pass = 0;
		for (size_t j = 0; j < graph->pass_count; ++j) {
			struct rg_pass *pass = &graph->passes_own[j];
			for (size_t k = 0; k < pass->image_ref_count; ++k) {
				struct rg_image_ref *ref = &pass->image_refs_own[k];
				if (ref->image_index == image_index) {
					image->last_use_ref.image_index = image_index;
					image->last_use_ref.access_flags |= ref->access_flags;
					image->last_use_ref.stage_flags |= ref->stage_flags;
					image->last_use_ref.final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
					image->last_use_ref.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
					image->pass_use_map_own[last_pass] = (struct rg_graph_image_use){
						.pass_index = j,
						.ref_index = k,
					};
					last_pass = j + 1;
					break;
				}
			}
		}
	}

	rg_impl_build_merge_passes(graph);
	
	graph->current.command_buffer = VK_NULL_HANDLE;
}

void rg_free_graph(struct rg_graph *graph) {
	for (size_t i = 0; i < graph->image_count; ++i) {
		free(graph->images_own[i].pass_use_map_own);
	}
	for (size_t i = 0; i < graph->pass_count; ++i) {
		free(graph->passes_own[i].image_refs_own);
	}
	free(graph->passes_own);
}

static inline void rg_i_debug_vkCmdPipelineBarrier2(
	VkCommandBuffer commandBuffer,
	const VkDependencyInfo *pDependencyInfo
) {
#if defined(RG_DEBUG_PIPELINE_BARRIER_CALLS) && RG_DEBUG_PIPELINE_BARRIER_CALLS
	RG_DEBUG_PRINTF2("vkCmdPipelineBarrier2(%p, &(VkDependencyInfo){\n", commandBuffer);
	RG_DEBUG_PRINTF2("\t.imageMemoryBarrierCount = %u,\n", pDependencyInfo->imageMemoryBarrierCount);
	RG_DEBUG_PRINTF2("\t.pImageMemoryBarriers = (VkImageMemoryBarrier[]){\n");
	for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; ++i) {
		const VkImageMemoryBarrier2 *barrier = &pDependencyInfo->pImageMemoryBarriers[i];
		RG_DEBUG_PRINTF2("\t\t(VkImageMemoryBarrier){\n");
		RG_DEBUG_PRINTF2("\t\t\t.image = ... (%p)\n", barrier->image);
		RG_DEBUG_PRINTF2("\t\t\t.srcAccessMask = ");
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = barrier->srcAccessMask & (1ull << j);
			if (!bit) { if (cnt == 0 && j == 63) RG_DEBUG_PRINTF("0"); continue; };
			if (cnt != 0) RG_DEBUG_PRINTF("|");
			RG_DEBUG_PRINTF2("%s", pshine_vk_access_bit_string(bit)); cnt += 1;
		}
		RG_DEBUG_PRINTF2(",\n");
		RG_DEBUG_PRINTF2("\t\t\t.srcStageMask = ");
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = barrier->srcStageMask & (1ull << j);
			if (!bit) { if (cnt == 0 && j == 63) RG_DEBUG_PRINTF("0"); continue; };
			if (cnt != 0) RG_DEBUG_PRINTF2("|");
			RG_DEBUG_PRINTF2("%s", pshine_vk_stage_bit_string(bit)); cnt += 1;
		}
		RG_DEBUG_PRINTF2(",\n");
		RG_DEBUG_PRINTF2("\t\t\t.dstAccessMask = ");
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = barrier->dstAccessMask & (1ull << j);
			if (!bit) { if (cnt == 0 && j == 63) RG_DEBUG_PRINTF("0"); continue; };
			if (cnt != 0) RG_DEBUG_PRINTF2("|");
			RG_DEBUG_PRINTF2("%s", pshine_vk_access_bit_string(bit)); cnt += 1;
		}
		RG_DEBUG_PRINTF2(",\n");
		RG_DEBUG_PRINTF2("\t\t\t.dstStageMask = ");
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = barrier->dstStageMask & (1ull << j);
			if (!bit) { if (cnt == 0 && j == 63) RG_DEBUG_PRINTF2("0"); continue; };
			if (cnt != 0) RG_DEBUG_PRINTF2("|");
			RG_DEBUG_PRINTF2("%s", pshine_vk_stage_bit_string(bit)); cnt += 1;
		}
		RG_DEBUG_PRINTF2(",\n");
		RG_DEBUG_PRINTF2("\t\t\t.oldLayout = VK_IMAGE_LAYOUT_%s,\n",
			pshine_vk_layout_string(barrier->oldLayout));
		RG_DEBUG_PRINTF2("\t\t\t.newLayout = VK_IMAGE_LAYOUT_%s,\n",
			pshine_vk_layout_string(barrier->newLayout));
		RG_DEBUG_PRINTF2("\t\t\t...\n");
		RG_DEBUG_PRINTF2("\t\t},\n");
	}
	RG_DEBUG_PRINTF("\t},\n");
	RG_DEBUG_PRINTF("\t.dependencyFlags = %s,\n",
		pDependencyInfo->dependencyFlags == VK_DEPENDENCY_BY_REGION_BIT
			? "VK_DEPENDENCY_BY_REGION_BIT" : "...");
	RG_DEBUG_PRINTF("\t...\n");
	RG_DEBUG_PRINTF("}\n");
#endif
	vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

/// Increase count and write an appropriate image barrier
/// Region image barriers are written if the count isnt a nullptr
/// and if `dst_ref->access_flags & VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT`.
static void rg_i_graph_impl_barriers(
	struct rg_graph *graph,
	uint32_t pass,
	struct rg_image_ref *ref,
	uint32_t *image_barrier_count,
	VkImageMemoryBarrier2 *image_barriers,
	uint32_t *region_image_barrier_count,
	VkImageMemoryBarrier2 *region_image_barriers
) {

	struct rg_graph_image *image = ref->image_index == UINT32_MAX
		? &graph->current.swapchain_image
		: &graph->images_own[ref->image_index];

	struct rg_graph_image_use next_use = image->pass_use_map_own[pass];

	if (next_use.pass_index == UINT32_MAX) return;
	
	struct rg_pass *dst_pass = &graph->passes_own[next_use.pass_index];
	struct rg_image_ref *dst_ref = &dst_pass->image_refs_own[next_use.ref_index];

	RG_CHECK(dst_ref->image_index == ref->image_index, "invalid image pass use data");
	
	[[maybe_unused]]
	struct rg_graph_image *dst_image = dst_ref->image_index == UINT32_MAX
		? &graph->current.swapchain_image
		: &graph->images_own[ref->image_index];

	RG_CHECK(dst_image->image == image->image, "invalid image pass use data");

	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.image = image->image,
		.oldLayout = ref->final_layout,
		.srcAccessMask = ref->access_flags,
		.srcStageMask = ref->stage_flags,
		.srcQueueFamilyIndex = graph->current.queue_family_index,
		.newLayout = dst_ref->initial_layout,
		.dstAccessMask = dst_ref->access_flags,
		.dstStageMask = dst_ref->stage_flags,
		.dstQueueFamilyIndex = graph->current.queue_family_index,
		.subresourceRange = {
			.aspectMask = image->aspect,
			.baseArrayLayer = 0,
			.layerCount = 1,
			.baseMipLevel = 0,
			.levelCount = 1,
		},
	};

	bool by_region = dst_ref->access_flags & VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;

	if (!by_region || region_image_barrier_count == nullptr) {
		RG_CHECK(*image_barrier_count < 8, "too many image barriers");
		image_barriers[(*image_barrier_count)++] = barrier;
	} else if (region_image_barrier_count != nullptr) {
		RG_CHECK(*region_image_barrier_count < 8, "too many image barriers");
		region_image_barriers[(*region_image_barrier_count)++] = barrier;
	}


	RG_DEBUG_PRINTF("\t%s (%p)%s: ", image->name, image->image, by_region ? " (by region)" : "");
	for (unsigned j = 0, cnt = 0; j < 64; ++j) {
		VkFlags64 bit = ref->stage_flags & (1ull << j);
		if (!bit) continue;
		if (cnt != 0) RG_DEBUG_PRINTF("|");
		RG_DEBUG_PRINTF("%s", rg_i_vk_stage_bit_string(bit));
		cnt += 1;
	}
	RG_DEBUG_PRINTF("(");
	for (unsigned j = 0, cnt = 0; j < 64; ++j) {
		VkFlags64 bit = ref->access_flags & (1ull << j);
		if (!bit) continue;
		if (cnt != 0) RG_DEBUG_PRINTF("|");
		RG_DEBUG_PRINTF("%s", rg_i_vk_access_bit_string(bit));
		cnt += 1;
	}
	RG_DEBUG_PRINTF(") -> ");
	for (unsigned j = 0, cnt = 0; j < 64; ++j) {
		VkFlags64 bit = dst_ref->stage_flags & (1ull << j);
		if (!bit) continue;
		if (cnt != 0) RG_DEBUG_PRINTF("|");
		RG_DEBUG_PRINTF("%s", rg_i_vk_stage_bit_string(bit));
		cnt += 1;
	}
	RG_DEBUG_PRINTF("(");
	for (unsigned j = 0, cnt = 0; j < 64; ++j) {
		VkFlags64 bit = dst_ref->access_flags & (1ull << j);
		if (!bit) continue;
		if (cnt != 0) RG_DEBUG_PRINTF("|");
		RG_DEBUG_PRINTF("%s", rg_i_vk_access_bit_string(bit));
		cnt += 1;
	}
	RG_DEBUG_PRINTF(")\n");
	RG_DEBUG_PRINTF("\t    barrier.newLayout = %s\n", rg_i_vk_layout_string(barrier.newLayout));
	RG_DEBUG_PRINTF("\t    barrier.oldLayout = %s\n", rg_i_vk_layout_string(barrier.oldLayout));
}

void rg_graph_begin_frame(
	struct rg_graph *graph,
	VkRect2D render_area,
	uint32_t queue_family_index,
	VkImage swapchain_image,
	VkImageView swapchain_image_view,
	VkCommandBuffer command_buffer
) {
	graph->current.pass_index = 0;
	graph->current.render_area = render_area;
	graph->current.swapchain_image.image = swapchain_image;
	graph->current.swapchain_image.image_view = swapchain_image_view;
	graph->current.command_buffer = command_buffer;
	graph->current.queue_family_index = queue_family_index;

	RG_DEBUG_PRINTF("begin_frame; barriers:\n");

	uint32_t image_barrier_count = 0;
	VkImageMemoryBarrier2 image_barriers[8] = {};
	for (uint32_t i = 0; i < graph->image_count + 1; ++i) {
		// uint32_t image_index = i != graph->image_count ? i : UINT32_MAX;
		struct rg_graph_image *image = i != graph->image_count
			? &graph->images_own[i]
			: &graph->current.swapchain_image;
		// struct rg_pass *last_use_pass = &graph->passes_own[image->last_use.pass_index];
		// struct rg_image_ref last_use_ref = last_use_pass->image_refs_own[image->last_use.ref_index];
		// RG_CHECK(last_use_ref.image_index == image_index, "invalid image last_use");
		// last_use_ref.final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		// last_use_ref.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		rg_i_graph_impl_barriers(graph, 0, &image->last_use_ref,
			&image_barrier_count, image_barriers, nullptr, nullptr);
	}

	RG_DEBUG_PRINTF("begin_frame: pipeline barrier with %u image memory barriers.\n",
		image_barrier_count);
	if (graph->current.command_buffer && image_barrier_count > 0)
		rg_i_debug_vkCmdPipelineBarrier2(graph->current.command_buffer, &(VkDependencyInfo){
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = image_barrier_count,
			.pImageMemoryBarriers = image_barriers,
			.dependencyFlags = 0,
		});
}

void rg_graph_end_frame(struct rg_graph *graph) {
	graph->current.command_buffer = nullptr;
}

static VkRenderingAttachmentInfo rg_i_vulkan_from_image_ref(
	struct rg_image_ref *ref,
	struct rg_graph *graph
) {
	return (VkRenderingAttachmentInfo){
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageLayout = ref->initial_layout,
		.imageView = ref->image_index == UINT32_MAX
			? graph->current.swapchain_image.image_view
			: graph->images_own[ref->image_index].image_view,
		.loadOp = ref->load_op,
		.storeOp = ref->store_op,
		.clearValue = ref->clear,
	};
}

void rg_graph_begin_pass(struct rg_graph *graph) {
	struct rg_pass *pass = &graph->passes_own[graph->current.pass_index];
	RG_DEBUG_PRINTF("begin_pass %s%s\n", pass->name, pass->merged_with_prev ? " (<-merged)" : "");
	if (pass->merged_with_prev) return;

	for (uint32_t i = 0; i < pass->image_ref_count; ++i) {
		struct rg_image_ref *ref = &pass->image_refs_own[i];
		[[maybe_unused]]
		struct rg_graph_image *image = ref->image_index == UINT32_MAX
			? &graph->current.swapchain_image
			: &graph->images_own[ref->image_index];
		RG_DEBUG_PRINTF("\t%s (%p): ", image->name, image->image);
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = ref->stage_flags & (1ull << j);
			if (!bit) continue;
			if (cnt != 0) RG_DEBUG_PRINTF("|");
			RG_DEBUG_PRINTF("%s", rg_i_vk_stage_bit_string(bit));
			cnt += 1;
		}
		RG_DEBUG_PRINTF("(");
		for (unsigned j = 0, cnt = 0; j < 64; ++j) {
			VkFlags64 bit = ref->access_flags & (1ull << j);
			if (!bit) continue;
			if (cnt != 0) RG_DEBUG_PRINTF("|");
			RG_DEBUG_PRINTF("%s", rg_i_vk_access_bit_string(bit));
			cnt += 1;
		}
		RG_DEBUG_PRINTF(")\n");
	}

	if (pass->compute) return;
	VkRenderingAttachmentInfo color_attachments[12]; // pass->color_attachment_count
	RG_CHECK(pass->color_attachment_count <= 12, "max color attachment count is 12");
	for (size_t i = 0; i < pass->color_attachment_count; ++i) {
		size_t ref_idx = pass->color_attachments[i];
		struct rg_image_ref *ref = &pass->image_refs_own[ref_idx];
		color_attachments[i] = rg_i_vulkan_from_image_ref(ref, graph);
	}

	VkRenderingAttachmentInfo depth_attachment;
	if (pass->has_depth_attachment)
		depth_attachment = rg_i_vulkan_from_image_ref(
			&pass->image_refs_own[pass->depth_attachment],
			graph
		);

	if (graph->current.command_buffer) {
		// fprintf(stderr, "vkCmdBeginRendering();\n");
		vkCmdBeginRendering(graph->current.command_buffer, &(VkRenderingInfo){
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = pass->render_area.extent.width != 0
				? pass->render_area
				: graph->current.render_area,
			.colorAttachmentCount = pass->color_attachment_count,
			.pColorAttachments = color_attachments,
			.pDepthAttachment = pass->has_depth_attachment
				? &depth_attachment : nullptr,
			.layerCount = 1,
		});
	}
}

void rg_graph_end_pass(struct rg_graph *graph) {
	// RG_DEBUG_PRINTF("current pass index: %u\n", graph->current.pass_index);
	struct rg_pass *pass = &graph->passes_own[graph->current.pass_index];
	if (!pass->compute && !pass->merged_with_next && graph->current.command_buffer) {
		// fprintf(stderr, "vkCmdEndRendering();\n");
		vkCmdEndRendering(graph->current.command_buffer);
	}

	RG_DEBUG_PRINTF("end_pass %s%s; barriers:\n",
		pass->name, pass->merged_with_next ? " (merged->)" : "");

	uint32_t image_barrier_count = 0;
	VkImageMemoryBarrier2 image_barriers[8] = {};
	uint32_t region_image_barrier_count = 0;
	VkImageMemoryBarrier2 region_image_barriers[8] = {};
	for (uint32_t i = 0; i < pass->image_ref_count; ++i) {
		struct rg_image_ref *ref = &pass->image_refs_own[i];
		rg_i_graph_impl_barriers(
			graph,
			graph->current.pass_index + 1,
			ref,
			&image_barrier_count,
			image_barriers,
			pass->merged_with_next ? &region_image_barrier_count : nullptr,
			region_image_barriers
		);
	}
	RG_DEBUG_PRINTF("\n");

	if (graph->current.command_buffer) {
		if (!pass->merged_with_next && image_barrier_count > 0) {
			// TODO: postpone this barrier to last merged
			rg_i_debug_vkCmdPipelineBarrier2(graph->current.command_buffer, &(VkDependencyInfo){
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.imageMemoryBarrierCount = image_barrier_count,
				.pImageMemoryBarriers = image_barriers,
				.dependencyFlags = 0,
			});
			
		}
		if (region_image_barrier_count > 0) {
			rg_i_debug_vkCmdPipelineBarrier2(graph->current.command_buffer, &(VkDependencyInfo){
				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
				.imageMemoryBarrierCount = region_image_barrier_count,
				.pImageMemoryBarriers = region_image_barriers,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
			});
		}
	}

	graph->current.pass_index += 1;
}

void rg_graph_pass_last_use(
	struct rg_graph *graph,
	rg_image_id id
) {
	if (id == RG_IMAGE_SWAPCHAIN) return;
}

void rg_graph_create_dot_file(struct rg_graph *graph, const char *fpath) {
	FILE *fout = fopen(fpath, "w");
	fprintf(fout, "digraph render_graph {\n");
	for (size_t i = 0; i < graph->image_count; ++i) {
		fprintf(fout, "\timg%zu [label=\"%s\"];\n", i, graph->images_own[i].name);
	}
	fprintf(fout, "\timgswap [label=\"%s\"];\n", "Swapchain");
	for (size_t i = 0; i < graph->pass_count; ++i) {
		struct rg_pass *pass = &graph->passes_own[i];
		fprintf(fout, "\tpass%zu [label=\"%s\",shape=box,style=filled];\n", i, pass->name);
		if (i != 0) fprintf(fout, "\tpass%zu -> pass%zu [style=bold];\n", i - 1, i);
		for (size_t j = 0; j < pass->image_ref_count; ++j) {
			size_t idx = pass->image_refs_own[j].image_index;
			fprintf(fout, "\tpass%zu -> ", i);
			if (idx == UINT32_MAX) {
				fprintf(fout, "imgswap");
			} else {
				fprintf(fout, "img%zu", idx);
			}
			fprintf(fout, "\n");
		}
	}
	fprintf(fout, "}\n");
	fclose(fout);
}

[[maybe_unused]] static void example_render_graph(struct rg_graph *graph) {
	struct { VkImageView view; VkImage image; VkFormat format; VkImageAspectFlags aspect; }
		color0 = { 0, 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT },
		depth0 = { 0, 0, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT },
		gbuffer0 = { 0, 0, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT },
		gbuffer1 = { 0, 0, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT },
		gbuffer2 = { 0, 0, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT },
		shadow0 = { 0, 0, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT };
	enum images : rg_image_id {
		IMG_COLOR0,
		IMG_DEPTH0,
		IMG_GBUFFER0,
		IMG_GBUFFER1,
		IMG_GBUFFER2,
		IMG_SHADOW0,
		IMG_COUNT,
	};
	struct rg_pass_spec rpasses[] = {
		(struct rg_pass_spec){
			.name = "shadow",
			.image_ref_count = 1,
			.image_refs = (struct rg_image_ref_spec[]){
				{ IMG_SHADOW0, RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT },
			},
		},
		(struct rg_pass_spec){
			.name = "hdr geometry",
			.image_ref_count = 4,
			.image_refs = (struct rg_image_ref_spec[]){
				{ IMG_DEPTH0, RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT },
				{ IMG_GBUFFER0, RG_IMAGE_USE_COLOR_ATTACHMENT_BIT },
				{ IMG_GBUFFER1, RG_IMAGE_USE_COLOR_ATTACHMENT_BIT },
				{ IMG_GBUFFER2, RG_IMAGE_USE_COLOR_ATTACHMENT_BIT },
			},
		},
		(struct rg_pass_spec){
			.name = "hdr atmosphere",
			.image_ref_count = 2,
			.image_refs = (struct rg_image_ref_spec[]){
				{ IMG_COLOR0,
					RG_IMAGE_USE_COLOR_ATTACHMENT_BIT |
					RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_DEPTH0, RG_IMAGE_USE_DEPTH_ATTACHMENT_BIT },
			},
		},
		(struct rg_pass_spec){
			.name = "hdr lighting",
			.image_ref_count = 6,
			.image_refs = (struct rg_image_ref_spec[]){
				{ IMG_COLOR0,
					RG_IMAGE_USE_COLOR_ATTACHMENT_BIT |
					RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_DEPTH0, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_GBUFFER0, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_GBUFFER1, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_GBUFFER2, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_SHADOW0, RG_IMAGE_USE_SAMPLED_BIT },
			},
		},
		(struct rg_pass_spec){
			.name = "bloom",
			.image_ref_count = 1,
			.image_refs = (struct rg_image_ref_spec[]){
				(struct rg_image_ref_spec){
					.image_id = IMG_COLOR0,
					.usage = RG_IMAGE_USE_SAMPLED_BIT,
					.final_layout = VK_IMAGE_LAYOUT_GENERAL,
				},
			},
		},
		(struct rg_pass_spec){
			.name = "sdr composite",
			.image_ref_count = 3,
			.image_refs = (struct rg_image_ref_spec[]){
				{ RG_IMAGE_SWAPCHAIN, RG_IMAGE_USE_COLOR_ATTACHMENT_BIT },
				{ IMG_COLOR0, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
				{ IMG_DEPTH0, RG_IMAGE_USE_INPUT_ATTACHMENT_BIT },
			},
		},
		(struct rg_pass_spec){
			.name = "sdr gui",
			.image_ref_count = 1,
			.image_refs = (struct rg_image_ref_spec[]){
				{ RG_IMAGE_SWAPCHAIN, RG_IMAGE_USE_COLOR_ATTACHMENT_BIT },
			},
		},
	};
	rg_build_graph(&(struct rg_graph_spec){
		.image_count = IMG_COUNT,
		.images = (struct rg_graph_image_spec[]){
			{ "Color 0", color0.image, color0.view, color0.format, color0.aspect },
			{ "Depth 0", depth0.image, depth0.view, depth0.format, depth0.aspect },
			{ "GBuffer 0", gbuffer0.image, gbuffer0.view, gbuffer0.format, gbuffer0.aspect },
			{ "GBuffer 1", gbuffer1.image, gbuffer1.view, gbuffer1.format, gbuffer1.aspect },
			{ "GBuffer 2", gbuffer2.image, gbuffer2.view, gbuffer2.format, gbuffer2.aspect },
			{ "Shadow 0", shadow0.image, shadow0.view, shadow0.format, shadow0.aspect },
		},
		.pass_count = sizeof(rpasses) / sizeof(*rpasses),
		.passes = rpasses,
	}, graph);
}

#endif // RG_IMPLEMENTATION

#endif // VKRGRAPH_H_
