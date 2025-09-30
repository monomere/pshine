#include "game.h"
#include <cimgui/cimgui.h>

static void spectrometry_imgui(struct pshine_game *game, float actual_delta_time) {
	if (ImGui_Begin("Spectrometry", nullptr, 0)) {
		struct pshine_celestial_body *star_body = game->star_systems_own[game->current_star_system].bodies_own[0];
		if (star_body->type != PSHINE_CELESTIAL_BODY_STAR) {
			ImGui_PushStyleColor(ImGuiCol_Text, ImGui_ColorConvertFloat4ToU32((ImVec4){1.0, 0.2, 0.2, 1.0}));
			ImGui_Text("Configuration error: the first specified celestial body must be the star.");
			ImGui_Text("Multi-star system support TBD.");
			ImGui_PopStyleColor();
			ImGui_End();
			return;
		}

		struct pshine_star *star = (void*)star_body;
		double dist2 = double3mag2(double3sub(
			double3mul(double3vs(game->camera_position.values), PSHINE_XCS_WCS_FACTOR),
			double3mul(double3vs(star_body->position.values), PSHINE_XCS_WCS_FACTOR)
		));

		float temp = star->temperature;
		// ImGui_SliderFloat("Temp, K", &temp, 1200, 8000);

		enum : size_t { SAMPLE_COUNT = 50 };
		float values[SAMPLE_COUNT];
		const float λ_min = 240.0f;
		const float λ_max = 1840.0f; // 2.8977721e-3 / temp * 1e9;
		const float λ_step = (λ_max - λ_min) / SAMPLE_COUNT;

		for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
			values[i] = (double)pshine_blackbody_radiation(λ_min + λ_step * i, temp) / dist2;
			// introduce some noise
			// values[i] += ((double)pshine_pcg64_random_float(&game->rng64) * 2.0 - 1.0) * 0.0001 / sqrt(dist2);
			// values[i] += (pshine_pcg64_random_float(&game->rng64) * 2.0 - 1.0) * 0.0001;
		}

		eximgui_plot(&(struct eximgui_plot_info){
			.y_count = SAMPLE_COUNT,
			.ys = values,
			.x_min = λ_min,
			.x_max = λ_max,
			.x_ticks = 30,
			.y_ticks = 30,
			.auto_y_range = true,
			.extend_by_auto_range = true,
			.y_min = -0.0004,
			.y_max = 0.05,
		});

		// double value_norm_min = -0.02f;
		// double value_norm_max = 1.0f;
		// double value_max = -9999.99f;
		// {
		// }

		// ImGui_Text("%f", value_max);

		// const ImGuiStyle *style = ImGui_GetStyle();
		// const ImVec2 cursor_pos = ImGui_GetCursorScreenPos();
		// const ImVec2 plot_off = {
		// 	cursor_pos.x + style->ItemSpacing.x + 50.0f,
		// 	cursor_pos.y + style->ItemSpacing.y,
		// };
		// const ImVec2 plot_size = {
		// 	ImGui_GetContentRegionAvail().x - 2.0 * style->ItemSpacing.x - 50.0f,
		// 	200.0f - 2.0 * style->ItemSpacing.y,
		// };

		// static ImVec2 old_plot_positions[SAMPLE_COUNT];
		// static ImVec2 plot_positions[SAMPLE_COUNT];

		// float pt_step_x = plot_size.x / SAMPLE_COUNT;
		// float pt_scale_y = plot_size.y / (value_norm_max - value_norm_min);
		// {
		// 	for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
		// 		plot_positions[i].x = plot_off.x + pt_step_x * i;
		// 		plot_positions[i].y = plot_off.y + plot_size.y - (values[i] / value_max - value_norm_min) * pt_scale_y;
		// 	}
		// }

		// ImDrawList *drawlist = ImGui_GetWindowDrawList();

		// {
		// 	size_t i = 0;
		// 	for (float λ = λ_min; λ < λ_max; (λ += 10.0f), ++i) {
		// 		const float x = plot_off.x + plot_size.x * ((λ - λ_min) / (λ_max - λ_min));
		// 		ImDrawList_AddLine(drawlist, (ImVec2){ x, plot_off.y }, (ImVec2){ x, plot_off.y + plot_size.y },
		// 			i % 4 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
		// 		if (i % 8 == 0) {
		// 			char s[32] = {};
		// 			snprintf(s, sizeof s, "%g", λ);
		// 			ImDrawList_AddText(drawlist, (ImVec2){
		// 				x,
		// 				plot_off.y + plot_size.y + style->ItemInnerSpacing.y
		// 			}, 0x10FFFFFF, s);
		// 		}
		// 	}
		// 	i = 0;
		// 	for (
		// 		float l = value_norm_min * value_max;
		// 		l < value_max;
		// 		(l += 2.0f * (value_max - value_min) / 50.0f), ++i
		// 	) {
		// 		ImDrawList_AddLine(drawlist, (ImVec2){
		// 			plot_off.x,
		// 			plot_off.y + plot_size.y - (l / value_max - value_norm_min) * pt_scale_y,
		// 		}, (ImVec2){
		// 			plot_off.x + plot_size.x,
		// 			plot_off.y + plot_size.y - (l / value_max - value_norm_max) * pt_scale_y,
		// 		}, i % 2 == 0 ? 0x10FFFFFF : 0x05FFFFFF);
		// 		if (i % 8 == 0) {
		// 			char s[32] = {};
		// 			snprintf(s, sizeof s, "%.3g", l - value_norm_min);
		// 			ImVec2 text_size = ImGui_CalcTextSize(s);
		// 			ImDrawList_AddText(drawlist, (ImVec2){
		// 				plot_off.x - text_size.x - style->ItemInnerSpacing.x,
		// 				plot_off.y + plot_size.y - l * pt_scale_y - text_size.y * 0.5
		// 			}, 0x10FFFFFF, s);
		// 		}
		// 	}
		// }

		// ImDrawList_PushClipRect(drawlist, plot_off,
		// 	(ImVec2){ plot_off.x + plot_size.x, plot_off.y + plot_size.y }, false);
		// ImDrawList_AddPolyline(drawlist, plot_positions, SAMPLE_COUNT, 0xFFFFFFFF, ImDrawFlags_None, 2.0f);
		// ImDrawList_AddPolyline(drawlist, old_plot_positions, SAMPLE_COUNT, 0x02FFFFFF, ImDrawFlags_None, 1.0f);
		// ImDrawList_PopClipRect(drawlist);

		// for (size_t i = 0; i < SAMPLE_COUNT; ++i) {
		// 	old_plot_positions[i] = plot_positions[i];
		// }
	}
	ImGui_End();
}

