/*
OBS Squawk Plugin
Copyright (C) 2024 Roy Shilkrot roy.shil@gmail.com
Copyright (C) 2026 croc-pro-dev

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>

*/

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MT_ obs_module_text

const char *squawk_source_name(void *unused);
void *squawk_source_create(obs_data_t *settings, obs_source_t *source);
void squawk_source_destroy(void *data);
void squawk_source_defaults(obs_data_t *settings);
obs_properties_t *squawk_source_properties(void *data);
void squawk_source_update(void *data, obs_data_t *settings);
void squawk_source_activate(void *data);
void squawk_source_deactivate(void *data);
void squawk_source_show(void *data);
void squawk_source_hide(void *data);

const char *const PLUGIN_INFO_TEMPLATE =
	"<a href=\"https://github.com/occ-ai/obs-squawk/\">Squawk</a> (%s) C5 by "
	"<a href=\"https://github.com/occ-ai\">OCC AI</a> ❤️ "
	"<a href=\"https://www.patreon.com/RoyShilkrot\">Support & Follow</a>";

#ifdef __cplusplus
}
#endif
