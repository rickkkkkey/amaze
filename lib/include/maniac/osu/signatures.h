#pragma once

#include <maniac/signature.h>

namespace signatures {
    const auto time = Signature{ "DB 5D E8 8B 45 E8 A1 ? ? ? ? 8D 50", 7 };
    const auto player = Signature{ "C7 86 48 01 00 00 01 00 00 00 A1 ? ? ? ?", 11 };
    const auto status = Signature{ "83 3D ? ? ? ? 00 7E ? A1 ? ? ? ? 89", 2 };
}
