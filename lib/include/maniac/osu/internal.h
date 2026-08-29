#pragma once

#include <iostream>
#include <algorithm>

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
		if (!base) return 0;
		// Probing primary offset 0x10, fallback 0x14
		int32_t t = 0;
		if (process->read_memory<int32_t>(base + 0x10, &t) && t >= -100000 && t <= 3600000) {
			return t;
		}
		if (process->read_memory<int32_t>(base + 0x14, &t) && t >= -100000 && t <= 3600000) {
			return t;
		}
		return 0;
	}

	[[nodiscard]] int32_t get_end_time() const {
		if (!base) return 0;
		int32_t t = 0;
		if (process->read_memory<int32_t>(base + 0x14, &t) && t >= -100000 && t <= 3600000) {
			return t;
		}
		if (process->read_memory<int32_t>(base + 0x18, &t) && t >= -100000 && t <= 3600000) {
			return t;
		}
		return get_start_time();
	}

	[[nodiscard]] int32_t get_type() const {
		if (!base) return 0;
		return process->read_memory<int32_t>(base + 0x18);
	}

	[[nodiscard]] int32_t get_column() const {
		if (!base) return 0;

		// 1. Check raw mania column property
		int32_t col = 0;
		if (process->read_memory<int32_t>(base + 0x9C, &col) && col >= 0 && col <= 18) {
			return col;
		}
		if (process->read_memory<int32_t>(base + 0xA0, &col) && col >= 0 && col <= 18) {
			return col;
		}

		// 2. Fallback: calculate column from hit object X position (float at 0x38 or 0x3C)
		float x_pos = 0.0f;
		if (process->read_memory<float>(base + 0x38, &x_pos) && x_pos >= 0.0f && x_pos <= 512.0f) {
			return static_cast<int32_t>(std::floor((x_pos * 4.0f) / 512.0f));
		}

		return 0;
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

	explicit hit_manager_headers(uintptr_t base) : base(base), column_count(get_column_count()) {}

	[[nodiscard]] int get_column_count() const {
		if (!base) return 4;
		
		float cs = 0.0f;
		if (process->read_memory<float>(base + 0x30, &cs) && cs >= 1.0f && cs <= 18.0f) {
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

		// Prioritize primary HitObject list offsets: 0x48, 0x40, 0x44
		for (const auto offset : { 0x48, 0x40, 0x44, 0x3C, 0x50 }) {
			uintptr_t list_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &list_ptr) && list_ptr > 0x10000) {
				int32_t count = 0;
				if (process->read_memory<int32_t>(list_ptr + 0xC, &count) && count > 50 && count < 100000) {
					uintptr_t items_arr = 0;
					if (process->read_memory<uintptr_t>(list_ptr + 0x4, &items_arr) && items_arr > 0x10000) {
						// Validate that the first element in this candidate list has a valid start_time
						uintptr_t first_item = 0;
						if (process->read_memory<uintptr_t>(items_arr + 0x8, &first_item) && first_item > 0x10000) {
							int32_t test_time = 0;
							process->read_memory<int32_t>(first_item + 0x10, &test_time);
							if (test_time > 0 && test_time < 3600000) {
								std::cout << "[Maniac::Internal] Valid HitObjects list found at offset 0x" 
								          << std::hex << offset << " (Count: " << std::dec << count 
								          << ", First Note: " << test_time << " ms)" << std::endl;
								return list_container<hit_object>(list_ptr);
							}
						}
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

		// Check primary HitObjectManager container offsets on Player: 0x40, 0x48, 0x44, 0x38
		for (const auto offset : { 0x40, 0x48, 0x44, 0x38, 0x30, 0x50 }) {
			uintptr_t hm_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &hm_ptr) && hm_ptr > 0x10000) {
				hit_manager candidate(hm_ptr);
				if (!candidate.list.content.empty()) {
					std::cout << "[Maniac::Internal] HitObjectManager confirmed at Player+0x" 
					          << std::hex << offset << std::dec << std::endl;
					return candidate;
				}
			}
		}

		return hit_manager(0);
	}
};