static void science_imgui(struct pshine_game *game, float actual_delta_time) {
	spectrometry_imgui(game, actual_delta_time);

	if (ImGui_Begin("Gravity", nullptr, 0)) {
		ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0);
		ImGui_PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0);
		ImGui_PushStyleColor(ImGuiCol_ChildBg, 0xFF050505);
		ImVec2 size = { 100, 100 };
		if (ImGui_BeginChild("Superposition", size, ImGuiChildFlags_Border, 0)) {
			double3 sum = double3v0();
			for (size_t i = 0; i < game->star_systems_own[game->current_star_system].body_count; ++i) {
				struct pshine_celestial_body *body = game->star_systems_own[game->current_star_system].bodies_own[i];
				double3 d = double3sub(
					double3mul(double3vs(game->camera_position.values), PSHINE_XCS_WCS_FACTOR),
					double3mul(double3vs(body->position.values), PSHINE_XCS_WCS_FACTOR)
				);
				sum = double3add(sum, double3div(d, double3mag2(d) / body->gravitational_parameter));
			}
			float3 dir = float3_double3(double3norm(sum));
			ImDrawList *drawlist = ImGui_GetWindowDrawList();
			float radius = 40.0f;
			ImGuiStyle *style = ImGui_GetStyle();
			ImVec2 off = ImGui_GetCursorScreenPos();
			off.x -= style->WindowPadding.x;
			off.y -= style->WindowPadding.y;
			ImVec2 center = { off.x + size.x / 2, off.y + size.y / 2 };
			ImDrawList_AddCircle(
				drawlist,
				center,
				radius,
				0x10FFFFFF
			);


			float3x3 mat = {};
			setfloat3x3rotation(&mat, 0.0, -game->data_own->camera_pitch, 0.0);
			{
				float3x3 mat2 = {};
				setfloat3x3rotation(&mat2, -game->data_own->camera_yaw, 0.0, 0.0);
				float3x3mul(&mat, &mat2);
			}
			dir = float3x3mulv(&mat, dir);
			float2 pdir = float2xy(-dir.x, dir.y);

			float2 arrow_end = float2xy(pdir.x * radius, pdir.y * radius);
			float2 arrow_end_n = float2norm(arrow_end);

			ImDrawList_AddLine(
				drawlist,
				center,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				0xFF0000FF
			);
			ImDrawList_AddLine(
				drawlist,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				(ImVec2){
					center.x + arrow_end.x + arrow_end_n.y * 5 - arrow_end_n.x * 5,
					center.y + arrow_end.y - arrow_end_n.x * 5 - arrow_end_n.y * 5,
				},
				0xFF0000FF
			);
			ImDrawList_AddLine(
				drawlist,
				(ImVec2){ center.x + arrow_end.x, center.y + arrow_end.y },
				(ImVec2){
					center.x + arrow_end.x - arrow_end_n.y * 5 - arrow_end_n.x * 5,
					center.y + arrow_end.y + arrow_end_n.x * 5 - arrow_end_n.y * 5,
				},
				0xFF0000FF
			);
		}
		ImGui_EndChild();
		ImGui_PopStyleColor();
		ImGui_PopStyleVar();
		ImGui_PopStyleVar();
	}
	ImGui_End();
}

