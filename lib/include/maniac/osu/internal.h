#pragma once

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
		column(0) {}

	explicit hit_object(uintptr_t base) : base(base) {
		start_time = get_start_time();
		end_time = get_end_time();
		type = get_type();
		column = get_column();

		is_slider = start_time != end_time;
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
		return process->read_memory<int32_t>(base + 0x9C);
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
		// Standard .NET List: _size is at offset 0xC (items buffer is at 0x4)
		return process->read_memory_safe<size_t>("list contents size", base + 0xC);
	}

	[[nodiscard]] std::vector<T> get_content() {
		std::vector<T> vector;
		if (!base) return vector;

		auto content_address = process->read_memory_safe<uintptr_t>(
			"list content address", base + 0x4);

		if (!content_address) return vector;

		auto content_size = get_size();
		if (content_size == 0 || content_size > 100000) return vector;

		vector.reserve(content_size);

		for (size_t i = 0; i < content_size; i++) {
			uintptr_t item_ptr = 0;
			if (process->read_memory<uintptr_t>(content_address + 0x8 + (i * 0x4), &item_ptr) && item_ptr) {
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
		
		// In modern osu!, CircleSize / column count in header is a float at 0x30 or 0x2C
		float cs = 0.0f;
		if (process->read_memory<float>(base + 0x30, &cs) && cs >= 1.0f && cs <= 18.0f) {
			return static_cast<int>(cs);
		}
		if (process->read_memory<float>(base + 0x2C, &cs) && cs >= 1.0f && cs <= 18.0f) {
			return static_cast<int>(cs);
		}
		return 4; // default mania fallback
	}
};

struct hit_manager {
	uintptr_t base;

	hit_manager_headers headers;
	list_container<hit_object> list;

	explicit hit_manager(uintptr_t base) : base(base), headers(get_headers()),
		list(get_list()) {}

	[[nodiscard]] hit_manager_headers get_headers() const {
		if (!base) return hit_manager_headers(0);

		// Try standard offsets for beatmap header (0x14, 0x18, 0x30)
		for (const auto offset : { 0x14, 0x18, 0x30, 0x34 }) {
			uintptr_t hdr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &hdr) && hdr > 0x10000) {
				return hit_manager_headers(hdr);
			}
		}
		return hit_manager_headers(0);
	}

	[[nodiscard]] list_container<hit_object> get_list() const {
		if (!base) return list_container<hit_object>(0);

		// Try standard offsets for HitObjects generic list (0x48, 0x44, 0x40, 0x38)
		for (const auto offset : { 0x48, 0x44, 0x40, 0x38 }) {
			uintptr_t list_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &list_ptr) && list_ptr > 0x10000) {
				size_t size = 0;
				if (process->read_memory<size_t>(list_ptr + 0xC, &size) && size > 0 && size < 100000) {
					return list_container<hit_object>(list_ptr);
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

		// Try standard offsets from Player to HitObjectManager (0x40, 0x48, 0x44, 0x38)
		for (const auto offset : { 0x40, 0x48, 0x44, 0x38, 0x34 }) {
			uintptr_t hm_ptr = 0;
			if (process->read_memory<uintptr_t>(base + offset, &hm_ptr) && hm_ptr > 0x10000) {
				return hit_manager(hm_ptr);
			}
		}
		throw std::runtime_error("failed resolving HitObjectManager from player pointer");
	}
};
