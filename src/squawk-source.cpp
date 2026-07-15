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

#include "audio-thread.h"
#include "input-thread.h"
#include "model-utils/model-downloader-types.h"
#include "model-utils/model-downloader.h"
#include "model-utils/model-find-utils.h"
#include "plugin-support.h"
#include "sherpa-tts/sherpa-tts.h"
#include "squawk-source-data.h"
#include "squawk-source.h"
#include "tts-utils.h"

#include <new>
#include <chrono>
#include <util/platform.h>
#include <fstream>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <random>
#include <algorithm>
#include <cctype>

static uint64_t last_check_time_ns = 0;
static const uint64_t CHECK_INTERVAL_NS = 500000000ULL; // 500ms

const char *squawk_source_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Squawk Text-to-Speech";
}

void audio_samples_callback(void *data, const float *samples, int num_samples, int sample_rate)
{
	UNUSED_PARAMETER(sample_rate);
	squawk_source_data *squawk_data = (squawk_source_data *)data;
	squawk_data->audioThread->pushAudioSamples(
		std::vector<float>(samples, samples + num_samples));
}

void CheckForNewTextLines(void *param, float seconds)
{
	UNUSED_PARAMETER(seconds);

	uint64_t current_time_ns = os_gettime_ns();
	if (current_time_ns - last_check_time_ns < CHECK_INTERVAL_NS) {
		return; 
	}
	last_check_time_ns = current_time_ns;

	squawk_source_data *squawk_data = static_cast<squawk_source_data*>(param);

	obs_data_t *settings = obs_source_get_settings(squawk_data->context);
	std::string text_file_path = obs_data_get_string(settings, "tts_tracked_file");
	std::string state_file_path = obs_data_get_string(settings, "tts_state_file");
	
	// Fetch dynamic auto-voice settings
	uint32_t num_voices = (uint32_t)obs_data_get_int(settings, "auto_voice_num");
	std::string memory_file = obs_data_get_string(settings, "auto_voice_memory_file");
	std::string blocked_file = obs_data_get_string(settings, "auto_voice_blocked_file");
	std::string owner_name = obs_data_get_string(settings, "auto_voice_owner");
	uint32_t default_speaker = (uint32_t)obs_data_get_int(settings, "speaker_id");
	std::string fallback_text = obs_data_get_string(settings, "text");

	obs_data_release(settings);

	if (text_file_path.empty() || state_file_path.empty()) return;

	uint64_t last_offset = 0;
	std::ifstream state_in(state_file_path);
	if (state_in.is_open()) {
		state_in >> last_offset;
		state_in.close();
	}

	std::ifstream text_in(text_file_path, std::ios::binary | std::ios::ate);
	if (!text_in.is_open()) return; 

	uint64_t current_file_size = static_cast<uint64_t>(text_in.tellg());

	if (current_file_size < last_offset) last_offset = 0; 
	if (current_file_size == last_offset) return; 

	text_in.seekg(static_cast<std::streamoff>(last_offset), std::ios::beg);

	auto trim = [](std::string s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
		return s;
	};

	std::string new_line;
	while (std::getline(text_in, new_line)) {
		if (!new_line.empty() && new_line.back() == '\r') {
			new_line.pop_back();
		}
		if (new_line.empty()) continue;

		// 2. Bypass Mode check
		if (num_voices <= 0 || memory_file.empty()) {
			std::string final_text = new_line;
			if (squawk_data->phonetic_transcription) final_text = phonetic_transcription(new_line);
			generate_audio_from_text(squawk_data->tts_context, final_text, default_speaker, squawk_data->speed);
			continue;
		}

		// 3. Standard Mode Parse
		size_t colon_pos = new_line.find(':');
		if (colon_pos == std::string::npos) {
			std::string final_text = new_line;
			if (squawk_data->phonetic_transcription) final_text = phonetic_transcription(new_line);
			generate_audio_from_text(squawk_data->tts_context, final_text, default_speaker, squawk_data->speed);
			continue;
		}

		std::string viewer_name = trim(new_line.substr(0, colon_pos));
		std::string message = trim(new_line.substr(colon_pos + 1));

		// Load blocked voices
		std::unordered_set<uint32_t> blocked_voices;
		if (!blocked_file.empty()) {
			std::ifstream bf(blocked_file);
			uint32_t bv;
			while (bf >> bv) blocked_voices.insert(bv);
		}

		// Load memory file
		std::unordered_map<std::string, uint32_t> memory_map;
		std::ifstream mf(memory_file);
		std::string m_name;
		uint32_t m_id;
		while (mf >> m_name >> m_id) memory_map[m_name] = m_id;
		mf.close();

		auto save_memory = [&]() {
			std::ofstream out(memory_file, std::ios::trunc);
			for (const auto& kv : memory_map) out << kv.first << " " << kv.second << "\n";
		};

		// 4. Command Handling
		bool is_owner = (viewer_name == owner_name);
		if (is_owner && message.rfind("!delete ", 0) == 0) {
			std::string target = trim(message.substr(8));
			memory_map.erase(target);
			save_memory();
			continue; 
		}

		if (is_owner && message.rfind("!change ", 0) == 0) {
			std::string target = trim(message.substr(8));
			
			std::vector<uint32_t> permitted_voices;
			for (uint32_t i = 0; i < (uint32_t)num_voices; ++i) {
				if (blocked_voices.find(i) == blocked_voices.end()) permitted_voices.push_back(i);
			}
			
			uint32_t new_id = default_speaker;
			if (!permitted_voices.empty()) {
				std::unordered_set<uint32_t> used;
				for (const auto& kv : memory_map) used.insert(kv.second);
				
				std::vector<uint32_t> unused;
				for (uint32_t v : permitted_voices) {
					if (used.find(v) == used.end()) unused.push_back(v);
				}
				
				if (!unused.empty()) {
					new_id = unused[0];
				} else {
					new_id = permitted_voices[rand() % permitted_voices.size()];
				}
			}

			memory_map[target] = new_id;
			save_memory();

			std::string final_text = fallback_text;
			if (squawk_data->phonetic_transcription) final_text = phonetic_transcription(final_text);
			generate_audio_from_text(squawk_data->tts_context, final_text, new_id, squawk_data->speed);
			continue;
		}

		// 5. Normal Message Handling
		uint32_t assigned_voice = default_speaker;

		// If the viewer is the owner, skip pool assignment entirely.
		// They will automatically use 'default_speaker' (Speaker ID) and won't be saved to the memory file.
		if (!is_owner) {
			if (memory_map.find(viewer_name) != memory_map.end()) {
				assigned_voice = memory_map[viewer_name];
			} else {
				std::vector<uint32_t> permitted_voices;
				for (uint32_t i = 0; i < num_voices; ++i) {
					if (blocked_voices.find(i) == blocked_voices.end()) permitted_voices.push_back(i);
				}
				
				if (!permitted_voices.empty()) {
					std::unordered_set<uint32_t> used;
					for (const auto& kv : memory_map) used.insert(kv.second);
					
					std::vector<uint32_t> unused;
					for (uint32_t v : permitted_voices) {
						if (used.find(v) == used.end()) unused.push_back(v);
					}
					
					if (!unused.empty()) {
						assigned_voice = unused[0];
					} else {
						assigned_voice = permitted_voices[rand() % permitted_voices.size()];
					}
				}
				memory_map[viewer_name] = assigned_voice;
				save_memory();
			}
		}

		std::string final_text = new_line;
		if (squawk_data->phonetic_transcription) final_text = phonetic_transcription(final_text);
		generate_audio_from_text(squawk_data->tts_context, final_text, assigned_voice, squawk_data->speed);
	}

	text_in.clear(); 
	last_offset = static_cast<uint64_t>(text_in.tellg());
	text_in.close();

	std::ofstream state_out(state_file_path, std::ios::trunc);
	if (state_out.is_open()) state_out << last_offset;
}

