#include "game.h"
#include <toml.h>
#include <errno.h>

static struct pshine_celestial_body *load_celestial_body(
	const char *fpath
) {
	PSHINE_DEBUG("loading celestial body from '%s'", fpath);
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open celestial body file %s: %s", fpath,
			strerror(errno));
		return nullptr;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse celestial body configuration:\n%s", errbuf);
		free(errbuf);
		return nullptr;
	}
	struct pshine_celestial_body *body = nullptr;
	toml_table_t *ptab = nullptr;

	toml_datum_t dat;
#define READ_FIELD(tab, NAME, FIELD_NAME, TYPE, DAT_FIELD) \
	dat = toml_##TYPE##_in(tab, NAME); if (dat.ok) FIELD_NAME = dat.u.DAT_FIELD
#define READ_FIELD_AT(arr, IDX, FIELD_NAME, TYPE, DAT_FIELD) \
	dat = toml_##TYPE##_at(arr, IDX); if (dat.ok) FIELD_NAME = dat.u.DAT_FIELD

	// no need for pshine_strdup, as dat.u.s is allocated by tomlc99.
#define READ_STR_FIELD(tab, NAME, FIELD_NAME) \
	dat = toml_string_in(tab, NAME); if (dat.ok) FIELD_NAME = dat.u.s

	if ((ptab = toml_table_in(tab, "planet")) != nullptr) {
		body = calloc(1, sizeof(struct pshine_planet));
		body->type = PSHINE_CELESTIAL_BODY_PLANET;
		struct pshine_planet *planet = (void*)body;
		toml_table_t *atab = toml_table_in(tab, "atmosphere");
		planet->has_atmosphere = false;
		if (atab != nullptr) {
			planet->has_atmosphere = true;
			READ_FIELD(atab, "height", planet->atmosphere.height, double, d);
			toml_array_t *arr = toml_array_in(atab, "rayleigh_coefs");
			if (arr != nullptr) {
				if (toml_array_nelem(arr) != 3) {
					PSHINE_WARN("Invalid celestial body configuration: atmosphere.rayleigh_coefs "
						"must be a 3 element array.");
				} else {
					READ_FIELD_AT(arr, 0, planet->atmosphere.rayleigh_coefs[0], double, d);
					READ_FIELD_AT(arr, 1, planet->atmosphere.rayleigh_coefs[1], double, d);
					READ_FIELD_AT(arr, 2, planet->atmosphere.rayleigh_coefs[2], double, d);
				}
			}
			struct pshine_elemental_composition *composition = &planet->atmosphere.composition;
			composition->constituent_count = 0;
			arr = toml_array_in(atab, "composition");
			if (arr != nullptr) {
				size_t n = toml_array_nelem(arr);
				if (n % 2 != 0) {
					PSHINE_WARN("Invalid celestial body configuration: the length of atmosphere.composition "
						"must be a multiple of two.");
					n -= 1;
				}
				if (n > 0) {
					composition->constituent_count = n / 2;
					// TODO: free
					composition->constituents_own = calloc(composition->constituent_count,
						sizeof(*composition->constituents_own));
					for (size_t i = 0; i < n; i += 2) {
						READ_FIELD_AT(arr, i + 0, composition->constituents_own[i / 2].fraction, double, d);
						READ_FIELD_AT(arr, i + 1, composition->constituents_own[i / 2].name_own, string, s);
					}
				}
			}
			READ_FIELD(atab, "rayleigh_falloff", planet->atmosphere.rayleigh_falloff, double, d);
			READ_FIELD(atab, "mie_coef", planet->atmosphere.mie_coef, double, d);
			READ_FIELD(atab, "mie_ext_coef", planet->atmosphere.mie_ext_coef, double, d);
			READ_FIELD(atab, "mie_g_coef", planet->atmosphere.mie_g_coef, double, d);
			READ_FIELD(atab, "mie_falloff", planet->atmosphere.mie_falloff, double, d);
			READ_FIELD(atab, "intensity", planet->atmosphere.intensity, double, d);
		}
	} else if ((ptab = toml_table_in(tab, "star")) != nullptr) {
		body = calloc(1, sizeof(struct pshine_star));
		body->type = PSHINE_CELESTIAL_BODY_STAR;
		struct pshine_star *star = (void*)body;
		READ_FIELD(ptab, "surface_temperature", star->temperature, double, d);
	} else {
		PSHINE_ERROR("Invalid celestial body configuration: no [planet] or [star] tables.");
		free(errbuf);
		return nullptr;
	}
	PSHINE_CHECK(ptab != nullptr, "what the");

	READ_STR_FIELD(ptab, "name", body->name_own);
	READ_STR_FIELD(ptab, "description", body->desc_own);

	// Remove newlines
	if (body->desc_own != nullptr) {
		for (char *found = body->desc_own; (found = strchr(found, '\n')) != nullptr;) {
			*(found++) = ' ';
		}
	}

	READ_STR_FIELD(ptab, "parent", body->tmp_parent_ref_name_own);

	READ_FIELD(ptab, "radius", body->radius, double, d);
	READ_FIELD(ptab, "mass", body->mass, double, d);
	READ_FIELD(ptab, "mu", body->gravitational_parameter, double, d);
	READ_FIELD(ptab, "average_color", body->average_color, int, i);
	READ_FIELD(ptab, "gizmo_color", body->gizmo_color, int, i);
	READ_FIELD(ptab, "is_static", body->is_static, bool, b);

	toml_table_t *otab = toml_table_in(tab, "orbit");
	if (otab != nullptr) {
		READ_FIELD(otab, "argument", body->orbit.argument, double, d);
		READ_FIELD(otab, "eccentricity", body->orbit.eccentricity, double, d);
		READ_FIELD(otab, "inclination", body->orbit.inclination, double, d);
		READ_FIELD(otab, "longitude", body->orbit.longitude, double, d);
		READ_FIELD(otab, "semimajor", body->orbit.semimajor, double, d);
		READ_FIELD(otab, "true_anomaly", body->orbit.true_anomaly, double, d);
	}

	toml_table_t *stab = toml_table_in(tab, "surface");
	if (stab != nullptr) {
		READ_STR_FIELD(stab, "albedo_texture_path", body->surface.albedo_texture_path_own);
		READ_STR_FIELD(stab, "spec_texture_path", body->surface.spec_texture_path_own);
		READ_STR_FIELD(stab, "lights_texture_path", body->surface.lights_texture_path_own);
		READ_STR_FIELD(stab, "bump_texture_path", body->surface.bump_texture_path_own);
	}

	body->rings.has_rings = false;
	toml_table_t *gtab = toml_table_in(tab, "rings");
	if (gtab != nullptr) {
		body->rings.has_rings = true;
		READ_FIELD(gtab, "inner_radius", body->rings.inner_radius, double, d);
		READ_FIELD(gtab, "outer_radius", body->rings.outer_radius, double, d);
		READ_FIELD(gtab, "shadow_smoothing", body->rings.shadow_smoothing, double, d);
		READ_STR_FIELD(gtab, "slice_texture_path", body->rings.slice_texture_path_own);
		if (body->rings.slice_texture_path_own == nullptr) {
			body->rings.has_rings = false;
			PSHINE_ERROR("No ring texture path present, ignoring rings.");
		}
	}

	toml_table_t *rtab = toml_table_in(tab, "rotation");
	if (rtab != nullptr) {
		READ_FIELD(rtab, "speed", body->rotation_speed, double, d);
		toml_array_t *arr = toml_array_in(rtab, "axis");
		if (arr != nullptr) {
			if (toml_array_nelem(arr) != 3) {
				PSHINE_WARN("Invalid celestial body configuration: rotation.axis "
					"must be a 3 element array.");
			} else {
				READ_FIELD_AT(arr, 0, body->rotation_axis.xyz.x, double, d);
				READ_FIELD_AT(arr, 1, body->rotation_axis.xyz.y, double, d);
				READ_FIELD_AT(arr, 2, body->rotation_axis.xyz.z, double, d);
			}
		}
		READ_FIELD(rtab, "speed", body->rotation_speed, double, d);
		READ_STR_FIELD(rtab, "spec_texture_path", body->surface.spec_texture_path_own);
	}

