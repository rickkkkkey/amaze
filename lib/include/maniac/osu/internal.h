#pragma once

#include <iostream>

constexpr auto OSU_STATUS_IN_MENU = 0;
constexpr auto OSU_STATUS_PLAYING = 2;
constexpr auto OSU_STATUS_IN_SONG_SELECT = 5;

inline Process *process;

class hit_object {
	uintptr_t base;

public:
	int32_t start_time;
	int32_t end_time;
	int32_t type;
	int32_t column;

	bool is_slider;

	hit_object() : base(0), start_time(0), end_time(0), type(0),
		column(0), is_slider(false) {}

	explicit hit_object(uintptr_t base) : base(base) {
		start_time = get_start_time();
		end_time = get_end_time();
		type = get_type();
		column = get_column();

		is_slider = (start_time != end_time);
	}

private:
	[[nodiscard]] int32_t get_start_time() const {
		return process->read_memory<int32_t>(base + 0x10);
	}

	[[nodiscard]] int32_t get_end_time() const {
		return process->read_memory<int32_t>(base + 0x14);
	}

	[[nodiscard]] int32_t get_type() const {
		return process->read_memory<int32_t>(base + 0x18);
	}

	[[nodiscard]] int32_t get_column() const {
		// Try column offset 0x9C, fallback to calculating via X-coordinate if needed
		int32_t col = process->read_memory<int32_t>(base + 0x9C);
		return (col >= 0 && col <= 18) ? col : 0;
	}
};

template<typename T>
struct list_container {
	uintptr_t base;

	size_t size;
	std::vector<T> content;

	explicit list_container(uintptr_t base) : base(base), size(get_size()),
		content(get_content()) {}

	[[nodiscard]] size_t get_size() {
		if (!base) return 0;
		int32_t s = 0;
		if (process->read_memory<int32_t>(base + 0xC, &s) && s > 0 && s < 100000) {
			return static_cast<size_t>(s);
		}
		return 0;
	}

	[[nodiscard]] std::vector<T> get_content() {
		std::vector<T> vector;
		if (!base) return vector;

		uintptr_t content_address = 0;
		if (!process->read_memory<uintptr_t>(base + 0x4, &content_address) || !content_address) {
			return vector;
		}

		auto content_size = get_size();
		if (content_size == 0) return vector;

		vector.reserve(content_size);

		for (size_t i = 0; i < content_size; i++) {
			uintptr_t item_ptr = 0;
			if (process->read_memory<uintptr_t>(content_address + 0x8 + (i * 0x4), &item_ptr) && item_ptr > 0x10000) {
				vector.push_back(T(item_ptr));
			}
		}

		return vector;
	}
};

struct hit_manager_headers {
	uintptr_t base;

	int column_count;

	explicit hit_manager_headers(uintptr_t base) : base(base),
		column_count(get_column_count()) {}

	[[nodiscard]] int get_column_count() const {
		if (!base) return 4;
		
		float cs = 0.0f;
		if (process->read_memory<float>(base + 0x30, &cs) && cs >= 1.0f && cs <= 18.0f) {
			return static_cast<int>(cs);
		}
		if (process->read_memory<float>(base + 0x2C, &cs) && cs >= 1.0f && cs <= 18.0f) {
			return static_cast<int>(cs);
		}
		return 4;
	}
};

struct hit_manager {
	uintptr_t base;

	hit_manager_headers headers;
	list_container<hit_object> list;

	explicit hit_manager(uintptr_t base) : base(base), headers(get_headers()),
		list(get_list()) {}

	[[nodiscard]] hit_manager_headers get_headers() const {
		return hit_manager_headers(base);
	}

	[[nodiscard]] list_container<hit_object> get_list() const {
		if (!base) return list_container<hit_object>(0);

		// Deep probe every 4-byte aligned offset inside HitObjectManager for a valid .NET List
		for (uintptr_t offset = 0x20; offset <= 0x80; offset += 0x4) {
			uintptr_t list_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &list_ptr) && list_ptr > 0x10000) {
				int32_t count = 0;
				if (process->read_memory<int32_t>(list_ptr + 0xC, &count) && count > 0 && count < 100000) {
					uintptr_t items_arr = 0;
					if (process->read_memory<uintptr_t>(list_ptr + 0x4, &items_arr) && items_arr > 0x10000) {
						std::cout << "[Maniac::Internal] HitObjects list found at offset 0x" 
						          << std::hex << offset << " (Count: " << std::dec << count << ")" << std::endl;
						return list_container<hit_object>(list_ptr);
					}
				}
			}
		}
		return list_container<hit_object>(0);
	}
};

struct map_player {
	uintptr_t base;

	hit_manager manager;

	explicit map_player(uintptr_t base) : base(base), manager(get_hit_manager()) {}

	[[nodiscard]] hit_manager get_hit_manager() const {
		if (!base) return hit_manager(0);

		// Probe offsets in Player to find the HitObjectManager instance containing a valid list
		for (uintptr_t offset = 0x30; offset <= 0x60; offset += 0x4) {
			uintptr_t hm_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &hm_ptr) && hm_ptr > 0x10000) {
				hit_manager candidate(hm_ptr);
				if (!candidate.list.content.empty()) {
					std::cout << "[Maniac::Internal] HitObjectManager resolved at Player+0x" 
					          << std::hex << offset << std::dec << std::endl;
					return candidate;
				}
			}
		}

		// Fallback: Player itself might directly host the HitObjectManager structure
		hit_manager direct(base);
		if (!direct.list.content.empty()) {
			return direct;
		}

		return hit_manager(0);
	}
};
