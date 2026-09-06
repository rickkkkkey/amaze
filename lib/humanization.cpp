#include <maniac/common.h>
#include <maniac/maniac.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

void maniac::randomize(std::vector<osu::HitObject> &hit_objects, int mean, int stddev) {
    if (stddev <= 0 && mean == 0) {
        return;
    }

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::normal_distribution<> distr{static_cast<double>(mean), static_cast<double>(stddev)};

    for (auto &hit_object : hit_objects) {
        const auto offset = static_cast<int32_t>(std::round(distr(gen)));
        hit_object.start_time += offset;
        if (hit_object.is_slider) {
            hit_object.end_time += offset;
        }
    }

    debug("randomized %zu hit objects with mean %d and stddev %d",
          hit_objects.size(), mean, stddev);
}

void maniac::humanize_dynamic(std::vector<osu::HitObject> &hit_objects, int modifier,
                             double judgment_window_ms, double approach_time_ms) {
    if (modifier <= 0 || hit_objects.empty()) {
        return;
    }

    const double mod_scale = static_cast<double>(modifier) / 100.0;
    constexpr int32_t density_window_ms = 1000;
    constexpr double min_stddev = 1.0;
    constexpr double pattern_max_stddev = 12.0;
    constexpr double absolute_max_stddev = 22.0;
    constexpr double base_correlation = 0.35;
    constexpr double correlation_half_life_ms = 250.0;
    constexpr double jack_late_bias_scale = 0.6;
    constexpr double release_correlation = 0.4;
    constexpr double hold_correlation_half_life_ms = 400.0;

    // --- pattern-complexity tuning ---
    constexpr double density_weight = 0.2;
    constexpr int32_t chord_epsilon_ms = 2;
    constexpr double chord_weight = 1.6;
    constexpr size_t transition_window = 8;
    constexpr double irregularity_weight = 2.5;
    constexpr double jack_weight = 1.2;

    // --- bracket / near-miss chord tuning ---
    constexpr int32_t bracket_epsilon_ms = 40;
    constexpr double bracket_weight = 0.9;

    // --- fatigue tuning ---
    constexpr double fatigue_recovery_half_life_ms = 180.0;
    constexpr double column_fatigue_weight = 3.0;
    constexpr double hand_fatigue_weight = 1.5;
    constexpr double column_fatigue_late_shift = 0.35;
    constexpr double comfortable_column_repeat_ms = 250.0;
    constexpr int32_t comfortable_alternation_ms = 130;
    constexpr double fresh_finger_efficiency = 0.25;
    constexpr size_t hand_recent_window = 3;

    // Anchor strain: a hand playing other notes while holding an LN
    constexpr double anchor_strain_multiplier = 1.6;

    // --- release cognitive-load tuning ---
    constexpr int32_t release_sync_epsilon_ms = 25;
    constexpr double release_cognitive_weight = 2.0;
    constexpr double release_cognitive_late_shift = 1.5;

    // --- psychological tuning ---
    constexpr double max_nerve_scaling = 1.30;
    constexpr double inattention_floor_stddev = 4.0;
    constexpr double choke_threshold = 16.0;
    constexpr double choke_exponent = 1.5;

    // --- judgment-window / reading-load awareness ---
    constexpr double reference_judgment_window_ms = 34.0;
    constexpr double reference_approach_time_ms = 600.0;
    const double window_scale = std::max(0.1, judgment_window_ms / reference_judgment_window_ms);
    const double scaled_pattern_max_stddev = pattern_max_stddev * window_scale;
    const double scaled_absolute_max_stddev = absolute_max_stddev * window_scale;
    const double reading_load_scale = reference_approach_time_ms / std::max(1.0, approach_time_ms);

    thread_local std::mt19937 gen{std::random_device{}()};

    std::vector<int32_t> original_start_times(hit_objects.size());
    std::vector<int32_t> slider_end_times;
    for (size_t i = 0; i < hit_objects.size(); ++i) {
        original_start_times[i] = hit_objects[i].start_time;
        if (hit_objects[i].is_slider) {
            slider_end_times.push_back(hit_objects[i].end_time);
        }
    }
    std::sort(slider_end_times.begin(), slider_end_times.end());

    int max_col = 0;
    for (const auto &obj : hit_objects) {
        if (obj.column > max_col) max_col = obj.column;
    }
    const int total_columns = max_col + 1;
    const int center_column = total_columns / 2;
    const bool has_thumb_column = (total_columns % 2 == 1) && (total_columns >= 3);
    const int irregularity_norm = std::max(1, std::min(static_cast<int>(transition_window), total_columns));

    auto hand_of = [total_columns, center_column, has_thumb_column](int32_t column) -> int {
        if (total_columns <= 1) return 0;
        if (column < 0 || column >= total_columns) return 0;
        if (has_thumb_column && column == center_column) return 2;
        return (column < center_column) ? 0 : 1;
    };

    auto finger_weakness = [total_columns](int32_t column) -> double {
        if (total_columns <= 3) return 1.0;
        const double center = (total_columns - 1) / 2.0;
        const double dist_from_center = std::abs(static_cast<double>(column) - center);
        const double normalized_dist = dist_from_center / center;
        return 1.0 + (normalized_dist * 0.6);
    };

    struct HandState {
        double prev_offset = 0.0;
        int32_t prev_note_time = 0;
        bool initialized = false;
    };
    HandState hands[3];

    std::vector<int32_t> last_column_time(total_columns, std::numeric_limits<int32_t>::min());
    std::vector<double> column_fatigue(total_columns, 0.0);

    double hand_fatigue[3] = {0.0, 0.0, 0.0};
    int32_t last_hand_time[3] = {
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min()
    };
    std::array<std::array<int32_t, hand_recent_window>, 3> hand_recent_columns{};
    std::array<size_t, 3> hand_recent_count{0, 0, 0};
    std::array<size_t, 3> hand_recent_head{0, 0, 0};

    int32_t hand_active_hold_until[3] = {
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::min()
    };

    std::array<int32_t, transition_window> transition_history = {0};
    size_t transition_head = 0;
    size_t transition_count = 0;
    int32_t last_distinct_time = std::numeric_limits<int32_t>::min();
    int32_t last_distinct_column = -1;

    const size_t n = hit_objects.size();
    for (size_t i = 0; i < n; ++i) {
        auto &cur = hit_objects[i];
        const int hand = std::clamp(hand_of(cur.column), 0, 2);
        auto &state = hands[hand];
        const double anatomy_multiplier = finger_weakness(cur.column);
        const int32_t safe_column = std::clamp(cur.column, 0, total_columns - 1);

        // 1. Local density
        int local_density = 0;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (original_start_times[i] - original_start_times[j] > (density_window_ms / 2)) break;
            local_density++;
        }
        for (size_t j = i + 1; j < n; ++j) {
            if (original_start_times[j] - original_start_times[i] > (density_window_ms / 2)) break;
            local_density++;
        }

        // 2 & 2b. Chord and bracket detection
        int chord_count = 0;
        int bracket_count = 0;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            const int32_t dt = original_start_times[i] - original_start_times[j];
            if (dt > bracket_epsilon_ms) break;
            if (dt <= chord_epsilon_ms) chord_count++;
            else if (hit_objects[j].column != cur.column) bracket_count++;
        }
        for (size_t j = i + 1; j < n; ++j) {
            const int32_t dt = original_start_times[j] - original_start_times[i];
            if (dt > bracket_epsilon_ms) break;
            if (dt <= chord_epsilon_ms) chord_count++;
            else if (hit_objects[j].column != cur.column) bracket_count++;
        }

        // 3. Pattern irregularity
        const bool is_new_time_step = (original_start_times[i] != last_distinct_time);
        if (is_new_time_step && last_distinct_column != -1) {
            const int32_t transition_key = last_distinct_column * 1000 + cur.column;
            transition_history[transition_head] = transition_key;
            transition_head = (transition_head + 1) % transition_window;
            transition_count = std::min(transition_count + 1, transition_window);
        }
        if (is_new_time_step) {
            last_distinct_time = original_start_times[i];
            last_distinct_column = cur.column;
        }
        double pattern_irregularity = 0.0;
        if (transition_count > 0) {
            int unique_transitions = 0;
            for (size_t k = 0; k < transition_count; ++k) {
                bool is_unique = true;
                for (size_t m = 0; m < k; ++m) {
                    if (transition_history[k] == transition_history[m]) {
                        is_unique = false;
                        break;
                    }
                }
                if (is_unique) unique_transitions++;
            }
            pattern_irregularity = static_cast<double>(unique_transitions) / static_cast<double>(irregularity_norm);
        }

        // 4. Instantaneous jack strain + column fatigue
        double jack_strain = 0.0;
        double column_fatigue_value = 0.0;
        {
            const int32_t prev_time = last_column_time[safe_column];
            const bool found = (prev_time != std::numeric_limits<int32_t>::min());
            double decay = 0.0;
            double col_increment = anatomy_multiplier;
            if (found) {
                const int32_t col_delta = cur.start_time - prev_time;
                if (col_delta > 0 && col_delta < 250) {
                    jack_strain = (250.0 - col_delta) / 50.0;
                }
                decay = std::exp(-static_cast<double>(std::max(0, col_delta)) / fatigue_recovery_half_life_ms);
                const double clamped_delta = static_cast<double>(std::max(0, col_delta));
                col_increment = (clamped_delta < comfortable_column_repeat_ms)
                    ? anatomy_multiplier * (1.0 - clamped_delta / comfortable_column_repeat_ms)
                    : 0.0;
            }
            column_fatigue[safe_column] = column_fatigue[safe_column] * decay + col_increment;
            column_fatigue_value = column_fatigue[safe_column];
        }
        last_column_time[safe_column] = cur.start_time;

        // 4b. Anchor strain check
        const bool is_anchored = (original_start_times[i] < hand_active_hold_until[hand]);
        if (cur.is_slider) {
            hand_active_hold_until[hand] = std::max(hand_active_hold_until[hand], cur.end_time);
        }

        // 4c. Hand fatigue
        double hand_fatigue_value;
        {
            double &hf = hand_fatigue[hand];
            const double hand_dt = (last_hand_time[hand] == std::numeric_limits<int32_t>::min())
                ? static_cast<double>(comfortable_alternation_ms) + 1.0
                : static_cast<double>(std::max(0, cur.start_time - last_hand_time[hand]));
            const double decay = std::exp(-hand_dt / fatigue_recovery_half_life_ms);

            double base_increment = 0.0;
            if (hand_dt < comfortable_alternation_ms) {
                base_increment = 1.0 - (hand_dt / static_cast<double>(comfortable_alternation_ms));
            }

            bool is_fresh_finger = true;
            const size_t recent_n = hand_recent_count[hand];
            for (size_t k = 0; k < recent_n; ++k) {
                if (hand_recent_columns[hand][k] == cur.column) {
                    is_fresh_finger = false;
                    break;
                }
            }
            const double hand_increment = base_increment * (is_fresh_finger ? fresh_finger_efficiency : 1.0);

            hf = hf * decay + hand_increment;
            hand_fatigue_value = hf;
            last_hand_time[hand] = cur.start_time;

            hand_recent_columns[hand][hand_recent_head[hand]] = cur.column;
            hand_recent_head[hand] = (hand_recent_head[hand] + 1) % hand_recent_window;
            hand_recent_count[hand] = std::min(hand_recent_count[hand] + 1, hand_recent_window);
        }

        // 5. Dynamic stddev
        double base_stddev = 2.5 * mod_scale;
        if (local_density < 3) {
            base_stddev = std::max(base_stddev, inattention_floor_stddev * mod_scale);
        }

        const double density_term = std::sqrt(local_density) * density_weight * reading_load_scale;
        const double chord_term = chord_count * chord_weight;
        const double bracket_term = bracket_count * bracket_weight;
        const double irregularity_term = pattern_irregularity * irregularity_weight;
        const double jack_term = jack_strain * jack_weight;

        double raw_pattern_stress = density_term + chord_term + bracket_term + irregularity_term + jack_term;
        raw_pattern_stress = std::min(raw_pattern_stress, scaled_pattern_max_stddev);

        const double anchor_multiplier = is_anchored ? anchor_strain_multiplier : 1.0;
        const double raw_fatigue_stress = (column_fatigue_value * column_fatigue_weight)
                                         + (hand_fatigue_value * hand_fatigue_weight * anchor_multiplier);
        double total_stress = raw_pattern_stress + raw_fatigue_stress;

        if (total_stress > choke_threshold) {
            const double choke_multiplier = std::pow(total_stress / choke_threshold, choke_exponent);
            total_stress *= choke_multiplier;
        }

        double dynamic_stddev = base_stddev + (total_stress * mod_scale);

        const double map_progress = static_cast<double>(i) / static_cast<double>(n);
        const double nerve_scale = 1.0 + (map_progress * map_progress * (max_nerve_scaling - 1.0));
        dynamic_stddev *= nerve_scale;

        dynamic_stddev = std::clamp(dynamic_stddev, min_stddev, scaled_absolute_max_stddev);

        const double jack_late_shift = (jack_strain * jack_late_bias_scale
                                       + column_fatigue_value * column_fatigue_late_shift) * mod_scale;

        std::normal_distribution<> hit_distr{jack_late_shift, dynamic_stddev};
        const double raw_sample = hit_distr(gen);

        // 6. Time-decayed autoregressive correlation, per hand
        double correlated_offset = raw_sample;
        if (state.initialized) {
            const int32_t dt = std::max(0, cur.start_time - state.prev_note_time);
            const double active_correlation = base_correlation * std::exp(-static_cast<double>(dt) / correlation_half_life_ms);
            correlated_offset = (active_correlation * state.prev_offset) + ((1.0 - active_correlation) * raw_sample);
        } else {
            state.initialized = true;
        }

        state.prev_offset = correlated_offset;
        state.prev_note_time = cur.start_time;

        const auto start_offset = static_cast<int32_t>(std::round(correlated_offset));
        cur.start_time += start_offset;

        // 7. Slider release
        if (cur.is_slider) {
            int release_sync_count = 0;

            const auto start_it = std::lower_bound(original_start_times.begin(), original_start_times.end(), cur.end_time);
            const size_t start_pos = static_cast<size_t>(std::distance(original_start_times.begin(), start_it));
            for (size_t k = start_pos; k < n; ++k) {
                if (original_start_times[k] - cur.end_time > release_sync_epsilon_ms) break;
                if (k != i) release_sync_count++;
            }
            for (int k = static_cast<int>(start_pos) - 1; k >= 0; --k) {
                if (cur.end_time - original_start_times[k] > release_sync_epsilon_ms) break;
                if (static_cast<size_t>(k) != i) release_sync_count++;
            }

            const auto end_it = std::lower_bound(slider_end_times.begin(), slider_end_times.end(), cur.end_time);
            const size_t end_pos = static_cast<size_t>(std::distance(slider_end_times.begin(), end_it));
            for (size_t k = end_pos; k < slider_end_times.size(); ++k) {
                if (slider_end_times[k] - cur.end_time > release_sync_epsilon_ms) break;
                release_sync_count++;
            }
            for (int k = static_cast<int>(end_pos) - 1; k >= 0; --k) {
                if (cur.end_time - slider_end_times[k] > release_sync_epsilon_ms) break;
                release_sync_count++;
            }
            release_sync_count = std::max(0, release_sync_count - 1);

            const double cognitive_term = release_sync_count * release_cognitive_weight * mod_scale;
            double release_stddev = (dynamic_stddev * 1.25) + cognitive_term;
            release_stddev = std::clamp(release_stddev, min_stddev, scaled_absolute_max_stddev);

            const double release_mean_shift = jack_late_shift + release_sync_count * release_cognitive_late_shift * mod_scale;

            std::normal_distribution<> release_distr{release_mean_shift, release_stddev};
            const double release_sample = release_distr(gen);

            const int32_t hold_duration = std::max(0, cur.end_time - original_start_times[i]);
            const double hold_decay = std::exp(-static_cast<double>(hold_duration) / hold_correlation_half_life_ms);
            const double effective_release_correlation = release_correlation * hold_decay;

            const double correlated_release = (effective_release_correlation * correlated_offset) + ((1.0 - effective_release_correlation) * release_sample);
            const auto end_offset = static_cast<int32_t>(std::round(correlated_release));
            cur.end_time += end_offset;

            if (cur.end_time <= cur.start_time) {
                cur.end_time = cur.start_time + 30;
            }
        }
    }

    debug("dynamically humanized %zu hit objects with modifier %d", hit_objects.size(), modifier);
}
