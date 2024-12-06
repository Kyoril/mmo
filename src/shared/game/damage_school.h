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

            Crit = 1,

            Crushing,
        };
    }

    typedef damage_flags::Type DamageFlags;

    namespace melee_attack_outcome
    {
	    enum Type
	    {
			/// Target is immune to attacks and evaded it completely. 100% of the damage are evaded.
            Evade,

			/// The attacker missed the target (hit chance was too low). 100% of the damage are missed.
            Miss,

            /// THe target dodged the attack (dodge chance was high enough). 100% of the damage are dodged.
            Dodge,

			/// The target blocked the attack (block chance was high enough). Damage is reduced by the block value.
            Block,

			/// The target parried the attack (parry chance was high enough). 100% of the damage is parried.
            Parry,

            /// The attack barely hit the target (target level was too high). Most of the damage is missing.
            Glancing,

			/// The attack was a critical hit. Damage is doubled.
            Crit,

			/// The attack was a crushing hit (target level was too low). The damage is increased by 300%.
            Crushing,

            /// The attack was a normal hit for 100% damage.
            Normal
	    };
    }

	typedef melee_attack_outcome::Type MeleeAttackOutcome;
}
