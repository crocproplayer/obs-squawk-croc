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
#include "plugin-support.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info squawk_source_info;

MODULE_EXPORT bool obs_module_load(void)
{
	obs_log(LOG_INFO, "====== SQUAWK: obs_module_load INITIATED ======");
	
	obs_register_source(&squawk_source_info);
	
	obs_log(LOG_INFO, "====== SQUAWK: obs_register_source SUCCESS ======");
	return true;
}

MODULE_EXPORT void obs_module_unload(void)
{
	obs_log(LOG_INFO, "====== SQUAWK: obs_module_unload INITIATED ======");
}