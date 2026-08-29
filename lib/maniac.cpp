#include <maniac/maniac.h>
#include <iostream>
#include <iomanip>

namespace maniac {
    void reset_keys() {
        auto keys = osu::Osu::get_key_subset(config.keys, 9);
        for (auto key : keys) {
            Process::send_keypress(key, false);
        }
    }

	void block_until_playing() {
		while (true) {
			if (osu->is_playing()) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}

	void play(const std::vector<Action> &actions) {
		reset_keys();

		size_t cur_i = 0;
		auto cur_time = 0;
		auto raw_actions = actions.data();
		auto total_actions = actions.size();

		std::cout << "[Maniac::Play] Started playback loop for " << total_actions << " actions." << std::endl;

		int loop_ticks = 0;

		while (cur_i < total_actions) {
			if (!osu->is_playing()) {
				std::cout << "[Maniac::Play] osu! is no longer in playing state. Exiting play loop." << std::endl;
				return;
			}

			cur_time = osu->get_game_time();

			// Periodic live heartbeat log to check if time is updating
			if (++loop_ticks % 2000 == 0) {
				std::cout << "[Maniac::Play] Live Time: " << cur_time 
				          << " ms | Next Action Time: " << (raw_actions + cur_i)->time 
				          << " ms | Action: " << cur_i << "/" << total_actions << std::endl;
			}

			while (cur_i < total_actions && (raw_actions + cur_i)->time <= cur_time) {
				const auto& act = *(raw_actions + cur_i);
				
				std::cout << "[Maniac::Play] Executing Action: Key '" << act.key 
				          << "' (" << (act.down ? "DOWN" : "UP") 
				          << ") at Live Time: " << cur_time 
				          << " ms (Target: " << act.time << " ms)" << std::endl;

				(raw_actions + cur_i)->execute();
				cur_i++;
			}

			std::this_thread::sleep_for(std::chrono::nanoseconds(100));
		}

		std::cout << "[Maniac::Play] All actions executed successfully." << std::endl;
	}

    std::vector<Action> to_actions(std::vector<osu::HitObject> &hit_objects, int32_t min_time) {
        if (hit_objects.empty()) {
            return {};
        }

        const auto columns = std::max_element(hit_objects.begin(),
                                              hit_objects.end(), [](auto a, auto b) {
                    return a.column < b.column; })->column + 1;

        std::cout << "[Maniac::to_actions] Detected max column index: " << (columns - 1) 
                  << " (Total columns: " << columns << ")" << std::endl;

        auto keys = osu::Osu::get_key_subset(config.keys, columns);
        std::cout << "[Maniac::to_actions] Assigned key subset: \"" << keys << "\"" << std::endl;

        if (config.mirror_mod)
            std::reverse(keys.begin(), keys.end());

        std::vector<Action> actions;
        actions.reserve(hit_objects.size() * 2);

        for (auto &hit_object : hit_objects) {
            if (hit_object.start_time < min_time)
                continue;

            if (!hit_object.is_slider)
                hit_object.end_time = hit_object.start_time + config.tap_time;

            if (hit_object.column >= 0 && static_cast<size_t>(hit_object.column) < keys.size()) {
                actions.emplace_back(keys[hit_object.column], true,
                    hit_object.start_time + config.compensation_offset);
                actions.emplace_back(keys[hit_object.column], false,
                    hit_object.end_time + config.compensation_offset);
            } else {
                std::cout << "[Maniac::to_actions] Warning: Note with invalid column index: " 
                          << hit_object.column << std::endl;
            }
        }

        debug("converted %d hit objects to %d actions", hit_objects.size(), actions.size());
        std::cout << "[Maniac::to_actions] Converted " << hit_objects.size() 
                  << " hit objects to " << actions.size() << " raw actions." << std::endl;

        std::sort(actions.begin(), actions.end());
        actions.erase(std::unique(actions.begin(), actions.end()), actions.end());

        std::cout << "[Maniac::to_actions] Final sorted unique action count: " << actions.size() << std::endl;

        return actions;
    }
}
