#pragma once

#include "base/typedefs.h"

namespace mmo
{
    namespace poly_flags
    {
        enum Type : uint8
        {
            Ground = 1 << 0,

            Steep = 1 << 1,

            Liquid = 1 << 2,

            Entity = 1 << 3,
        };
    }
}