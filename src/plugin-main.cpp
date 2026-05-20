/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <obs-module.h>
#include "asio-plugin.h"
#include "asio-manager.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("alir-io", "en-US")

bool obs_module_load(void)
{
	register_asio_input();
	register_asio_output();
	return true;
}

void obs_module_unload(void)
{
	ASIOManager::getInstance().shutdown();
}
