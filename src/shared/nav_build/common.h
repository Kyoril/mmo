#pragma once

#include "base/typedefs.h"

namespace mmo
{
    enum PolyFlags : uint8
    {
        Ground = 1 << 0,

        Steep = 1 << 1,

        Liquid = 1 << 2,

        Entity = 1 << 3,
    };
}