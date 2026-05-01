/*
 * obs-scene-organiser
 * Copyright (C) 2026 Awet
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <obs-module.h>
#include "plugin-support.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-scene-organiser", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Scene Organiser - organize OBS scenes into folders with colors";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Scene Organiser";
}

MODULE_EXPORT const char *obs_module_author(void)
{
	return "Awet";
}

extern void scene_organiser_register_dock(void);

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	scene_organiser_register_dock();
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}