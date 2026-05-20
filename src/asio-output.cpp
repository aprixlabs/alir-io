/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <obs-module.h>
#include "asio-manager.h"
#include "asio-plugin.h"

static std::vector<speaker_layout> known_layouts = {
		SPEAKERS_MONO,
		SPEAKERS_STEREO,
		SPEAKERS_2POINT1,
		SPEAKERS_4POINT0,
		SPEAKERS_4POINT1,
		SPEAKERS_5POINT1,
		SPEAKERS_7POINT1,
};
static std::vector<std::string> known_layouts_keys = {
		"Layout.Mono",
		"Layout.Stereo",
		"Layout.2Point1",
		"Layout.4Point0",
		"Layout.4Point1",
		"Layout.5Point1",
		"Layout.7Point1",
};

static int get_max_obs_channels()
{
	static int channels = 0;
	if (channels > 0)
		return channels;
	for (int i = 0; i < 1024; i++) {
		int c = get_audio_channels((speaker_layout)i);
		if (c > channels)
			channels = c;
	}
	return channels;
}

class ASIOOutputFilter {
private:
	obs_source_t      *source;
	std::vector<short> _route;
	speaker_layout     _outLayout = SPEAKERS_STEREO;

public:
	ASIOOutputFilter(obs_data_t *settings, obs_source_t *source) : source(source)
	{
		update(settings);
	}

	~ASIOOutputFilter()
	{
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		return new ASIOOutputFilter(settings, source);
	}

	static void Destroy(void *vptr)
	{
		delete static_cast<ASIOOutputFilter *>(vptr);
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		static_cast<ASIOOutputFilter *>(vptr)->update(settings);
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("FilterName");
	}

	void update(obs_data_t *settings)
	{
		_outLayout = (speaker_layout)obs_data_get_int(settings, "out_speaker_layout");

		_route.clear();
		for (int i = 0; i < get_max_obs_channels(); i++) {
			std::string route_str = "out_route_" + std::to_string(i);
			_route.push_back((short)obs_data_get_int(settings, route_str.c_str()));
		}
	}

	struct obs_audio_data *filter_audio(struct obs_audio_data *audio)
	{
		if (!ASIOManager::getInstance().isPlaying())
			return audio;

		int  out_channels = get_audio_channels(_outLayout);
		long outCount     = ASIOManager::getInstance().getOutputChannelsCount();

		std::vector<int>           asioChannels;
		std::vector<const float *> channelData;

		for (int i = 0; i < out_channels; i++) {
			if (i >= (int)_route.size() || audio->data[i] == nullptr)
				break;
			short route = _route[i];

			if (route >= 0 && route < outCount) {
				asioChannels.push_back(route);
				channelData.push_back((const float *)audio->data[i]);
			}
		}

		if (!asioChannels.empty()) {
			ASIOManager::getInstance().pushOutputAudio(asioChannels, channelData, audio->frames);
		}

		return audio;
	}

	static struct obs_audio_data *FilterAudio(void *data, struct obs_audio_data *audio)
	{
		return static_cast<ASIOOutputFilter *>(data)->filter_audio(audio);
	}

	static bool fill_asio_out_channels(obs_property_t *list, speaker_layout layout)
	{
		obs_property_list_clear(list);
		obs_property_list_add_int(list, obs_module_text("Mute"), -1);

		std::string currentDriver = ASIOManager::getInstance().getCurrentDriverName();
		auto        channels      = ASIOManager::getInstance().getOutputChannels(currentDriver);

		if (channels.empty())
			return true;

		for (size_t i = 0; i < channels.size(); i++)
			obs_property_list_add_int(list, channels[i].c_str(), (int)i);

		return true;
	}

	static bool refresh_route_visibility(obs_properties_t *props, obs_data_t *settings)
	{
		speaker_layout layout    = (speaker_layout)obs_data_get_int(settings, "out_speaker_layout");
		int            active_ch = get_audio_channels(layout);
		int            max_ch    = get_max_obs_channels();

		obs_property_t *status = obs_properties_get(props, "driver_status");
		if (status) {
			std::string active = ASIOManager::getInstance().getCurrentDriverName();
			std::string label =
					active.empty() ? obs_module_text("Status.NoDriver") : get_hw_status_string();
			obs_property_set_description(status, label.c_str());
		}

		for (int i = 0; i < max_ch; i++) {
			std::string     prop_id = "out_route_" + std::to_string(i);
			obs_property_t *r       = obs_properties_get(props, prop_id.c_str());
			if (!r)
				continue;

			if (i < active_ch) {
				obs_property_set_visible(r, true);
				std::string label = get_spatial_channel_name(layout, i, true);
				obs_property_set_description(r, label.c_str());
				fill_asio_out_channels(r, layout);
			} else {
				obs_property_set_visible(r, false);
			}
		}
		return true;
	}

	static bool asio_out_layout_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
	{
		UNUSED_PARAMETER(list);
		return refresh_route_visibility(props, settings);
	}

	static obs_properties_t *Properties(void *vptr)
	{
		UNUSED_PARAMETER(vptr);
		obs_properties_t *props = obs_properties_create();

		{
			std::string active = ASIOManager::getInstance().getCurrentDriverName();
			std::string label =
					active.empty() ? obs_module_text("Status.NoDriver") : get_hw_status_string();
			obs_property_t *st =
					obs_properties_add_text(props, "driver_status", label.c_str(), OBS_TEXT_INFO);
			UNUSED_PARAMETER(st);
		}

		obs_property_t *layout = obs_properties_add_list(props, "out_speaker_layout",
				obs_module_text("Channels"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t i = 0; i < known_layouts.size(); i++)
			obs_property_list_add_int(
					layout, obs_module_text(known_layouts_keys[i].c_str()), known_layouts[i]);
		obs_property_set_modified_callback(layout, asio_out_layout_changed);

		int max_ch = get_max_obs_channels();
		for (int i = 0; i < max_ch; i++) {
			std::string prop_id = "out_route_" + std::to_string(i);
			std::string label   = "Out " + std::to_string(i + 1);
			obs_properties_add_list(props, prop_id.c_str(), label.c_str(), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);
		}

		obs_properties_add_button(props, "about", obs_module_text("About"), show_asio_about);
		return props;
	}

	static void Defaults(obs_data_t *settings)
	{
		obs_data_set_default_int(settings, "out_speaker_layout", SPEAKERS_STEREO);
		for (int i = 0; i < get_max_obs_channels(); i++) {
			std::string name = "out_route_" + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}
	}
};

void register_asio_output()
{
	struct obs_source_info asio_output_filter_info = {};
	asio_output_filter_info.id                     = "asio_output_filter";
	asio_output_filter_info.type                   = OBS_SOURCE_TYPE_FILTER;
	asio_output_filter_info.output_flags           = OBS_SOURCE_AUDIO;
	asio_output_filter_info.create                 = ASIOOutputFilter::Create;
	asio_output_filter_info.destroy                = ASIOOutputFilter::Destroy;
	asio_output_filter_info.update                 = ASIOOutputFilter::Update;
	asio_output_filter_info.get_defaults           = ASIOOutputFilter::Defaults;
	asio_output_filter_info.get_name               = ASIOOutputFilter::Name;
	asio_output_filter_info.get_properties         = ASIOOutputFilter::Properties;
	asio_output_filter_info.filter_audio           = ASIOOutputFilter::FilterAudio;

	obs_register_source(&asio_output_filter_info);
}