void *squawk_source_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "Squawk source create");

	void *data = bmalloc(sizeof(squawk_source_data));
	squawk_source_data *squawk_data = new (data) squawk_source_data();

	squawk_data->tts_context.callback_data = squawk_data;
	squawk_data->tts_context.model_name = "";

	squawk_data->context = source;
	squawk_data->audioThread = std::make_unique<AudioThread>(source);
	squawk_data->audioThread->start();

	squawk_data->inputThread = std::make_unique<InputThread>();
	squawk_data->inputThread->setSpeechGenerationCallback(
		[squawk_data](const std::string &text) {
			std::string transformed_text = text;
			if (squawk_data->phonetic_transcription) {
				transformed_text = phonetic_transcription(text);
			}
			generate_audio_from_text(squawk_data->tts_context, transformed_text,
						 squawk_data->speaker_id, squawk_data->speed);
		});
	squawk_data->inputThread->start();

	squawk_source_update(data, settings);

	std::string text_file_path = obs_data_get_string(settings, "tts_tracked_file");
	std::string state_file_path = obs_data_get_string(settings, "tts_state_file");

	if (!text_file_path.empty()) {
		std::ofstream text_out(text_file_path, std::ios::out | std::ios::trunc);
		if (text_out.is_open()) text_out.close();
	}

	if (!state_file_path.empty()) {
		std::ofstream state_out(state_file_path, std::ios::out | std::ios::trunc);
		if (state_out.is_open()) {
			state_out << 0ULL;
			state_out.close();
		}
	}

	obs_add_tick_callback(CheckForNewTextLines, squawk_data);

	return data;
}