#undef READ_FIELD
#undef READ_FIELD_AT
#undef READ_STR_FIELD

	toml_free(tab);

	free(errbuf);
	return body;
}

static void load_star_system_config(struct pshine_game *game, struct pshine_star_system *out, const char *fpath) {
	PSHINE_DEBUG("loading star system from '%s'", fpath);
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open star system config file '%s': %s", fpath,
			strerror(errno));
		return;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse star system config:\n%s", errbuf);
		free(errbuf);
		return;
	}
	toml_array_t *arr = toml_array_in(tab, "planets");
	out->body_count = 0;
	if (arr != nullptr) {
		out->body_count = toml_array_nelem(arr);
		out->bodies_own = calloc(out->body_count, sizeof(struct pshine_celestial_body *));
		for (size_t i = 0; i < out->body_count; ++i) {
			toml_datum_t body_fpath = toml_string_at(arr, i);
			if (!body_fpath.ok) {
				PSHINE_ERROR("star system config planets[%zu] isn't a string", i);
				continue;
			}
			out->bodies_own[i] = load_celestial_body(body_fpath.u.s);
			free(body_fpath.u.s);
		}
	}
	toml_datum_t name = toml_string_in(tab, "name");
	if (!name.ok) {
		PSHINE_ERROR("star system config name isn't a string");
		out->name_own = pshine_strdup("<config_error>");
	} else {
		out->name_own = name.u.s;
	}
}