void draw_debug_windows(struct pshine_game *game, float actual_delta_time) {
#define SCSd3_WCSd3(wcs) (double3mul((wcs), PSHINE_SCS_FACTOR))
#define SCSd3_WCSp3(wcs) SCSd3_WCSd3(double3vs((wcs).values))
#define SCSd_WCSd(wcs) ((wcs) * PSHINE_SCS_FACTOR)
	eximgui_begin_frame();

	if (ImGui_Begin("Material", nullptr, 0)) {
		ImGui_DragFloat("Smoothness", &game->material_smoothness_);
	}
	ImGui_End();

	if (ImGui_Begin("Star Systems", nullptr, 0)) {
		for (size_t i = 0; i < game->star_system_count; ++i) {
			bool selected = i == game->current_star_system;
			if (ImGui_SelectableBoolPtr(game->star_systems_own[i].name_own, &selected, ImGuiSelectableFlags_None)) {
				game->current_star_system = i;
				if (game->star_systems_own[game->current_star_system].body_count <= game->data_own->selected_body) {
					game->data_own->selected_body = game->star_systems_own[game->current_star_system].body_count;
				}
			}
		}
	}
	ImGui_End();

	struct pshine_star_system *current_system = &game->star_systems_own[game->current_star_system];
	if (ImGui_Begin("Celestial Bodies", nullptr, 0)) {
		for (size_t i = 0; i < current_system->body_count; ++i) {
			bool selected = i == game->data_own->selected_body;
			if (ImGui_SelectableBoolPtr(current_system->bodies_own[i]->name_own, &selected, ImGuiSelectableFlags_None)) {
				game->data_own->selected_body = i;
			}
		}
	}
	ImGui_End();

	struct pshine_celestial_body *body = current_system->bodies_own[game->data_own->selected_body];

	if (ImGui_Begin("Orbit", NULL, 0)) {
		if (!body->is_static) {
			ImGui_Text("True anomaly: %.5f", body->orbit.true_anomaly);
			double3 pos = (SCSd3_WCSp3(body->position));
			ImGui_Text("Position (SCS): %.0f, %.0f, %.0f", pos.x, pos.y, pos.z);
			double μ = body->parent_ref->gravitational_parameter;
			double a = body->orbit.semimajor; // The semimajor axis.
			double e = body->orbit.eccentricity; // The eccentricity.

			double p = a * (1 - e*e);

			double u = NAN; // Mean motion.
			if (fabs(e - 1.0) < 1e-6) { // parabolic
				u = 2.0 * sqrt(μ / (p*p*p));
			} else if (e < 1.0) { // elliptic
				u = sqrt(μ / (a*a*a));
			} else if (e < 1.0) { // hyperbolic
				u = sqrt(μ / -(a*a*a));
			} else {
				unreachable();
			}

			double T = 2 * π / u; // Orbital period.
			struct time_format_params time_fmt = compute_time_format_params(T);
			ImGui_Text("Period: " TIME_FORMAT, TIME_FORMAT_ARGS(time_fmt));
		} else {
			ImGui_PushStyleColor(ImGuiCol_Text, 0xff747474);
			ImGui_Text("This body doesn't have an orbit.");
			ImGui_PopStyleColor();
		}
	}
	ImGui_End();

	if (ImGui_Begin("Camera", NULL, 0)) {
		double3 p = double3vs(game->camera_position.values);
		double3 p_scs = double3mul(p, PSHINE_SCS_FACTOR);
		double d = double3mag(double3sub(p, double3vs(body->position.values))) - body->radius;
		enum si_prefix d_prefix = find_optimal_si_prefix(d);
		double d_scaled = apply_si_prefix(d_prefix, d);
		eximgui_input_double3("##WCS Position", "World coordinate system camera position. In meters.",
			game->camera_position.values, 1000.0, "%.3fm");
		if (eximgui_input_double3("##SCS Position", "Scaled coordinate system camera position. 1:8192.",
			p_scs.vs, 100.0, "%.3fu")) {
			*(double3*)&game->camera_position.values = double3mul(p_scs, PSHINE_SCS_SCALE);
		}
		if (eximgui_begin_input_box("Movement speed", "The speed at which the camera moves.")) {
			ImGui_Text("Speed, m/s");
			ImGui_SameLine();
			ImGui_InputDouble("##Movement speed", &game->data_own->move_speed);
			ImGui_Text("That's %0.3fc", game->data_own->move_speed / PSHINE_SPEED_OF_LIGHT);
			struct {
				const char *name;
				const char *tooltip;
				double value;
			} marks[] = {
				{ "Crawl", "100 m/s", 100.0 },
				{ "Snail", "1 km/s", 1000.0 },
				{ "Slow", "10 km/s", 10000.0 },
				{ "Fast", "500 km/s", 5.0e5 },
				{ "Faster", "50 Mm/s", 5.0e7 },
				{ "Light", "≈300 Mm/s", PSHINE_SPEED_OF_LIGHT },
				{ "10c", "10 times the speed of light", PSHINE_SPEED_OF_LIGHT * 10.0 },
				{ "166c", "≈166 times the speed of light", 5.0e10 },
				{ "1kc", "1000 times the speed of light", PSHINE_SPEED_OF_LIGHT * 1000.0 },
				{ "2kc", "2000 times the speed of light", PSHINE_SPEED_OF_LIGHT * 2000.0 },
			};
			for (size_t i = 0; i < sizeof(marks)/sizeof(*marks); ++i) {
				if (ImGui_Button(marks[i].name)) game->data_own->move_speed = marks[i].value;
				ImGui_SetItemTooltip("%s", marks[i].tooltip);
				ImGui_SameLine();
				if (
					i + 1 >= sizeof(marks)/sizeof(*marks) ||
					0
					+ ImGui_GetStyle()->ItemSpacing.x
					+ ImGui_GetStyle()->ItemInnerSpacing.x * 2.0
					+ ImGui_CalcTextSize(marks[i + 1].name).x
					>= ImGui_GetContentRegionAvail().x * 2.0
				) ImGui_NewLine();
			}
		}
		eximgui_end_input_box();
		ImGui_Text("Distance from surface: %.3f %s m = %.3fly", d_scaled, si_prefix_english(d_prefix), d / 9.4607e+15);
		ImGui_Text("Yaw: %.3frad, Pitch: %.3frad", game->data_own->camera_yaw, game->data_own->camera_pitch);
		if (ImGui_Button("Reset rotation")) {
			game->data_own->camera_yaw = 0.0;
			game->data_own->camera_pitch = 0.0;
		}
		ImGui_SetItemTooltip("Set the rotation to (0.0, 0.0)");
		ImGui_SameLine();
		if (ImGui_Button("Center on target")) {
			double3 bp_wcs = double3vs(body->position.values);
			double3 bp_scs = double3mul(bp_wcs, PSHINE_SCS_FACTOR);
			game->data_own->camera_yaw = π / 2.0 + atan2(
				p_scs.z - bp_scs.z,
				p_scs.x - bp_scs.x
			);
			// TODO: this is a bit weird.
			game->data_own->camera_pitch = -atan2(
				p_scs.y - bp_scs.y,
				p_scs.x - bp_scs.x
			);
			// game->data_own->camera_pitch = 0.0;
		}
		ImGui_SetItemTooltip("Set the rotation so that the selected body is in the center");
		ImGui_SameLine();
		ImGui_BeginDisabled(true);
		if (ImGui_Button("Reset position")) {
			game->data_own->camera_yaw = 0.0;
			game->data_own->camera_pitch = 0.0;
		}
		ImGui_EndDisabled();
		if (eximgui_begin_input_box("Graphics", "Graphics settings.")) {
			ImGui_SliderFloat("FoV", &game->graphics_settings.camera_fov, 0.00001f, 179.999f);
			ImGui_SliderFloat("EV", &game->graphics_settings.exposure, -8.0f, 6.0f);

			if (eximgui_begin_input_box("Bloom", "Bloom effect settings. TBD.")) {
				ImGui_SliderFloat("Knee", &game->graphics_settings.bloom_knee, 0.0f, 16.0f);
				ImGui_SliderFloat("Threshold", &game->graphics_settings.bloom_threshold, game->graphics_settings.bloom_knee, 16.0f);
			}
			eximgui_end_input_box();
		}
		eximgui_end_input_box();
		if (eximgui_begin_input_box("Presets", "Preset times, positions and rotations with good views")) {
			if (ImGui_Button("KJ63 system view##Preset_0")) {
				game->time = 126000.0;
				game->current_star_system = 0;
				game->data_own->camera_yaw = 6.088;
				game->data_own->camera_pitch = -0.227;
				game->camera_position.xyz.x = -117766076861.631;
				game->camera_position.xyz.y = 159655341.412;
				game->camera_position.xyz.z = 69706944591.439;
			}
		}
		eximgui_end_input_box();

		// ImGui_Spacing();
		// ImVec2 begin = ImGui_GetCursorScreenPos();
		// ImVec2 size = ImGui_GetItemRectSize();
		// ImVec2 end = { begin.x + size.x, begin.y + size.y };
		// ImDrawList_AddRectFilledEx(ImDrawList *self, ImVec2 p_min, ImVec2 p_max, ImU32 col, float rounding, ImDrawFlags flags)
		// ImDrawList_AddRectFilledMultiColor(
		// 	ImGui_GetWindowDrawList(),
		// 	begin,
		// 	end,
		// 	0xFF565656,
		// 	0xFF565656,
		// 	0xFF232323,
		// 	0xFF232323
		// );
	}
	ImGui_End();

	if (ImGui_Begin("Ship", nullptr, 0)) {
		struct pshine_ship *ship = &game->ships.ptr[0];
		ImGui_Text("Max Velocity: %.1fm/s", ship->current_max_velocity);
		ImGui_Text("Velocity: %.1fm/s", ship->velocity);
		ImGui_Text("Warp: %s", ship->is_in_warp ? "Active" : ship->is_warp_safe ? "Safe" : "Unsafe");
	}
	ImGui_End();

	if (body->type == PSHINE_CELESTIAL_BODY_PLANET) {
		if (ImGui_Begin("Planet", nullptr, 0)) {
			struct pshine_planet *planet = (struct pshine_planet*)body;
			ImGui_BeginDisabled(!planet->has_atmosphere);
			if (eximgui_begin_input_box("Atmosphere", "The paramters of the planet's atmosphere. You may need to recalculate the LUT.")) {
				struct pshine_atmosphere_info *atmo = &planet->atmosphere;
				ImGui_SliderFloat3("Rayleigh Coefs.", atmo->rayleigh_coefs, 0.001f, 50.0f);
				ImGui_SetItemTooltip("The Rayleigh scattering coeffitients for Red, Green and Blue.");
				ImGui_SliderFloat("Rayleigh Falloff.", &atmo->rayleigh_falloff, 0.0001f, 100.0f);
				ImGui_SetItemTooltip("The falloff factor for Rayleigh scattering.");
				ImGui_SliderFloat("Mie Coef.", &atmo->mie_coef, 0.001f, 50.0f);
				ImGui_SetItemTooltip("The coefficient for Mie scattering.");
				ImGui_SliderFloat("Mie Ext. Coef.", &atmo->mie_ext_coef, 0.001f, 5.0f);
				ImGui_SetItemTooltip("The external light coefficient for Mie scattering.");
				ImGui_SliderFloat("Mie 'g' Coef.", &atmo->mie_g_coef, -0.9999f, 0.9999f);
				ImGui_SetItemTooltip("The 'g' coefficient for Mie scattering.");
				ImGui_SliderFloat("Mie Falloff.", &atmo->mie_falloff, 0.0001f, 100.0f);
				ImGui_SetItemTooltip("The falloff factor for Mie scattering.");
				ImGui_SliderFloat("Intensity", &atmo->intensity, 0.0f, 50.0f);
				ImGui_SetItemTooltip("The intensity of the atmosphere. Not really a physical property.");
			}
			eximgui_end_input_box();
			ImGui_EndDisabled();
			ImGui_BeginDisabled(!body->rings.has_rings);
			if (eximgui_begin_input_box("Rings", "Parameters of the planetary rings.")) {
				struct pshine_rings_info *rings = &body->rings;
				ImGui_SliderFloat("Shadow smoothing", &rings->shadow_smoothing, -1.0f, 1.0f);
				ImGui_SetItemTooltip("How \"smooth\" the transition to and from shadow is.");
			}
			eximgui_end_input_box();
			ImGui_EndDisabled();
		}
		ImGui_End();
	}

	if (ImGui_Begin("System", NULL, 0)) {
		// float3 p = float3_double3(double3vs(game->sun_direction_.values));
		// if (ImGui_SliderFloat3("Sun", p.vs, -1.0f, 1.0)) {
		// 	*(double3*)game->sun_direction_.values = double3_float3(p);
		// }
		ImGui_SliderFloat("Time scale", &game->time_scale, 0.0, 1000.0);
		struct time_format_params time_fmt = compute_time_format_params(game->time);
		ImGui_Text("Time: " TIME_FORMAT, TIME_FORMAT_ARGS(time_fmt));
	}
	ImGui_End();

	ImGui_PushStyleColor(ImGuiCol_WindowBg, 0x00000030);
	ImGui_PushStyleColor(ImGuiCol_Border, 0x00000000);
	ImGui_PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
	if (ImGui_Begin("Selected celestial body", nullptr,
		// ImGuiWindowFlags_NoTitleBar |
		// ImGuiWindowFlags_NoResize |
		// ImGuiWindowFlags_NoScrollbar |
		// ImGuiWindowFlags_NoCollapse |
		// ImGuiWindowFlags_NoDocking |
		// ImGuiWindowFlags_AlwaysAutoResize |
		0
	)) {
		eximgui_title_text("%s", body->name_own);
		ImGui_TextWrapped("%s", body->desc_own != nullptr ? body->desc_own : "?");
		if (body->type == PSHINE_CELESTIAL_BODY_PLANET) {
			struct pshine_planet *planet = (void*)body;
			if (planet->has_atmosphere) {
				eximgui_bold_text("Atmospheric composition");
				if (ImGui_BeginTable("_AtmosphericCompositionTable", 2,
					ImGuiTableFlags_Borders |
					ImGuiTableFlags_RowBg
				)) {
					// ImGui_TableSetColumnIndex(0);
					ImGui_TableSetupColumn("Fraction", 0);
					// ImGui_TableSetColumnIndex(1);
					ImGui_TableSetupColumn("Element", 0);
					ImGui_TableHeadersRow();
					for (size_t i = 0; i < planet->atmosphere.composition.constituent_count; ++i) {
						struct pshine_named_constituent c = planet->atmosphere.composition.constituents_own[i];
						ImGui_TableNextRow();
						ImGui_TableSetColumnIndex(0);
						ImGui_Text("%g%%", c.fraction * 100.0f);
						ImGui_TableSetColumnIndex(1);
						ImGui_Text("%s", c.name_own);
					}
					ImGui_EndTable();
				}
			}
		}
		eximgui_input_double3("##WCS Position", "World coordinate system body position. In meters.",
			body->position.values, 1000.0, "%.3fm");
		// if (eximgui_input_double3("##SCS Position", "Scaled coordinate system camera position. 1:8192.",
		// 	p_scs.vs, 100.0, "%.3fu")) {
		// 	*(double3*)&game->camera_position.values = double3mul(p_scs, PSHINE_SCS_SCALE);
		// }
	}
	ImGui_End();
	ImGui_PopStyleColor();
	ImGui_PopStyleColor();
	ImGui_PopStyleColor();

	if (ImGui_Begin("Blackbody Radiation Test", nullptr, 0)) {
		static float temp = 4200.0;
		ImGui_SliderFloat("Temp, K", &temp, 1200, 8000);
		pshine_color_rgb rgb = pshine_blackbody_temp_to_rgb(temp);
		ImGui_ColorEdit3("Color", rgb.values, ImGuiColorEditFlags_NoInputs
			| ImGuiColorEditFlags_NoPicker);
	}
	ImGui_End();

	science_imgui(game, actual_delta_time);

	eximgui_end_frame();
}
