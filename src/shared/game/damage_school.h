#pragma once
#include "base/typedefs.h"

namespace mmo
{
    enum class DamageSchool : ::uint8
    {
        Physical,
        Holy,
        Fire,
        Nature,
        Frost,
        Shadow,
        Arcane,
    };


    namespace damage_flags
    {
        enum Type
        {
            None = 0,

            Crit = 1
        };
    }

    typedef damage_flags::Type DamageFlags;
}
