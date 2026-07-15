#include <util/platform.h>
#include <thread>
#include <chrono>
#include <vector>

#include "audio-thread.h"

void AudioThread::run()
{
	// Keep a running timestamp for perfect, continuous OBS audio sync
	uint64_t current_timestamp = os_gettime_ns();

	while (this->running) {
		std::vector<float> chunk;
		size_t chunk_frames = (size_t)TARGET_BATCH_SIZE_MS * sample_rate / 1000;

		// Extract a chunk safely
		{
			std::lock_guard<std::mutex> lock(mutex);
			for (size_t i = 0; i < chunk_frames; i++) {
				if (buffer.empty()) {
					break;
				}
				chunk.push_back(buffer.front());
				buffer.pop_front();
			}
		}

		if (!chunk.empty()) {
			obs_source_audio audio_data = {};
			audio_data.data[0] = (uint8_t *)chunk.data();
			audio_data.data[1] = nullptr;
			audio_data.frames = (uint32_t)chunk.size();
			audio_data.speakers = SPEAKERS_MONO;
			audio_data.samples_per_sec = (uint32_t)sample_rate;
			audio_data.format = AUDIO_FORMAT_FLOAT;
			
			// Give OBS the exact, continuous timestamp
			audio_data.timestamp = current_timestamp;

			obs_source_output_audio(this->context, &audio_data);

			// Advance the expected timestamp by the exact duration of this chunk
			uint64_t actual_duration_ns = (uint64_t)chunk.size() * 1000000000ULL / sample_rate;
			current_timestamp += actual_duration_ns;

			// Sleep precisely until it is time to push the next chunk
			os_sleepto_ns(current_timestamp);
		} else {
			// Queue is empty. Sleep for a standard 10ms polling interval to save CPU
			os_sleepto_ns(os_gettime_ns() + 10000000ULL);
			
			// Crucial: Reset the timestamp base so the next message doesn't 
			// calculate as "late" and dump instantly to catch up.
			current_timestamp = os_gettime_ns();
		}
	}
}