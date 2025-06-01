#include "game.h"
#include <cimgui/cimgui.h>

struct eximgui_state {
	ImGuiStorage storage;
	ImFont *font_regular;
	ImFont *font_bold;
	ImFont *font_italic;
	ImFont *font_title;
};

struct eximgui_state g__eximgui_state_ = {};
static struct eximgui_state *const g__eximgui_state = &g__eximgui_state_;

static inline ImVec4 rgbint_to_vec4(int r, int g, int b, int a) {
	return (ImVec4){ r / 255.f, g / 255.f, b / 255.f, a / 255.f };
}

void init_imgui_style() {
	ImGuiStyle *st = ImGui_GetStyle();
	st->Colors[ImGuiCol_WindowBg] = rgbint_to_vec4(0x02, 0x02, 0x02, 0xFE);
	st->Colors[ImGuiCol_Button] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_ButtonHovered] = rgbint_to_vec4(0x0E, 0x0E, 0x0E, 0xFF);
	st->Colors[ImGuiCol_ButtonActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_FrameBg] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_FrameBgHovered] = rgbint_to_vec4(0x0E, 0x0E, 0x0E, 0xFF);
	st->Colors[ImGuiCol_FrameBgActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_SliderGrab] = rgbint_to_vec4(0x30, 0x30, 0x30, 0xFF);
	st->Colors[ImGuiCol_SliderGrabActive] = rgbint_to_vec4(0x2C, 0x2C, 0x2C, 0xFF);
	st->Colors[ImGuiCol_Tab] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabActive] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabHovered] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabSelectedOverline] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TabDimmedSelectedOverline] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TabDimmedSelected] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TabDimmed] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TitleBg] = rgbint_to_vec4(0x05, 0x05, 0x05, 0xFF);
	st->Colors[ImGuiCol_TitleBgActive] = rgbint_to_vec4(0x0A, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_TitleBgCollapsed] = rgbint_to_vec4(0, 0, 0, 0xE0);
	st->Colors[ImGuiCol_TextSelectedBg] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_Header] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_HeaderActive] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_HeaderHovered] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_ResizeGrip] = rgbint_to_vec4(0x0C, 0x0C, 0x0C, 0xFF);
	st->Colors[ImGuiCol_ResizeGripActive] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_ResizeGripHovered] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_BorderShadow] = rgbint_to_vec4(0x12, 0x0A, 0x0A, 0xFF);
	st->Colors[ImGuiCol_SeparatorHovered] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0xFF);
	st->Colors[ImGuiCol_SeparatorActive] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0xFF);
	st->Colors[ImGuiCol_DockingEmptyBg] = rgbint_to_vec4(0x1F, 0x11, 0x11, 0x00);
}

void eximgui_begin_frame() {
	ImGui_DockSpaceOverViewportEx(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode, nullptr);
	ImGui_PushFont(g__eximgui_state->font_regular);
}

void eximgui_end_frame() {
	ImGui_PopFont();
}

void eximgui_init() {
	ImVector_Construct(&g__eximgui_state->storage);
	ImGuiIO *io = ImGui_GetIO();
	// ImFontConfig cfg = {};
	static const ImWchar ranges[] =
	{
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x2070, 0x209F, // Superscripts and Subscripts
		0,
	};
	float base_font_size = 14.f;
	g__eximgui_state->font_regular =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Regular.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_italic =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Italic.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_bold =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Bold.ttf", base_font_size, nullptr, ranges);
	g__eximgui_state->font_title =
		ImFontAtlas_AddFontFromFileTTF(io->Fonts, "data/fonts/inter/Inter-Bold.ttf", 24.0f, nullptr, ranges);
	init_imgui_style();
}

void eximgui_deinit() {
	ImVector_Destruct(&g__eximgui_state->storage);
}

void eximgui_bold_text(const char *fmt, ...) {
	ImGui_PushFont(g__eximgui_state->font_bold);
	va_list va;
	va_start(va, fmt);
	ImGui_TextV(fmt, va);
	va_end(va);
	ImGui_PopFont();
}

void eximgui_title_text(const char *fmt, ...) {
	ImGui_PushFont(g__eximgui_state->font_title);
	va_list va;
	va_start(va, fmt);
	ImGui_TextV(fmt, va);
	va_end(va);
	ImGui_PopFont();
}

bool eximgui_begin_input_box(const char *label, const char *tooltip) {
	ImGuiID id = ImGui_GetID(label);
	bool is_open = ImGuiStorage_GetBool(&g__eximgui_state->storage, id, true);

	ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0);
	ImGui_PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0);
	ImGui_PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, 1.0);
	ImGui_PushStyleColor(ImGuiCol_ChildBg, 0xFF050505);
	bool r = ImGui_BeginChild(label, (ImVec2){}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border, 0);
	ImGui_SetCursorPosY(ImGui_GetCursorPosY() - 5.0f);
	ImGui_SeparatorText(label);
	ImGui_SetItemTooltip("%s", tooltip);
	if (ImGui_IsItemClicked()) {
		is_open = !is_open;
		ImGuiStorage_SetBool(&g__eximgui_state->storage, id, is_open);
	}
	ImGui_PopStyleColor();
	ImGui_PopStyleVar();
	ImGui_PopStyleVar();
	ImGui_PopStyleVar();
	// if (!is_open) {
	// 	ImVec2 win_size = ImGui_GetWindowSize();
	// 	win_size.y -= 10.0f;
	// 	ImGui_SetWindowSize(win_size, ImGuiCond_Always);
	// }
	// ImGui_PopStyleVar();
	return r && is_open;
}

