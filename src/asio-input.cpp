/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QString>
#include <QResource>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

#include "asio-manager.h"
#include "asio-plugin.h"

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

bool show_asio_about(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	UNUSED_PARAMETER(data);
	Q_INIT_RESOURCE(asio_input);
	QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
	QDialog      dialog(main_window);
	dialog.setWindowTitle("About alir.io");
	dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	layout->setContentsMargins(25, 20, 25, 20);
	layout->setSpacing(12);

	auto addCenteredText = [&](const QString &html) {
		QLabel *l = new QLabel(&dialog);
		l->setText("<div style='text-align:center;'>" + html + "</div>");
		l->setTextFormat(Qt::RichText);
		l->setTextInteractionFlags(Qt::TextBrowserInteraction);
		l->setOpenExternalLinks(true);
		l->setAlignment(Qt::AlignCenter);
		layout->addWidget(l);
	};

	auto addSeparator = [&]() {
		QFrame *line = new QFrame(&dialog);
		line->setFrameShape(QFrame::HLine);
		line->setFrameShadow(QFrame::Sunken);
		layout->addWidget(line);
	};

	addCenteredText("<a href='https://github.com/aprixlabs/alir-io' style='text-decoration:none;'>"
			"<img src=':/res/images/alir-io.png'></a><br>"
			"<span style='font-size:9pt; color:#888888;'>Version 1.0.1</span><br>"
			"<span style='font-size:10pt;'>ASIO\u00AE Integration for OBS Studio</span><br><br>"
			"<span style='font-size:10pt;'>\u00A9 2026 "
			"<a href='https://github.com/aprixlabs' style='text-decoration: none; color: white; "
			"font-weight: bold;'>"
			"Aprix Labs</a></span>");

	addSeparator();

	addCenteredText("<a href='https://www.steinberg.net/developers/asiosdk-open/'>"
			"<img src=':/res/images/asiologo.png'></a><br>"
			"<span style='font-size:10pt; font-style:italic;'>ASIO is a registered trademark of Steinberg "
			"Media "
			"Technologies GmbH.</span>");

	addSeparator();

	addCenteredText("<a href='https://ko-fi.com/aprixlabs'><img src=':/res/images/supportme.png'></a></span><br>"
			"<span style='font-size:10pt;'>Support alir.io future &amp; development with a "
			"contribution.</span>");

	QPushButton *btn = new QPushButton("Close", &dialog);
	btn->setFixedWidth(100);
	QObject::connect(btn, &QPushButton::clicked, &dialog, &QDialog::accept);

	QHBoxLayout *btnLayout = new QHBoxLayout();
	btnLayout->addStretch();
	btnLayout->addWidget(btn);
	btnLayout->addStretch();

	layout->addLayout(btnLayout);

	dialog.exec();
	return true;
}

class ASIOPlugin : public ASIOClient {
private:
	std::vector<short> _route;
	speaker_layout     _speakers;
	obs_source_t      *source;
	std::string        targetDriverName;

public:
	ASIOPlugin(obs_data_t *settings, obs_source_t *source) : source(source)
	{
		UNUSED_PARAMETER(settings);
		ASIOManager::getInstance().addClient(this);
	}

	~ASIOPlugin() override
	{
		ASIOManager::getInstance().removeClient(this);
		if (!targetDriverName.empty())
			ASIOManager::getInstance().releaseDriver(targetDriverName);
	}

	static void *Create(obs_data_t *settings, obs_source_t *source)
	{
		ASIOPlugin *plugin = new ASIOPlugin(settings, source);
		plugin->update(settings);
		return plugin;
	}

	static void Destroy(void *vptr)
	{
		delete static_cast<ASIOPlugin *>(vptr);
	}

	static bool open_control_panel(obs_properties_t *props, obs_property_t *property, void *data)
	{
		UNUSED_PARAMETER(props);
		UNUSED_PARAMETER(property);
		ASIOPlugin *self = static_cast<ASIOPlugin *>(data);
		if (self && self->targetDriverName == ASIOManager::getInstance().getCurrentDriverName())
			ASIOManager::getInstance().openControlPanel();
		return true;
	}

	static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
	{
		UNUSED_PARAMETER(props);
		std::string name = obs_data_get_string(settings, "device_id");

		obs_property_list_clear(list);
		obs_property_list_add_int(list, obs_module_text("None"), -1);

		auto channels = ASIOManager::getInstance().getInputChannels(name);
		for (size_t i = 0; i < channels.size(); i++) {
			obs_property_list_add_int(list, channels[i].c_str(), i);
		}
		return true;
	}

	static bool asio_device_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
	{
		int            max_channels      = get_max_obs_channels();
		speaker_layout layout            = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
		int            recorded_channels = get_audio_channels(layout);

		for (int i = 0; i < max_channels; i++) {
			std::string     name = "route " + std::to_string(i);
			obs_property_t *r    = obs_properties_get(props, name.c_str());

			std::string spatial_name = get_spatial_channel_name(layout, i, false);
			obs_property_set_description(r, spatial_name.c_str());

			obs_property_set_modified_callback(r, fill_out_channels_modified);
			obs_property_set_visible(r, i < recorded_channels);
			fill_out_channels_modified(props, r, settings);
		}

		const char *dev_id      = obs_data_get_string(settings, "device_id");
		std::string savedDriver = dev_id ? dev_id : "";

		obs_property_t *status = obs_properties_get(props, "hw_status");
		if (status)
			obs_property_set_description(status, get_hw_status_string(savedDriver, true).c_str());

		return true;
	}

