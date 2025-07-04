#pragma once

#include "connection.h"
#include "game_script.h"

namespace mmo
{
	/// @brief The Movement class handles the movement system for the game, processing updates and managing movers.
	class Movement : public NonCopyable
	{
	public:

		/// @brief Initializes the movement system, connecting to the event loop for updates.
		void Initialize();

		/// @brief Cleans up the movement system, disconnecting from the event loop.
		void Terminate();

		/// @brief Returns true if the movement system is initialized and ready to process updates.
		///	@returns true if initialized, false otherwise.
		[[nodiscard]] bool IsInitialized() const { return m_movementIdle.connected(); }

	private:
		/// @brief Handles the idle event to process movement updates.
		/// @param deltaSeconds The time since the last update in seconds.
		/// @param timestamp The current game time.
		void OnMovementIdle(float deltaSeconds, GameTime timestamp);

		[[nodiscard]] bool HasMovers() const;

		[[nodiscard]] bool HasLocalMover() const;

		/// @brief Moves all units based on their current movement state.
		void MoveUnits(GameTime timestamp, GameTime timeDiff);

		/// @brief Moves the local player based on the current game state.
		void MoveLocalPlayer(GameTime timestamp, GameTime timeDiff);

	private:
		scoped_connection m_movementIdle;

		GameTime m_lastMovementUpdate = 0;
	};
}