void squawk_source_destroy(void *data)
{
	squawk_source_data *squawk_data = (squawk_source_data *)data;
	
	obs_remove_tick_callback(CheckForNewTextLines, squawk_data);

	squawk_data->audioThread->stop();
	squawk_data->inputThread->stop();
	destroy_sherpa_tts_context(squawk_data->tts_context);
	squawk_data->~squawk_source_data();
	bfree(data);
}

void squawk_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "speaker_id", 0);
	obs_data_set_default_double(settings, "speed", 1.0);
	obs_data_set_default_string(settings, "text", "Hello, World!");
	obs_data_set_default_string(settings, "model", "vits-coqui-en-vctk");
	obs_data_set_default_string(settings, "input_source", "none");
	obs_data_set_default_bool(settings, "line_by_line", false);
	obs_data_set_default_bool(settings, "phonetic_transcription", true);
	obs_data_set_default_bool(settings, "input_debounce", true);

	obs_data_set_default_string(settings, "tts_tracked_file", "");
	obs_data_set_default_string(settings, "tts_state_file", "");

	// Auto-Voice Defaults
	obs_data_set_default_int(settings, "auto_voice_num", 0);
	obs_data_set_default_string(settings, "auto_voice_memory_file", "");
	obs_data_set_default_string(settings, "auto_voice_blocked_file", "");
	obs_data_set_default_string(settings, "auto_voice_owner", "");
}

bool add_sources_to_list(void *list_property, obs_source_t *source)
{
	auto source_id = obs_source_get_id(source);
	if (strcmp(source_id, "text_ft2_source_v2") != 0 &&
	    strcmp(source_id, "text_gdiplus_v2") != 0 &&
	    strcmp(source_id, "text_gdiplus_v3") != 0) {
		return true;
	}

	obs_property_t *sources = (obs_property_t *)list_property;
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(sources, name, name);
	return true;
}

