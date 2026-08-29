#include <maniac/osu.h>
#include <iostream>

using namespace osu;

Osu::Osu() : Process("osu!.exe") {
	using namespace signatures;

	base_address = find_signature(signatures::base);
	debug("found base address: %#x", base_address);
	std::cout << "[Maniac::Osu] Base address: 0x" << std::hex << base_address << std::dec << std::endl;

	status_address = find_signature(signatures::status);
	debug("found status address: %#x", status_address);
	std::cout << "[Maniac::Osu] Status address: 0x" << std::hex << status_address << std::dec << std::endl;

	ruleset_address = find_signature(signatures::ruleset);
	debug("found ruleset address: %#x", ruleset_address);
	std::cout << "[Maniac::Osu] Ruleset address: 0x" << std::hex << ruleset_address << std::dec << std::endl;

	if (!base_address || !status_address || !ruleset_address) {
		throw std::runtime_error("failed to find one or more signatures");
	}
}

Osu::~Osu() = default;

uintptr_t Osu::get_player() {
	if (!ruleset_address) return 0;

	// CurrentRuleset: dereference [ruleset_address + 0xB], then read pointer at + 0x4
	uintptr_t ruleset_instance = 0;
	if (!read_memory<uintptr_t>(ruleset_address + 0xB, &ruleset_instance) || !ruleset_instance) {
		std::cout << "[Maniac::Osu] Failed reading ruleset instance pointer at 0x"
		          << std::hex << (ruleset_address + 0xB) << std::dec << std::endl;
		return 0;
	}

	uintptr_t player = 0;
	if (!read_memory<uintptr_t>(ruleset_instance + 0x4, &player) || !player) {
		std::cout << "[Maniac::Osu] Failed reading player pointer at 0x"
		          << std::hex << (ruleset_instance + 0x4) << std::dec << std::endl;
		return 0;
	}

	return player;
}

std::string Osu::get_key_subset(const std::string &keys, int column_count) {
	if (column_count > 9) {
		throw std::runtime_error("maps with more than 9 columns are not supported");
	}

	if (column_count <= 0) {
		throw std::runtime_error("got negative column count");
	}

	const size_t key_subset_len = column_count + 1;
	char *const key_subset = reinterpret_cast<char *>(malloc(key_subset_len));

	if (!key_subset) {
		throw std::runtime_error("failed allocating memory");
	}

	key_subset[column_count] = '\0';

	const size_t subset_offset = (keys.length() / 2) - (column_count / 2);

	memmove_s(key_subset, key_subset_len, reinterpret_cast<const void *>(keys.data() + subset_offset),
		keys.length() - (subset_offset * 2));

	if (column_count % 2) {
		auto offset = column_count / 2;
		memmove_s(key_subset + offset + 1, key_subset_len + offset + 1, key_subset + offset,
			offset);

		key_subset[column_count / 2] = ' ';
	}

	auto out_string = std::string{key_subset};
	free(key_subset);

	return out_string;
}

std::vector<HitObject> Osu::get_hit_objects() {
	internal::process = this;

	const auto player_address = get_player();
	if (!player_address) {
		throw std::runtime_error("could not resolve player address");
	}

	std::cout << "[Maniac::Osu] Attempting to map Player object at 0x"
	          << std::hex << player_address << std::dec << "..." << std::endl;

	try {
		const auto player = internal::map_player(player_address);
		const auto count = player.manager.list.content.size();

		std::cout << "[Maniac::Osu] HitObjectManager mapped. Note count: "
		          << count << std::endl;

		return player.manager.list.content;
	} catch (std::runtime_error &err) {
		std::cout << "[Maniac::Osu] map_player failed: " << err.what()
		          << " (rescanning ruleset signature)" << std::endl;

		debug("failed getting map player, rescanning ruleset");
		ruleset_address = find_signature(signatures::ruleset);

		return get_hit_objects();
	}
}
