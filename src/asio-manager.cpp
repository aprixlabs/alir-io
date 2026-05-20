/*
 * alir.io - ASIO integration for OBS Studio.
 * Copyright (c) 2026 Aprix Labs
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "asio-manager.h"
#include <util/bmem.h>
#include <util/platform.h>
#include <thread>
#include <chrono>
#include "asiodrivers.h"
#include "asiolist.h"

extern AsioDrivers*    asioDrivers;
bool                   loadAsioDriver(char* name);
static AsioDriverList* asioDriverList = nullptr;

static ASIOCallbacks g_asioCallbacks;

#define blog(level, msg, ...) blog(level, "asio-manager: " msg, ##__VA_ARGS__)

static inline int getBytesPerSample(ASIOSampleType type)
{
	switch (type) {
	case ASIOSTInt16LSB:
	case ASIOSTInt16MSB:
		return 2;
	case ASIOSTInt24LSB:
	case ASIOSTInt24MSB:
		return 3;
	case ASIOSTInt32LSB:
	case ASIOSTInt32MSB:
	case ASIOSTFloat32LSB:
	case ASIOSTFloat32MSB:
	case ASIOSTInt32LSB16:
	case ASIOSTInt32LSB18:
	case ASIOSTInt32LSB20:
	case ASIOSTInt32LSB24:
	case ASIOSTInt32MSB16:
	case ASIOSTInt32MSB18:
	case ASIOSTInt32MSB20:
	case ASIOSTInt32MSB24:
		return 4;
	case ASIOSTFloat64LSB:
	case ASIOSTFloat64MSB:
		return 8;
	default:
		return 4;
	}
}

static inline float convertAsioSampleToFloat(uint8_t* ptr, ASIOSampleType type)
{
	switch (type) {
	case ASIOSTInt16LSB:
		return (float)(*(int16_t*)ptr) / 32768.0f;
	case ASIOSTInt24LSB: {
		int32_t val = (ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
		if (val & 0x800000)
			val |= 0xFF000000;
		return (float)val / 8388608.0f;
	}
	case ASIOSTInt32LSB:
		return (float)(*(int32_t*)ptr) / 2147483648.0f;
	case ASIOSTFloat32LSB:
		return *(float*)ptr;
	case ASIOSTInt32LSB24: {
		int32_t val = (*(int32_t*)ptr) << 8;
		return (float)val / 2147483648.0f;
	}
	default:
		return 0.0f;
	}
}

static inline void convertFloatToAsioSample(float val, uint8_t* dst, ASIOSampleType type)
{
	if (val > 1.0f)
		val = 1.0f;
	else if (val < -1.0f)
		val = -1.0f;

	switch (type) {
	case ASIOSTInt16LSB:
		*(int16_t*)dst = (int16_t)(val * 32767.0f);
		break;
	case ASIOSTInt24LSB: {
		int32_t intval = (int32_t)(val * 8388607.0f);
		dst[0]         = intval & 0xFF;
		dst[1]         = (intval >> 8) & 0xFF;
		dst[2]         = (intval >> 16) & 0xFF;
		break;
	}
	case ASIOSTInt32LSB:
		*(int32_t*)dst = (int32_t)(val * 2147483647.0f);
		break;
	case ASIOSTFloat32LSB:
		*(float*)dst = val;
		break;
	case ASIOSTInt32LSB24: {
		int32_t intval = (int32_t)(val * 8388607.0f);
		*(int32_t*)dst = intval << 8;
		break;
	}
	default:
		memset(dst, 0, getBytesPerSample(type));
		break;
	}
}

ASIOManager& ASIOManager::getInstance()
{
	static ASIOManager instance;
	return instance;
}

ASIOManager::ASIOManager()
{
	CoInitialize(nullptr);
	if (!asioDriverList)
		asioDriverList = new AsioDriverList();
	for (int i = 0; i < MAX_ASIO_CHANNELS; i++)
		circlebuf_init(&outBuffers[i]);
}

ASIOManager::~ASIOManager()
{
	shutdown();
}

void ASIOManager::shutdown()
{
	if (isOpen) {
		if (isPlayingState)
			ASIOStop();
		ASIODisposeBuffers();
		ASIOExit();
		if (asioDrivers)
			asioDrivers->removeCurrentDriver();
		isOpen              = false;
		isPlayingState      = false;
		supportsOutputReady = false;
		driverRefCounter    = 0;
	}
	for (int i = 0; i < MAX_ASIO_CHANNELS; i++)
		circlebuf_free(&outBuffers[i]);
	if (asioDriverList) {
		delete asioDriverList;
		asioDriverList = nullptr;
	}
	CoUninitialize();
}

void ASIOManager::ensureDriverLoaded(const std::string& driverName)
{
	if (driverName.empty())
		return;
	if (isOpen && currentDriverName == driverName) {
		driverRefCounter++;
		return;
	}
	if (isOpen) {
		shuttingDown.store(true);
		if (isPlayingState)
			ASIOStop();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		ASIODisposeBuffers();
		ASIOExit();
		if (asioDrivers)
			asioDrivers->removeCurrentDriver();
		isOpen              = false;
		isPlayingState      = false;
		supportsOutputReady = false;
		driverRefCounter    = 0;
		shuttingDown.store(false);
	}

	char drvName[128];
	strncpy(drvName, driverName.c_str(), sizeof(drvName) - 1);
	drvName[sizeof(drvName) - 1] = '\0';

	if (loadAsioDriver(drvName)) {
		if (ASIOInit(&driverInfo) == ASE_OK) {
			currentDriverName = driverName;
			isOpen            = true;

			ASIOGetChannels(&inputChannels, &outputChannels);
			if (inputChannels > MAX_ASIO_CHANNELS)
				inputChannels = MAX_ASIO_CHANNELS;
			if (outputChannels > MAX_ASIO_CHANNELS)
				outputChannels = MAX_ASIO_CHANNELS;

			long minSize, maxSize, prefSize, granularity;
			ASIOGetBufferSize(&minSize, &maxSize, &prefSize, &granularity);
			preferredSize = prefSize;

			ASIOGetSampleRate(&sampleRate);
			if (sampleRate <= 0) {
				ASIOSetSampleRate(48000.0);
				ASIOGetSampleRate(&sampleRate);
			}

			long numBuffers = inputChannels + outputChannels;
			for (long i = 0; i < inputChannels; i++) {
				bufferInfos[i].isInput    = ASIOTrue;
				bufferInfos[i].channelNum = i;
				bufferInfos[i].buffers[0] = nullptr;
				bufferInfos[i].buffers[1] = nullptr;
			}
			for (long i = 0; i < outputChannels; i++) {
				long bufIdx                    = inputChannels + i;
				bufferInfos[bufIdx].isInput    = ASIOFalse;
				bufferInfos[bufIdx].channelNum = i;
				bufferInfos[bufIdx].buffers[0] = nullptr;
				bufferInfos[bufIdx].buffers[1] = nullptr;
			}

			g_asioCallbacks.bufferSwitch         = &bufferSwitch;
			g_asioCallbacks.sampleRateDidChange  = &sampleRateChanged;
			g_asioCallbacks.asioMessage          = &asioMessages;
			g_asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;

			if (ASIOCreateBuffers(bufferInfos, numBuffers, preferredSize, &g_asioCallbacks) == ASE_OK) {
				for (long i = 0; i < numBuffers; i++) {
					channelInfos[i].channel = bufferInfos[i].channelNum;
					channelInfos[i].isInput = bufferInfos[i].isInput;
					ASIOGetChannelInfo(&channelInfos[i]);
				}

				supportsOutputReady = (ASIOOutputReady() == ASE_OK);

				floatInputBuffers.resize(inputChannels);
				for (long i = 0; i < inputChannels; i++) {
					floatInputBuffers[i].resize(preferredSize, 0.0f);
				}

				floatOutputBuffers.resize(outputChannels);
				{
					std::lock_guard<std::mutex> lock(outMutex);
					for (long i = 0; i < outputChannels; i++) {
						floatOutputBuffers[i].resize(preferredSize, 0.0f);
						circlebuf_pop_front(&outBuffers[i], nullptr, outBuffers[i].size);
					}
				}

				if (ASIOStart() == ASE_OK) {
					isPlayingState = true;
					blog(LOG_INFO, "Started ASIO device: %s", driverName.c_str());
				}
			}
		}
		if (!isOpen && asioDrivers)
			asioDrivers->removeCurrentDriver();
	}
	if (isOpen)
		driverRefCounter++;
}

void ASIOManager::releaseDriver(const std::string& driverName)
{
	if (!isOpen || currentDriverName != driverName)
		return;
	if (driverRefCounter > 0)
		driverRefCounter--;
	if (driverRefCounter <= 0) {
		shuttingDown.store(true);
		if (isPlayingState)
			ASIOStop();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		ASIODisposeBuffers();
		ASIOExit();
		if (asioDrivers)
			asioDrivers->removeCurrentDriver();
		isOpen              = false;
		isPlayingState      = false;
		supportsOutputReady = false;
		driverRefCounter    = 0;
		currentDriverName   = "";
		shuttingDown.store(false);
	}
}

void ASIOManager::forceReset()
{
	if (!isOpen || currentDriverName.empty())
		return;

	std::string driverName    = currentDriverName;
	int         savedRefCount = driverRefCounter;

	blog(LOG_INFO, "Forcing driver reset for %s", driverName.c_str());

	shuttingDown.store(true);
	if (isPlayingState)
		ASIOStop();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	ASIODisposeBuffers();
	ASIOExit();
	if (asioDrivers)
		asioDrivers->removeCurrentDriver();

	isOpen              = false;
	isPlayingState      = false;
	supportsOutputReady = false;
	driverRefCounter    = 0;
	currentDriverName   = "";
	shuttingDown.store(false);

	ensureDriverLoaded(driverName);

	if (isOpen) {
		driverRefCounter = savedRefCount;
		blog(LOG_INFO, "Driver reset complete.");
	} else {
		blog(LOG_ERROR, "Driver failed to reload after reset!");
	}
}

void ASIOManager::fillDeviceList(obs_property_t* prop)
{
	obs_property_list_clear(prop);
	obs_property_list_add_string(prop, "No Driver", "");
	if (!asioDriverList)
		return;
	for (int j = 0; j < asioDriverList->asioGetNumDev(); j++) {
		char driverName[64];
		if (asioDriverList->asioGetDriverName(j, driverName, sizeof(driverName)) == 0) {
			obs_property_list_add_string(prop, driverName, driverName);
		}
	}
}

std::vector<std::string> ASIOManager::getInputChannels(const std::string& driverName)
{
	std::vector<std::string> result;
	if (isOpen && currentDriverName == driverName) {
		for (long i = 0; i < inputChannels; i++) {
			result.push_back(channelInfos[i].name);
		}
	}
	return result;
}

std::vector<std::string> ASIOManager::getOutputChannels(const std::string& driverName)
{
	std::vector<std::string> result;
	if (isOpen && currentDriverName == driverName) {
		for (long i = 0; i < outputChannels; i++) {
			long bufIdx = inputChannels + i;
			result.push_back(channelInfos[bufIdx].name);
		}
	}
	return result;
}

void ASIOManager::openControlPanel()
{
	if (isOpen)
		ASIOControlPanel();
}

void ASIOManager::addClient(ASIOClient* client)
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	clients.push_back(client);
}

void ASIOManager::removeClient(ASIOClient* client)
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	auto                        it = std::remove(clients.begin(), clients.end(), client);
	if (it != clients.end()) {
		clients.erase(it, clients.end());
	}
}

void ASIOManager::pushOutputAudio(
		const std::vector<int>& asioChannels, const std::vector<const float*>& data, size_t frames)
{
	std::lock_guard<std::mutex> lock(outMutex);

	const float* mappedData[MAX_ASIO_CHANNELS] = {nullptr};

	for (size_t i = 0; i < asioChannels.size() && i < data.size(); i++) {
		int asioChannel = asioChannels[i];
		if (asioChannel >= 0 && asioChannel < outputChannels && data[i] != nullptr) {
			if (mappedData[asioChannel] == nullptr)
				mappedData[asioChannel] = data[i];
		}
	}

	static std::vector<float> silence;
	if (silence.size() < frames)
		silence.resize(frames, 0.0f);

	for (long ch = 0; ch < outputChannels; ch++) {
		if (mappedData[ch] != nullptr)
			circlebuf_push_back(&outBuffers[ch], mappedData[ch], frames * sizeof(float));
		else
			circlebuf_push_back(&outBuffers[ch], silence.data(), frames * sizeof(float));
	}
}

bool ASIOManager::isPlaying() const
{
	return isPlayingState;
}
std::string ASIOManager::getCurrentDriverName() const
{
	return currentDriverName;
}
long ASIOManager::getOutputChannelsCount() const
{
	return outputChannels;
}

// --- ASIO Callbacks ---
void ASIOManager::bufferSwitch(long index, ASIOBool processNow)
{
	ASIOManager& mgr = ASIOManager::getInstance();

	if (mgr.shuttingDown.load())
		return;

	uint64_t ts = os_gettime_ns();

	for (long ch = 0; ch < mgr.inputChannels; ch++) {
		void*          src            = mgr.bufferInfos[ch].buffers[index];
		ASIOSampleType type           = mgr.channelInfos[ch].type;
		size_t         frames         = mgr.preferredSize;
		float*         dst            = mgr.floatInputBuffers[ch].data();
		int            bytesPerSample = getBytesPerSample(type);

		for (size_t f = 0; f < frames; f++)
			dst[f] = convertAsioSampleToFloat((uint8_t*)src + (f * bytesPerSample), type);
	}

	{
		std::lock_guard<std::mutex> lock(mgr.clientsMutex);
		for (auto* client : mgr.clients)
			client->pushAudio(mgr.floatInputBuffers, ts, (uint32_t)mgr.sampleRate, mgr.preferredSize);
	}

	std::lock_guard<std::mutex> outLock(mgr.outMutex);
	for (long ch = 0; ch < mgr.outputChannels; ch++) {
		long           bufIdx         = mgr.inputChannels + ch;
		void*          dst            = mgr.bufferInfos[bufIdx].buffers[index];
		ASIOSampleType type           = mgr.channelInfos[bufIdx].type;
		size_t         frames         = mgr.preferredSize;
		int            bytesPerSample = getBytesPerSample(type);

		size_t bytesNeeded = frames * sizeof(float);
		float* tempFloat   = mgr.floatOutputBuffers[ch].data();
		size_t available   = mgr.outBuffers[ch].size;

		size_t max_buffer = (size_t)(mgr.sampleRate * sizeof(float) / 10);
		if (available > max_buffer) {
			circlebuf_pop_front(&mgr.outBuffers[ch], nullptr, available - max_buffer);
			available = max_buffer;
		}

		if (available >= bytesNeeded) {
			circlebuf_pop_front(&mgr.outBuffers[ch], tempFloat, bytesNeeded);
		} else {
			if (available > 0)
				circlebuf_pop_front(&mgr.outBuffers[ch], tempFloat, available);
			memset((uint8_t*)tempFloat + available, 0, bytesNeeded - available);
		}

		for (size_t f = 0; f < frames; f++)
			convertFloatToAsioSample(tempFloat[f], (uint8_t*)dst + (f * bytesPerSample), type);
	}

	if (mgr.supportsOutputReady)
		ASIOOutputReady();
}

ASIOTime* ASIOManager::bufferSwitchTimeInfo(ASIOTime* timeInfo, long index, ASIOBool processNow)
{
	bufferSwitch(index, processNow);
	return nullptr;
}

void ASIOManager::sampleRateChanged(ASIOSampleRate sRate)
{
	ASIOManager::getInstance().sampleRate = sRate;
}

long ASIOManager::asioMessages(long selector, long value, void* message, double* opt)
{
	long ret = 0;
	switch (selector) {
	case kAsioSelectorSupported:
		if (value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest ||
				value == kAsioLatenciesChanged || value == kAsioOverload)
			ret = 1L;
		break;
	case kAsioResetRequest:
		blog(LOG_INFO, "ASIO driver requested a reset. Scheduling deferred driver reload...");
		std::thread([]() {
			CoInitialize(nullptr);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			ASIOManager::getInstance().forceReset();
			CoUninitialize();
		}).detach();
		ret = 1L;
		break;
	case kAsioEngineVersion:
		ret = 2L;
		break;
	case kAsioResyncRequest:
		blog(LOG_WARNING, "ASIO driver requested a resynchronization due to buffer sync loss.");
		ret = 1L;
		break;
	case kAsioLatenciesChanged:
		blog(LOG_INFO, "ASIO driver reported latency changes.");
		ret = 1L;
		break;
	case kAsioOverload:
		blog(LOG_WARNING, "ASIO driver reported buffer overload / audio dropout (underrun)!");
		ret = 1L;
		break;
	}
	return ret;
}

long ASIOManager::getInputLatency() const
{
	long inLat = 0, outLat = 0;
	if (isOpen)
		ASIOGetLatencies(&inLat, &outLat);
	return inLat;
}

long ASIOManager::getOutputLatency() const
{
	long inLat = 0, outLat = 0;
	if (isOpen)
		ASIOGetLatencies(&inLat, &outLat);
	return outLat;
}

long ASIOManager::getSampleRate() const
{
	return (long)sampleRate;
}