void load_game_config(struct pshine_game *game, const char *fpath) {
	FILE *fin = fopen(fpath, "rb");
	if (fin == nullptr) {
		PSHINE_ERROR("Failed to open game config file '%s': %s", fpath,
			strerror(errno));
		return;
	}
	char *errbuf = calloc(1024, 1);
	toml_table_t *tab = toml_parse_file(fin, errbuf, 1024);
	fclose(fin);
	if (tab == nullptr) {
		PSHINE_ERROR("Failed to parse game config:\n%s", errbuf);
		free(errbuf);
		return;
	}
	toml_array_t *arr = toml_array_in(tab, "systems");
	game->star_system_count = 0;
	if (arr != nullptr) {
		game->star_system_count = toml_array_nelem(arr);
		game->star_systems_own = calloc(game->star_system_count, sizeof(*game->star_systems_own));
		for (size_t i = 0; i < game->star_system_count; ++i) {
			toml_datum_t body_fpath = toml_string_at(arr, i);
			if (!body_fpath.ok) {
				PSHINE_ERROR("game config systems[%zu] isn't a string", i);
				continue;
			}
			load_star_system_config(game, &game->star_systems_own[i], body_fpath.u.s);
			free(body_fpath.u.s);
		}
	}
	game->environment.type = PSHINE_ENVIRONMENT_PROJECTION_EQUIRECTANGULAR;
	game->environment.texture_path_own = nullptr;
	toml_table_t *etab = toml_table_in(tab, "environment");
	if (etab != nullptr) {
		struct toml_datum_t path = toml_string_in(etab, "texture_path");
		if (path.ok) game->environment.texture_path_own = path.u.s;
		struct toml_datum_t proj = toml_string_in(etab, "projection");
		if (proj.ok) {
			if (strcmp(proj.u.s, "equirectangular") == 0)
				game->environment.type = PSHINE_ENVIRONMENT_PROJECTION_EQUIRECTANGULAR;
			else
				PSHINE_ERROR("game config environment.projection: unknown value '%s', "
					"keeping default equirectangular.", proj.u.s);
			free(proj.u.s);
		}
	}
	free(errbuf);
}
