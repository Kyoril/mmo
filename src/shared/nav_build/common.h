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

            WorldModel = 1 << 4,

            /// Cut out under a door's footprint, so world instances can block it while the door
            /// is closed. Must equal nav::DoorPolyFlag.
            Door = 1 << 5,
        };
    }
}