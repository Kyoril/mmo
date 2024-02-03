#pragma once

#include "game_object_c.h"
#include "movement_info.h"

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
		[[nodiscard]] const MovementInfo& GetMovementInfo() const noexcept { return m_movementInfo; }

	protected:
		MovementInfo m_movementInfo;

		float m_movementAnimationTime = 0.0f;
		std::unique_ptr<Animation> m_movementAnimation;
		Vector3 m_movementStart;
		Vector3 m_movementEnd;
	};
}
