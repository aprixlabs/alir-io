/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <util/circlebuf.h>
#include <obs.h>
#include "asio.h"

#define MAX_ASIO_CHANNELS 64

// Interface for ASIO Input Plugins
class ASIOClient {
public:
	virtual void pushAudio(const std::vector<std::vector<float>>& buffers, uint64_t timestamp, uint32_t sample_rate,
			size_t frames) = 0;
	virtual ~ASIOClient()          = default;
};

class ASIOManager {
public:
	static ASIOManager& getInstance();

	// Driver control
	void ensureDriverLoaded(const std::string& driverName);
	void releaseDriver(const std::string& driverName);
	void forceReset();

	// UI Helpers
	void                     fillDeviceList(obs_property_t* prop);
	std::vector<std::string> getInputChannels(const std::string& driverName);
	std::vector<std::string> getOutputChannels(const std::string& driverName);
	void                     openControlPanel();

	// Client Registration
	void addClient(ASIOClient* client);
	void removeClient(ASIOClient* client);

	// Audio Output Buffer Push
	void pushOutputAudio(
			const std::vector<int>& asioChannels, const std::vector<const float*>& data, size_t frames);

	// Check state
	bool        isPlaying() const;
	std::string getCurrentDriverName() const;
	long        getOutputChannelsCount() const;

	long getInputLatency() const;
	long getOutputLatency() const;
	long getSampleRate() const;

	// Called by Module Unload
	void shutdown();

private:
	ASIOManager();
	~ASIOManager();

	// Internal ASIO callbacks
	static void      bufferSwitch(long index, ASIOBool processNow);
	static ASIOTime* bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow);
	static void      sampleRateChanged(ASIOSampleRate sRate);
	static long      asioMessages(long selector, long value, void* message, double* opt);

	// State variables
	bool              isOpen              = false;
	bool              isPlayingState      = false;
	bool              supportsOutputReady = false;
	std::string       currentDriverName;
	int               driverRefCounter = 0;
	std::atomic<bool> shuttingDown{false};

	ASIODriverInfo driverInfo;
	long           inputChannels  = 0;
	long           outputChannels = 0;
	long           preferredSize  = 0;
	ASIOSampleRate sampleRate     = 0;

	ASIOBufferInfo  bufferInfos[MAX_ASIO_CHANNELS * 2];
	ASIOChannelInfo channelInfos[MAX_ASIO_CHANNELS * 2];

	std::vector<std::vector<float>> floatInputBuffers;
	std::vector<std::vector<float>> floatOutputBuffers;

	std::mutex               clientsMutex;
	std::vector<ASIOClient*> clients;

	std::mutex outMutex;
	circlebuf  outBuffers[MAX_ASIO_CHANNELS];
};
