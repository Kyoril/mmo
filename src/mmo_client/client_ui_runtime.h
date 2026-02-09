#pragma once

#include "base/non_copyable.h"
#include "base/signal.h"
#include "frame_ui/localization.h"

namespace mmo
{
	class Cursor;
	class GameScript;
	class Minimap;

	/// @brief Manages frame-ui lifecycle and related client UI bindings.
	///
	/// This runtime owns localization loading for frame-ui, registers built-in
	/// UI frame factories/renderers, binds UI-specific Lua APIs, and wires
	/// input/update events during the active client session.
	class ClientUiRuntime final : public NonCopyable
	{
	public:
		ClientUiRuntime() = default;
		~ClientUiRuntime() = default;

	public:
		/// @brief Loads localization data used by frame-ui.
		/// @return True if localization data was loaded successfully.
		bool LoadLocalization();

		/// @brief Returns the currently loaded localization instance.
		/// @return Reference to localization data used by the UI runtime.
		const Localization& GetLocalization() const { return m_localization; }

		/// @brief Initializes frame-ui systems and UI Lua bindings.
		/// @param gameScript Script host used to access the Lua state.
		/// @param minimap Minimap instance exposed to UI frame factories.
		/// @param cursor Cursor manager for default cursor setup.
		/// @return True if initialization succeeded.
		bool Initialize(GameScript& gameScript, Minimap& minimap, Cursor& cursor);

		/// @brief Shuts down frame-ui and disconnects all runtime UI bindings.
		void Shutdown();

		/// @brief Returns whether frame-ui was initialized by this runtime.
		/// @return True if Initialize was called successfully and not yet shut down.
		bool IsInitialized() const { return m_initialized; }

	private:
		bool m_initialized = false;
		Localization m_localization;
		scoped_connection_container m_connections;
	};
}
