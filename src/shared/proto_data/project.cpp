#include "project.h"

#include "game/aura.h"

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
				case spell_effects::PowerDrain:
					return true;

				// An aura is harmful or not depending on what it applies, and the effect type alone
				// cannot tell them apart - the same ApplyAura carries a heal-over-time and a
				// damage-over-time. Judging by effect type only would leave every DoT and every stun
				// usable on a friendly target, which is most of the ways to actually ruin someone.
				case spell_effects::ApplyAura:
				case spell_effects::ApplyAreaAura:
				case spell_effects::PersistentAreaAura:
					switch (static_cast<aura_type::Type>(effect.aura()))
					{
					case aura_type::PeriodicDamage:
					case aura_type::ModRoot:
					case aura_type::ModSleep:
					case aura_type::ModStun:
					case aura_type::ModFear:
					case aura_type::ModDisorient:
						return true;
					default:
						break;
					}
					break;

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

