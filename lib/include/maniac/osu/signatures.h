#pragma once

#include <maniac/signature.h>

namespace signatures {
    // Song Time (using the full 8-byte anchor to guarantee uniqueness)
    const auto time = Signature{ "B8 01 00 00 00 EB 00 A3 ? ? ? ?", 8 };

    // Player / Ruleset pointer
    const auto player = Signature{ "FF 50 0C A1 ? ? ? ? 85 C0 74 ? 8B 40", 4 };

    // Osu Status / Game Mode
    const auto status = Signature{ "A1 ? ? ? ? 8D 50 ? A1 ? ? ? ? 85 C0 74 ? 8B 40", 1 };
}
