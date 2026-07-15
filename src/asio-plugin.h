/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <obs-module.h>
#include <obs-properties.h>
#include <string>

inline std::string get_spatial_channel_name(speaker_layout layout, int index, bool is_output)
{
	if (layout == SPEAKERS_MONO && index == 0)
		return is_output ? obs_module_text("Channel.Mono.Out") : obs_module_text("Channel.Mono.In");
	if (layout == SPEAKERS_STEREO) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
	}
	if (layout == SPEAKERS_2POINT1) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.Lfe");
	}
	if (layout == SPEAKERS_4POINT0) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.LeftSurround");
		if (index == 3)
			return obs_module_text("Channel.RightSurround");
	}
	if (layout == SPEAKERS_4POINT1) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.Lfe");
		if (index == 3)
			return obs_module_text("Channel.LeftSurround");
		if (index == 4)
			return obs_module_text("Channel.RightSurround");
	}
#ifdef SPEAKERS_5POINT0
	if (layout == SPEAKERS_5POINT0) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.Center");
		if (index == 3)
			return obs_module_text("Channel.LeftSurround");
		if (index == 4)
			return obs_module_text("Channel.RightSurround");
	}
#endif
	if (layout == SPEAKERS_5POINT1) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.Center");
		if (index == 3)
			return obs_module_text("Channel.Lfe");
		if (index == 4)
			return obs_module_text("Channel.LeftSurround");
		if (index == 5)
			return obs_module_text("Channel.RightSurround");
	}
	if (layout == SPEAKERS_7POINT1) {
		if (index == 0)
			return obs_module_text("Channel.Left");
		if (index == 1)
			return obs_module_text("Channel.Right");
		if (index == 2)
			return obs_module_text("Channel.Center");
		if (index == 3)
			return obs_module_text("Channel.Lfe");
		if (index == 4)
			return obs_module_text("Channel.LeftSurround");
		if (index == 5)
			return obs_module_text("Channel.RightSurround");
		if (index == 6)
			return obs_module_text("Channel.SideLeft");
		if (index == 7)
			return obs_module_text("Channel.SideRight");
	}
	return std::string(is_output ? obs_module_text("Channel.Out") : obs_module_text("Channel.In"))
	       + " " + std::to_string(index + 1);
}

#include "asio-manager.h"
inline std::string get_hw_status_string(const std::string &savedDriver = "", bool requireDriverMatch = false)
{
	ASIOManager &mgr     = ASIOManager::getInstance();
	std::string  active  = mgr.getCurrentDriverName();
	bool         playing = mgr.isPlaying();

	if (requireDriverMatch && savedDriver.empty())
		return obs_module_text("Status.NoDriver");

	bool show;
	if (requireDriverMatch) {
		show = playing && !savedDriver.empty() && (savedDriver == active);
	} else {
		show = playing && !active.empty();
	}

	if (!show)
		return obs_module_text("Status.NotRunning");

	long  inLat  = mgr.getInputLatency();
	long  outLat = mgr.getOutputLatency();
	long  sr     = mgr.getSampleRate();

	if (inLat == 0 && outLat == 0)
		return obs_module_text("Status.NotRunning");

	float inMs  = (sr > 0) ? ((float)inLat / sr) * 1000.0f : 0.0f;
	float outMs = (sr > 0) ? ((float)outLat / sr) * 1000.0f : 0.0f;

	char buf[512];
	snprintf(buf, sizeof(buf),
			"ASIO Driver: %s\nSample Rate: %ld Hz\n"
			"Input Latency: %ld spls (%.1f ms)\n"
			"Output Latency: %ld spls (%.1f ms)",
			active.c_str(), sr, inLat, inMs, outLat, outMs);
	return buf;
}

void register_asio_input();
void register_asio_output();
bool show_asio_about(obs_properties_t *props, obs_property_t *property, void *data);
