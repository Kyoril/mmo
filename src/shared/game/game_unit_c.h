#pragma once

#include "game_object_c.h"
#include "movement_info.h"
#include "shared/client_data/spells.pb.h"

namespace mmo
{
	class GameUnitC : public GameObjectC
	{
	public:
		explicit GameUnitC(Scene& scene)
			: GameObjectC(scene)
		{
		}

		virtual ~GameUnitC() override = default;

		virtual void Deserialize(io::Reader& reader, bool complete) override;

		virtual void Update(float deltaTime) override;

		template<typename T>
		T Get(const uint32 field) const
		{
			return m_fieldMap.GetFieldValue<T>(field);
		}

		virtual void InitializeFieldMap() override;

	public:
		void StartMove(bool forward);
		
		void StartStrafe(bool left);

		void StopMove();

		void StopStrafe();

		void StartTurn(bool left);

		void StopTurn();

		void SetFacing(const Radian& facing);

		void SetMovementPath(const std::vector<Vector3>& points);

	public:

		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		bool IsAlive() const { return GetHealth() > 0; }

	public:
		void SetInitialSpells(const std::vector<const proto_client::SpellEntry*>& spellIds);

		void LearnSpell(const proto_client::SpellEntry* spell);

		void UnlearnSpell(uint32 spellId);

		bool HasSpells() const noexcept { return !m_spells.empty(); }

		const proto_client::SpellEntry* GetSpell(uint32 index) const;

		uint32 GetSpellCount() const noexcept { return static_cast<uint32>(m_spells.size()); }

	public:
		[[nodiscard]] const MovementInfo& GetMovementInfo() const noexcept { return m_movementInfo; }

	protected:
		MovementInfo m_movementInfo;

		float m_movementAnimationTime = 0.0f;
		std::unique_ptr<Animation> m_movementAnimation;
		Vector3 m_movementStart;
		Vector3 m_movementEnd;
		std::vector<const proto_client::SpellEntry*> m_spells;
	};
}
