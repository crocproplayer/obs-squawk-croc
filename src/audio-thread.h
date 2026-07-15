#ifndef AUDIO_THREAD_H
#define AUDIO_THREAD_H

#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <deque>
#include <atomic>

#include <obs.h>
#include "plugin-support.h"

class AudioThread {
public:
	AudioThread(obs_source_t *ctx) : buffer(), mutex(), context(ctx) {}

	void start()
	{
		if (thread.joinable()) {
			return;
		}
		running = true;
		thread = std::thread(&AudioThread::run, this);
	}

	void stop()
	{
		running = false;
		if (thread.joinable()) {
			thread.join();
		}
	}

	void pushAudioSamples(const std::vector<float> &samples)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (auto sample : samples) {
			buffer.push_back(sample);
		}
	}

	// New method: Call this if you want new TTS to cut off the currently playing TTS
	void interruptPlayback()
	{
		std::lock_guard<std::mutex> lock(mutex);
		buffer.clear();
	}

private:
	const int TARGET_BATCH_SIZE_MS = 20; // 20ms chunks for smooth OBS ingestion

	std::deque<float> buffer;
	std::mutex mutex;
	std::thread thread;
	obs_source_t *context;
	int sample_rate = 22050;
	std::atomic<bool> running{false};

	void run();
};

#endif // AUDIO_THREAD_H