obs_properties_t *squawk_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *model = obs_properties_add_list(
		ppts, "model", MT_("Model"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (auto model_info : model_infos) {
		obs_property_list_add_string(model, model_info.friendly_name.c_str(),
					     model_info.local_folder_name.c_str());
	}
	
	obs_property_set_modified_callback2(
		model,
		[](void *data_, obs_properties *props, obs_property_t *property,
		   obs_data_t *settings) {
			UNUSED_PARAMETER(props);
			UNUSED_PARAMETER(property);

			squawk_source_data *squawk_data_ = (squawk_source_data *)data_;
			const char *model_name = obs_data_get_string(settings, "model");
			obs_log(LOG_INFO, "Selected model: %s", model_name);
			auto model_info = find_model_info_by_name(model_name);
			std::string model_folder = find_model_folder(model_info);
			if (!model_folder.empty()) {
				obs_log(LOG_INFO, "Model folder found: %s", model_folder.c_str());
				return true;
			}

			obs_log(LOG_INFO, "Model folder not found - downloading...");
			download_model_with_ui_dialog(
				model_info, [model_info, squawk_data_](int download_status,
								       const std::string &path) {
					UNUSED_PARAMETER(download_status);
					obs_log(LOG_INFO, "Model downloaded: %s", path.c_str());
					unpack_model(model_info, path);
					squawk_data_->tts_context.model_name = "";
					obs_data_t *source_settings =
						obs_source_get_settings(squawk_data_->context);
					obs_source_update(squawk_data_->context, source_settings);
					obs_data_release(source_settings);
				});
			return true;
		},
		data);

	obs_properties_add_int(ppts, "speaker_id", MT_("Speaker_ID"), 0, 1000, 1);
	obs_properties_add_float_slider(ppts, "speed", MT_("Speed"), 0.1, 2.5, 0.1);

	obs_properties_t *inputs_group = obs_properties_create();
	obs_properties_add_group(ppts, "inputs", MT_("Inputs"), OBS_GROUP_NORMAL, inputs_group);
	
	obs_property_t *input_source = obs_properties_add_list(inputs_group, "input_source",
							       "Input Source", OBS_COMBO_TYPE_LIST,
							       OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(input_source, MT_("none_no_input"), "none");
	obs_enum_sources(add_sources_to_list, input_source);
	
	obs_properties_add_path(inputs_group, "tts_tracked_file", "Tracked Text File (Input)", 
							OBS_PATH_FILE, "Text Files (*.txt);;All Files (*.*)", nullptr);
	obs_properties_add_path(inputs_group, "tts_state_file", "State File (Memory)", 
							OBS_PATH_FILE, "Text Files (*.txt);;All Files (*.*)", nullptr);

	// Auto-Voice Fields
	obs_properties_add_int(inputs_group, "auto_voice_num", "Auto-Voice: Total Pool Size", 0, 1000, 1);
	obs_properties_add_path(inputs_group, "auto_voice_memory_file", "Auto-Voice: Memory File", 
							OBS_PATH_FILE, "Text Files (*.txt);;All Files (*.*)", nullptr);
	obs_properties_add_path(inputs_group, "auto_voice_blocked_file", "Auto-Voice: Blocked IDs File", 
							OBS_PATH_FILE, "Text Files (*.txt);;All Files (*.*)", nullptr);
	obs_properties_add_text(inputs_group, "auto_voice_owner", "Auto-Voice: Owner Chat Name", OBS_TEXT_DEFAULT);

	obs_property_t *lbl_prop =
		obs_properties_add_bool(inputs_group, "line_by_line", MT_("Line_By_Line"));
	obs_property_set_long_description(lbl_prop, MT_("line_by_line_help"));
	
	obs_property_t *debouce_prop =
		obs_properties_add_bool(inputs_group, "input_debounce", MT_("Input_Debounce"));
	obs_property_set_long_description(debouce_prop, MT_("input_debounce_help"));

	obs_properties_add_text(ppts, "text", MT_("Text"), OBS_TEXT_DEFAULT);

	obs_properties_add_button2(
		ppts, "generate_audio", MT_("Generate_Audio"),
		[](obs_properties_t *props, obs_property_t *property, void *data_) {
			UNUSED_PARAMETER(props);
			UNUSED_PARAMETER(property);

			obs_log(LOG_INFO, "Generate Audio button clicked");
			squawk_source_data *squawk_data_ = (squawk_source_data *)data_;
			obs_data_t *settings = obs_source_get_settings(squawk_data_->context);
			std::string text = obs_data_get_string(settings, "text");
			uint32_t speaker_id = (uint32_t)obs_data_get_int(settings, "speaker_id");
			obs_data_release(settings);

			if (squawk_data_->phonetic_transcription) {
				std::string original_text = text;
				text = phonetic_transcription(text);
				obs_log(LOG_INFO, "Phonetic transcription: %s -> %s",
					original_text.c_str(), text.c_str());
			}

			generate_audio_from_text(squawk_data_->tts_context, text, speaker_id,
						 squawk_data_->speed);

			return true;
		}, data);

	obs_properties_add_button2(
		ppts, "delete_models", MT_("Delete_Cached_Models"),
		[](obs_properties_t *props, obs_property_t *property, void *data_) {
			UNUSED_PARAMETER(props);
			UNUSED_PARAMETER(property);

			obs_log(LOG_INFO, "Delete Cached Models button clicked");
			delete_cached_models();
			squawk_source_data *squawk_data_ = (squawk_source_data *)data_;
			obs_data_t *settings = obs_source_get_settings(squawk_data_->context);
			obs_data_set_string(settings, "model", "vits-coqui-en-vctk");
			obs_data_release(settings);
			return true;
		}, data);

	obs_properties_add_bool(ppts, "phonetic_transcription", MT_("Phonetic_Transcription"));

	char small_info[256];
	snprintf(small_info, sizeof(small_info), PLUGIN_INFO_TEMPLATE, PLUGIN_VERSION);
	obs_properties_add_text(ppts, "plugin_info", small_info, OBS_TEXT_INFO);

	return ppts;
}

void squawk_source_update(void *data, obs_data_t *settings)
{
	obs_log(LOG_DEBUG, "Squawk source update");

	squawk_source_data *squawk_data = (squawk_source_data *)data;

	squawk_data->speaker_id = (uint32_t)obs_data_get_int(settings, "speaker_id");
	squawk_data->speed = (float)obs_data_get_double(settings, "speed");
	squawk_data->phonetic_transcription = obs_data_get_bool(settings, "phonetic_transcription");

	std::string source = obs_data_get_string(settings, "input_source");
	if (source == "none") {
		source = "";
	}
	squawk_data->inputThread->setOBSTextSource(source);
	squawk_data->inputThread->setReadingMode(obs_data_get_bool(settings, "line_by_line")
							 ? ReadingMode::LineByLine
							 : ReadingMode::Whole);
	squawk_data->inputThread->setDebounceMode(obs_data_get_bool(settings, "input_debounce")
							  ? DebouceMode::Debounced
							  : DebouceMode::Immediate);

	std::string new_model_name = obs_data_get_string(settings, "model");
	if (new_model_name != squawk_data->tts_context.model_name) {
		destroy_sherpa_tts_context(squawk_data->tts_context);
		squawk_data->tts_context.model_name = new_model_name;
		init_sherpa_tts_context(squawk_data->tts_context, audio_samples_callback,
					squawk_data);
	}
}

void squawk_source_activate(void *data)
{
	UNUSED_PARAMETER(data);
}

void squawk_source_deactivate(void *data)
{
	UNUSED_PARAMETER(data);
}

void squawk_source_show(void *data)
{
	UNUSED_PARAMETER(data);
}

void squawk_source_hide(void *data)
{
	UNUSED_PARAMETER(data);
}
