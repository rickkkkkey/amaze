#include <maniac/common.h>
#include <maniac/maniac.h>
#include <cmath>
#include <random>
#include <algorithm>
#include <unordered_map>
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

void maniac::humanize_dynamic(std::vector<osu::HitObject> &hit_objects, int modifier) {
    if (modifier <= 0 || hit_objects.empty()) {
        return;
    }

    const double mod_scale = static_cast<double>(modifier) / 100.0;
    constexpr int32_t density_window_ms = 1000;
    constexpr double min_stddev = 1.0;
    constexpr double max_stddev = 12.0;
    constexpr double base_correlation = 0.35;
    constexpr double correlation_half_life_ms = 250.0;
    constexpr double jack_late_bias_scale = 0.6;
    constexpr double release_correlation = 0.4;
    constexpr double hold_correlation_half_life_ms = 400.0;

    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<int32_t> original_start_times(hit_objects.size());
    for (size_t i = 0; i < hit_objects.size(); ++i) {
        original_start_times[i] = hit_objects[i].start_time;
    }

    int max_col = 0;
    for (const auto &obj : hit_objects) {
        if (obj.column > max_col) max_col = obj.column;
    }
    const int total_columns = max_col + 1;
    const int center_column = total_columns / 2;
    const bool has_thumb_column = (total_columns % 2 == 1) && (total_columns >= 3);

    auto hand_of = [total_columns, center_column, has_thumb_column](int32_t column) -> int {
        if (total_columns <= 1) return 0;
        if (column < 0 || column >= total_columns) return 0;
        if (has_thumb_column && column == center_column) return 2;
        return (column < center_column) ? 0 : 1;
    };

    struct HandState {
        double prev_offset = 0.0;
        int32_t prev_note_time = 0;
        bool initialized = false;
    };
    HandState hands[3];

    std::unordered_map<int32_t, int32_t> last_column_time;

    for (size_t i = 0; i < hit_objects.size(); ++i) {
        auto &cur = hit_objects[i];
        const int hand = std::clamp(hand_of(cur.column), 0, 2);
        auto &state = hands[hand];

        // 1. Density Window Calculation
        int local_density = 0;
        for (int j = static_cast<int>(i) - 1; j >= 0; --j) {
            if (original_start_times[i] - original_start_times[j] > (density_window_ms / 2)) break;
            local_density++;
        }
        for (size_t j = i + 1; j < hit_objects.size(); ++j) {
            if (original_start_times[j] - original_start_times[i] > (density_window_ms / 2)) break;
            local_density++;
        }

        // 2. Evaluate Jack Strain per column
        double jack_strain = 0.0;
        if (last_column_time.find(cur.column) != last_column_time.end()) {
            const int32_t col_delta = cur.start_time - last_column_time[cur.column];
            if (col_delta > 0 && col_delta < 250) {
                jack_strain = (250.0 - col_delta) / 50.0;
            }
        }
        last_column_time[cur.column] = cur.start_time;

        // 3. Dynamic StdDev with Clamping
        const double base_stddev = 2.5 * mod_scale;
        const double raw_stddev = base_stddev + (std::sqrt(local_density) * 1.8 * mod_scale) + (jack_strain * 1.2 * mod_scale);
        const double dynamic_stddev = std::clamp(raw_stddev, min_stddev, max_stddev);
        const double jack_late_shift = jack_strain * jack_late_bias_scale * mod_scale;

        std::normal_distribution<> hit_distr{jack_late_shift, dynamic_stddev};
        const double raw_sample = hit_distr(gen);

        // 4. Time-Decayed Autoregressive Correlation (per hand)
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

        // 5. Humanize Long Note Releases
        if (cur.is_slider) {
            const double release_stddev = std::clamp(dynamic_stddev * 1.25, min_stddev, max_stddev);
            std::normal_distribution<> release_distr{jack_late_shift, release_stddev};
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
