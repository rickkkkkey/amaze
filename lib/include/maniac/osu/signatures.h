#pragma once

#include <maniac/signature.h>

namespace signatures {
    // Base anchor pattern (used for AudioTime)
    const auto base = Signature{ "F8 01 74 04 83 65", 0 };

    // Status pattern
    const auto status = Signature{ "48 83 F8 04 73 1E", 0 };

    // CurrentRuleset / Player pattern
    const auto ruleset = Signature{ "C7 86 48 01 00 00 01 00 00 00 A1", 0 };
}