void eximgui_end_input_box() {
	ImGui_EndChild();
}

/// NB: always start label with '##'.
bool eximgui_input_double3(const char *label, const char *tooltip, double *vs, double step, const char *format) {
	bool res = false;
	if (eximgui_begin_input_box(label + 2, tooltip)) {
		ImGui_PushIDInt(0);
		ImGui_Text("X"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[0], step, step * 100.0, format, 0);
		ImGui_PopID();
		ImGui_PushIDInt(1);
		ImGui_Text("Y"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[1], step, step * 100.0, format, 0);
		ImGui_PopID();
		ImGui_PushIDInt(2);
		ImGui_Text("Z"); ImGui_SameLine();
		res |= ImGui_InputDoubleEx(label, &vs[2], step, step * 100.0, format, 0);
		ImGui_PopID();
	}
	eximgui_end_input_box();
	return res;
}


void eximgui_plot(const struct eximgui_plot_info *info) {
	const float x_min = info->x_min;
	const float x_max = info->x_max;
	const float x_d = x_max - x_min;

	float y_min = float_pinfty, y_max = float_ninfty;
	if (!info->auto_y_range) {
		y_min = info->y_min;
		y_max = info->y_max;
	}
	if (info->auto_y_range || info->extend_by_auto_range) {
		for (size_t i = 0; i < info->y_count; ++i) {
			if (info->ys[i] >= y_max) y_max = info->ys[i];
			if (info->ys[i] <= y_min) y_min = info->ys[i];
		}
	}
	const float y_d = y_max - y_min;

	const ImGuiStyle *style = ImGui_GetStyle();
	const ImVec2 cursor_pos = ImGui_GetCursorScreenPos();

	const float left_margin = 50.0f;

	const ImVec2 off = {
		cursor_pos.x + style->ItemSpacing.x + left_margin,
		cursor_pos.y + style->ItemSpacing.y,
	};
	const ImVec2 size = {
		ImGui_GetContentRegionAvail().x - 2.0 * style->ItemSpacing.x - left_margin,
		200.0f - 2.0 * style->ItemSpacing.y,
	};

	ImVec2 line_pts[info->y_count];
	float pt_step_x = size.x / info->y_count;
	// float pt_scale_x = size.x / x_d;
	float pt_scale_y = size.y / y_d;
	for (size_t i = 0; i < info->y_count; ++i) {
		line_pts[i].x = off.x + pt_step_x * i;
		line_pts[i].y = off.y + size.y - (info->ys[i] - y_min) * pt_scale_y;
	}

	ImDrawList *drawlist = ImGui_GetWindowDrawList();

	{
		size_t i = 0;
		const float Δx = x_d / info->x_ticks;
		for (float x = x_min; x < x_max; x += Δx, ++i) {
			const float gui_x = off.x + size.x * (x - x_min) / x_d;
			ImDrawList_AddLine(drawlist, (ImVec2){ gui_x, off.y }, (ImVec2){ gui_x, off.y + size.y },
				i % 4 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
			if (i % 8 == 0) {
				char s[32] = {};
				snprintf(s, sizeof s, "%g", x);
				ImDrawList_AddText(drawlist, (ImVec2){
					gui_x,
					off.y + size.y + style->ItemInnerSpacing.y
				}, 0x10FFFFFF, s);
			}
		}
		i = 0;
		const float Δy = y_d / info->y_ticks;
		for (
			float y = y_min;
			y < y_max;
			y += Δy, ++i
		) {
			const float gui_y = off.y + size.y - size.y * (y - y_min) / y_d;
			ImDrawList_AddLine(drawlist, (ImVec2){ off.x, gui_y }, (ImVec2){ off.x + size.x, gui_y },
				i % 2 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
			if (i % 8 == 0) {
				char s[32] = {};
				snprintf(s, sizeof s, "%.3g", y);
				ImVec2 text_size = ImGui_CalcTextSize(s);
				ImDrawList_AddText(drawlist, (ImVec2){
					off.x - text_size.x - style->ItemInnerSpacing.x,
					gui_y - text_size.y * 0.5
				}, 0x10FFFFFF, s);
			}
		}
	}

	ImDrawList_PushClipRect(drawlist, off,
		(ImVec2){ off.x + size.x, off.y + size.y }, false);
	ImDrawList_AddPolyline(drawlist, line_pts, info->y_count, 0xFFFFFFFF, ImDrawFlags_None, 2.0f);
	// ImDrawList_AddPolyline(drawlist, old_plot_positions, info->y_count, 0x02FFFFFF, ImDrawFlags_None, 1.0f);
	ImDrawList_PopClipRect(drawlist);

}
