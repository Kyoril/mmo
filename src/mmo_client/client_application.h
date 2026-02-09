#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	/// @brief Orchestrates client startup and shutdown lifecycle.
	///
	/// This class encapsulates the previous global init/destroy flow and keeps
	/// application bootstrap logic separate from platform entry points.
	class ClientApplication final : public NonCopyable
	{
	public:
		ClientApplication() = default;
		~ClientApplication() = default;

	public:
		/// @brief Starts all required client systems.
		/// @return True if startup completed successfully.
		bool Start();

		/// @brief Stops and tears down all previously started client systems.
		void Stop();

		/// @brief Returns whether the application is currently started.
		/// @return True if Start succeeded and Stop has not been called.
		bool IsStarted() const { return m_started; }

	private:
		bool m_started = false;
	};
}
