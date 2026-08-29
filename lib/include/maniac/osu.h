#pragma once

#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include <algorithm>
#include <maniac/common.h>
#include <maniac/process.h>
#include <maniac/osu/signatures.h>

namespace osu {
	namespace internal {
		#include <maniac/osu/internal.h>
	};

	using HitObject = internal::hit_object;

	class Osu : public Process {
		uintptr_t base_address = 0;
		uintptr_t ruleset_address = 0;
		uintptr_t status_address = 0;

	public:
		Osu();

		~Osu();

		uintptr_t get_player();

		int get_status();

		int32_t get_game_time();

		bool is_playing();

		std::vector<HitObject> get_hit_objects();

		static std::string get_key_subset(const std::string &keys, int column_count);
	};

	inline int32_t Osu::get_game_time() {
		if (!base_address) return -1;

		// Step 1: Dereference pointer at [Base + 0x64]
		uintptr_t time_container = 0;
		if (!read_memory<uintptr_t>(base_address + 0x64, &time_container) || !time_container) {
			return -1;
		}

		// Step 2: Read audio time at [time_container - 0x10]
		int32_t time = -1;
		if (!read_memory<int32_t>(time_container - 0x10, &time)) {
			debug("%s %#x", "failed getting game time at",
				(unsigned int)(time_container - 0x10));
		}

		return time;
	}

	inline int Osu::get_status() {
		if (!status_address) return 0;

		// Step 1: Read pointer at [status_address - 0x4]
		uintptr_t ptr = 0;
		if (!read_memory<uintptr_t>(status_address - 0x4, &ptr) || !ptr) {
			return 0;
		}

		// Step 2: Read current status value
		int status = 0;
		read_memory<int>(ptr, &status);
		return status;
	}

	inline bool Osu::is_playing() {
		return get_status() == internal::OSU_STATUS_PLAYING;
	}
}
