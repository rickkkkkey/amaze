#include "window.h"
#include "config.h"
#include <maniac/maniac.h>
#include <windows.h>
#include <iostream>
#include <cstdio>

static void help_marker(const char *desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void horizontal_break() {
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    ImGui::Separator();

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
}

static void set_priority_class(int priority) {
    const auto proc = GetCurrentProcess();
    const auto old_priority = GetPriorityClass(proc);

    SetPriorityClass(proc, priority);

    debug("changed priority class from 0x%lx to 0x%lx", old_priority,
            GetPriorityClass(proc));
}

int main(int, char **) {
    // Open a debug console window alongside the UI
    AllocConsole();
    FILE *fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    std::cout << "=== Maniac Debug Console Initialized ===" << std::endl;

    std::string message;

    config::read_from_file(maniac::config);

    auto run = [&message](osu::Osu &osu) {
        maniac::osu = &osu;

        message = "waiting for beatmap...";
        std::cout << "[Maniac] Waiting for playing status (current status: " << osu.get_status() << ")..." << std::endl;

        maniac::block_until_playing();

        message = "found beatmap";
        std::cout << "[Maniac] Entered gameplay! Resolving hit objects..." << std::endl;

        std::vector<osu::HitObject> hit_objects;

        for (int i = 0; i < 10; i++) {
            try {
                hit_objects = osu.get_hit_objects();
                if (!hit_objects.empty()) {
                    break;
                }
            } catch (std::exception &err) {
                debug("get hit objects attempt %d failed: %s", i + 1, err.what());
                std::cout << "[Maniac] Attempt " << (i + 1) << " to get hit objects failed: " << err.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::cout << "[Maniac] Loaded " << hit_objects.size() << " hit objects from memory." << std::endl;

        if (hit_objects.empty()) {
            throw std::runtime_error("failed getting hit objects (list was empty)");
        }

        set_priority_class(HIGH_PRIORITY_CLASS);

        maniac::randomize(hit_objects, maniac::config.randomization_mean, maniac::config.randomization_stddev);

        if (maniac::config.humanization_type == maniac::config::STATIC_HUMANIZATION) {
            maniac::humanize_static(hit_objects, maniac::config.humanization_modifier);
        }

        if (maniac::config.humanization_type == maniac::config::DYNAMIC_HUMANIZATION) {
            maniac::humanize_dynamic(hit_objects, maniac::config.humanization_modifier);
        }

        const auto start_time = osu.get_game_time();
        std::cout << "[Maniac] Converting to actions at current game time: " << start_time << " ms" << std::endl;

        auto actions = maniac::to_actions(hit_objects, start_time);
        std::cout << "[Maniac] Generated " << actions.size() << " key actions. Starting playback loop..." << std::endl;

        message = "playing";

        maniac::play(actions);

        std::cout << "[Maniac] Finished playing current map." << std::endl;

        set_priority_class(NORMAL_PRIORITY_CLASS);
    };

    auto thread = std::jthread([&message, &run](const std::stop_token& token) {
        while (!token.stop_requested()) {
            try {
                std::cout << "[Maniac] Scanning for osu! process and signatures..." << std::endl;
                auto osu = osu::Osu();
                std::cout << "[Maniac] Connected to osu! process." << std::endl;

                while (!token.stop_requested()) {
                    run(osu);
                }
            } catch (std::exception &err) {
                (message = err.what()).append(" (retrying in 2 seconds)");
                std::cout << "[Maniac Error] " << err.what() << " (retrying in 2s)" << std::endl;

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    });

    window::start([&message] {
        ImGui::Begin("maniac", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::Text("Status: %s", message.c_str());
        horizontal_break();

        ImGui::Combo("Humanization Type", &maniac::config.humanization_type, "Static\0Dynamic (new)\0\0");
        ImGui::SameLine();
        help_marker("Static: Density calculated per 1s chunk and applied to all hit objects in that chunk. Dynamic: Density 1s 'in front' of each hit object, applied individually.");

        ImGui::InputInt("Humanization", &maniac::config.humanization_modifier, 0, 1000);
        ImGui::SameLine();
        help_marker("Advanced hit-time randomization based on hit density.");

        horizontal_break();

        ImGui::Text("Adds a random hit-time offset generated using a normal \ndistribution with given mean and standard deviation.");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        ImGui::InputInt("Randomization Mean", &maniac::config.randomization_mean);
        ImGui::InputInt("Randomization Stddev", &maniac::config.randomization_stddev);

        horizontal_break();

        ImGui::InputInt("Compensation", &maniac::config.compensation_offset);
        ImGui::SameLine();
        help_marker("Adds constant value to all hit-times to compensate for input latency, slower processors, etc.");

        ImGui::Checkbox("Mirror Mod", &maniac::config.mirror_mod);

        ImGui::InputInt("Tap time", &maniac::config.tap_time);
        ImGui::SameLine();
        help_marker("How long a key is held down for a single keypress, in milliseconds.");

        horizontal_break();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        ImGui::TextDisabled("maniac by fs-c, https://github.com/fs-c/maniac");

        ImGui::End();
    });

    config::write_to_file(maniac::config);

    thread.request_stop();

    return EXIT_SUCCESS;
}
