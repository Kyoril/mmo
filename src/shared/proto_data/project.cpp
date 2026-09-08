#include "project.h"

namespace mmo
{
	namespace proto
	{
		bool SpellHasEffect(const SpellEntry& spell, mmo::SpellEffect type)
		{
			for (const auto& effect : spell.effects())
			{
				if (type == static_cast<mmo::SpellEffect>(effect.type()))
				{
					return true;
				}
			}
			return false;
		}

		bool SpellIsHarmful(const SpellEntry& spell)
		{
			for (const auto& effect : spell.effects())
			{
				switch (static_cast<mmo::SpellEffect>(effect.type()))
				{
				case spell_effects::SchoolDamage:
				case spell_effects::WeaponDamage:
				case spell_effects::WeaponDamageNoSchool:
				case spell_effects::WeaponPercentDamage:
				case spell_effects::HealthLeech:
				case spell_effects::EnvironmentalDamage:
				case spell_effects::InstantKill:
					return true;
				default:
					break;
				}
			}

			return false;
		}

		const CombatSettings& GetDefaultCombatSettings()
		{
			static const CombatSettings defaultSettings;
			return defaultSettings;
		}
	}
	
}

