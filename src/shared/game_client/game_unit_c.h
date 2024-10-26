#pragma once

#include "game_object_c.h"
#include "game/movement_info.h"
#include "shared/client_data/spells.pb.h"
#include "game/creature_data.h"

namespace mmo
{
	class GameItemC;
	class GameUnitC;
	class GamePlayerC;

	/// @brief An interface for handling client network events related to units.
	class NetClient : public NonCopyable
	{
	public:
		virtual ~NetClient() = default;

	public:
		/// @brief An attack start request happened.
		virtual void SendAttackStart(const uint64 victim, const GameTime timestamp) = 0;

		/// @brief An attack stop request happened.
		virtual void SendAttackStop(const GameTime timestamp) = 0;

		virtual void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) = 0;

		virtual void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) = 0;

		virtual void GetItemData(uint64 guid, std::weak_ptr<GameItemC> item) = 0;
	};

	/// @brief Base class for a unit in the game client. A unit is a living object in the game world which can be interacted with,
	///	       participate in combat and more. All player characters are also units.
	class GameUnitC : public GameObjectC
	{
	public:
		/// @brief Creates a instance of the GameUnitC class.
		explicit GameUnitC(Scene& scene, NetClient& netDriver)
			: GameObjectC(scene)
			, m_netDriver(netDriver)
		{
		}

		virtual ObjectTypeId GetTypeId() const override
		{
			return ObjectTypeId::Unit;
		}

		/// @brief Destroys the instance of the GameUnitC class.
		virtual ~GameUnitC() override = default;

		/// @brief Deserializes the unit from the given reader.
		virtual void Deserialize(io::Reader& reader, bool complete) override;

		/// @brief Updates the unit. Should be called once per frame per unit.
		virtual void Update(float deltaTime) override;

		virtual void ApplyLocalMovement(float deltaTime);

		virtual void ApplyMovementInfo(const MovementInfo& movementInfo);

		/// @copydoc GameObjectC::InitializeFieldMap
		virtual void InitializeFieldMap() override;

	protected:
		virtual void SetupSceneObjects() override;

	public:
		/// @brief Starts moving the unit forward or backward.
		void StartMove(bool forward);

		/// @brief Starts strafing the unit to the left or right.
		void StartStrafe(bool left);

		/// @brief Stops moving the unit.
		void StopMove();

		/// @brief Stops strafing the unit.
		void StopStrafe();

		/// @brief Starts turning the unit to the left or right.
		void StartTurn(bool left);

		/// @brief Stops the unit from turning.
		void StopTurn();

		/// @brief Rotates the unit to face the specified direction.
		void SetFacing(const Radian& facing);

		/// @brief Makes the unit follow a given path of points.
		void SetMovementPath(const std::vector<Vector3>& points);

		void SetSpeed(const movement_type::Type type, float speed) { m_unitSpeed[type] = speed; }

		virtual void SetQueryMask(uint32 mask);

	public:
		/// @brief Returns the current health of this unit.
		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		/// @brief Gets the maximum health of this unit.
		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		/// @brief Returns whether the unit is currently alive.
		bool IsAlive() const { return GetHealth() > 0; }

		void SetTargetUnit(const std::shared_ptr<GameUnitC>& targetUnit);

	public:
		/// @brief Initializes the list of learned spells at once without notification for each single spell.
		void SetInitialSpells(const std::vector<const proto_client::SpellEntry*>& spellIds);

		/// @brief Adds a spell to the list of learned spells. Does nothing if this spell is already known.
		void LearnSpell(const proto_client::SpellEntry* spell);

		/// @brief Removes a spell from the list of learned spells. Does nothing if the spell is not known.
		void UnlearnSpell(uint32 spellId);

		/// @brief Returns whether this unit knows any spells at all.
		bool HasSpells() const noexcept { return !m_spells.empty(); }

		/// @brief Returns the spell at the specified index of learned spells of this unit. Returns nullptr if index is out of bounds.
		const proto_client::SpellEntry* GetSpell(uint32 index) const;

		/// @brief Returns the number of known spells of this unit.
		uint32 GetSpellCount() const noexcept { return static_cast<uint32>(m_spells.size()); }

		/// @brief Returns whether the unit is currently casting any spell.
		bool IsCastingSpell() const { return false; }

	public:
		/// @brief Returns whether the unit is currently attacking any unit.
		bool IsAttacking() const noexcept { return m_victim != 0; }

		/// @brief Returns whether the unit is currently attacking the specified unit.
		bool IsAttacking(const GameUnitC& victim) const noexcept { return victim.GetGuid() == m_victim; }

		/// @brief Returns whether the unit can attack right now at all.
		bool CanAttack() const noexcept { return false; }

		/// @brief Starts auto attacking the specified unit.
		void Attack(GameUnitC& victim);

		/// @brief Stops auto attacking.
		void StopAttack();

		bool IsInCombat() const { return (Get<uint32>(object_fields::Flags) & unit_flags::InCombat) != 0; }

		float GetSpeed(const movement_type::Type type) const { return m_unitSpeed[type]; }

		void SetCreatureInfo(const CreatureInfo& creatureInfo) { m_creatureInfo = creatureInfo; }

		const String& GetName() const override;

		void SetTargetAnimState(AnimationState* newTargetState);

	public:
		/// @brief Returns the current movement information of this unit.
		[[nodiscard]] const MovementInfo& GetMovementInfo() const noexcept { return m_movementInfo; }

	protected:
		NetClient& m_netDriver;
		MovementInfo m_movementInfo;

		float m_movementAnimationTime = 0.0f;
		std::unique_ptr<Animation> m_movementAnimation;
		Vector3 m_movementStart;
		Quaternion m_movementStartRot;
		Vector3 m_movementEnd;
		std::vector<const proto_client::SpellEntry*> m_spells;

		uint64 m_victim = 0;

		std::weak_ptr<GameUnitC> m_targetUnit;

		float m_unitSpeed[movement_type::Count];

		CreatureInfo m_creatureInfo;

	protected:
		// Animation stuff
		AnimationState* m_idleAnimState{ nullptr };
		AnimationState* m_runAnimState{ nullptr };
		AnimationState* m_deathState{ nullptr };

		AnimationState* m_targetState = nullptr;
		AnimationState* m_currentState = nullptr;
	};
}