	static bool asio_layout_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
	{
		UNUSED_PARAMETER(list);
		return asio_device_changed(nullptr, props, list, settings);
	}

	static obs_properties_t *Properties(void *vptr)
	{
		UNUSED_PARAMETER(vptr);
		obs_properties_t *props = obs_properties_create();

		obs_property_t *devices = obs_properties_add_list(props, "device_id", obs_module_text("AsioDriver"),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(devices, asio_device_changed, vptr);
		ASIOManager::getInstance().fillDeviceList(devices);

		obs_properties_add_button(props, "control_panel", obs_module_text("ControlPanel"), open_control_panel);

		{
			ASIOPlugin     *self        = static_cast<ASIOPlugin *>(vptr);
			std::string     savedDriver = self ? self->targetDriverName : "";
			std::string     initStatus  = get_hw_status_string(savedDriver, true);
			obs_property_t *hw =
					obs_properties_add_text(props, "hw_status", initStatus.c_str(), OBS_TEXT_INFO);
			UNUSED_PARAMETER(hw);
		}

		obs_property_t *format = obs_properties_add_list(props, "speaker_layout", obs_module_text("Channels"),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t i = 0; i < known_layouts.size(); i++)
			obs_property_list_add_int(
					format, obs_module_text(known_layouts_keys[i].c_str()), known_layouts[i]);
		obs_property_set_modified_callback(format, asio_layout_changed);

		int max_channels = get_max_obs_channels();
		for (int i = 0; i < max_channels; i++) {
			std::string label = "In " + std::to_string(i + 1);
			obs_properties_add_list(props, ("route " + std::to_string(i)).c_str(), label.c_str(),
					OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		}

		obs_properties_add_button(props, "about", obs_module_text("About"), show_asio_about);
		return props;
	}

	void update(obs_data_t *settings)
	{
		const char *dev_id    = obs_data_get_string(settings, "device_id");
		std::string newDriver = dev_id ? dev_id : "";

		if (newDriver != targetDriverName) {
			if (!targetDriverName.empty()) {
				ASIOManager::getInstance().releaseDriver(targetDriverName);
			}
			targetDriverName = newDriver;
		}

		_speakers             = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
		int recorded_channels = get_audio_channels(_speakers);

		_route.clear();
		for (int i = 0; i < recorded_channels; i++) {
			std::string route_str = "route " + std::to_string(i);
			_route.push_back((short)obs_data_get_int(settings, route_str.c_str()));
		}

		if (!targetDriverName.empty()) {
			ASIOManager::getInstance().ensureDriverLoaded(targetDriverName);
		}
	}

	void pushAudio(const std::vector<std::vector<float>> &buffers, uint64_t timestamp, uint32_t sample_rate,
			size_t frames) override
	{
		if (targetDriverName != ASIOManager::getInstance().getCurrentDriverName() ||
				!ASIOManager::getInstance().isPlaying())
			return;

		obs_source_audio out = {};
		out.speakers         = _speakers;
		out.samples_per_sec  = sample_rate;
		out.format           = AUDIO_FORMAT_FLOAT_PLANAR;
		out.timestamp        = timestamp;
		out.frames           = (uint32_t)frames;

		int                ochs = get_audio_channels(out.speakers);
		std::vector<float> silent(frames, 0.0f);

		for (int i = 0; i < ochs; i++) {
			if (i < _route.size() && _route[i] >= 0 && _route[i] < buffers.size()) {
				out.data[i] = (uint8_t *)buffers[_route[i]].data();
			} else {
				out.data[i] = (uint8_t *)silent.data();
			}
		}

		obs_source_output_audio(source, &out);
	}

	static void Update(void *vptr, obs_data_t *settings)
	{
		static_cast<ASIOPlugin *>(vptr)->update(settings);
	}

	static void Defaults(obs_data_t *settings)
	{
		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);
		int max_channels = get_max_obs_channels();
		for (int i = 0; i < max_channels; i++) {
			std::string name = "route " + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}
		obs_data_set_default_int(settings, "speaker_layout", aoi.speakers);
	}

	static const char *Name(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("SourceName");
	}
};

void register_asio_input()
{
	struct obs_source_info asio_input = {};
	asio_input.id                     = "asio_input";
	asio_input.type                   = OBS_SOURCE_TYPE_INPUT;
	asio_input.output_flags           = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	asio_input.create                 = ASIOPlugin::Create;
	asio_input.destroy                = ASIOPlugin::Destroy;
	asio_input.update                 = ASIOPlugin::Update;
	asio_input.get_defaults           = ASIOPlugin::Defaults;
	asio_input.get_name               = ASIOPlugin::Name;
	asio_input.get_properties         = ASIOPlugin::Properties;
	asio_input.icon_type              = OBS_ICON_TYPE_AUDIO_INPUT;

	obs_register_source(&asio_input);
}
