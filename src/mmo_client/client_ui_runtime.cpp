#include "client_ui_runtime.h"

#include "cursor.h"
#include "event_loop.h"
#include "game_script.h"
#include "log/default_log_levels.h"
#include "frame_ui/frame_mgr.h"
#include "ui/cooldown_frame.h"
#include "ui/minimap.h"
#include "ui/minimap_frame.h"
#include "ui/model_frame.h"
#include "ui/model_renderer.h"
#include "ui/unit_model_frame.h"

#include "luabind/iterator_policy.hpp"
#include "luabind/luabind.hpp"
#include "luabind/out_value_policy.hpp"

namespace mmo
{
	/// @copydoc ClientUiRuntime::LoadLocalization
	bool ClientUiRuntime::LoadLocalization()
	{
		if (!m_localization.LoadFromFile())
		{
			ELOG("Failed to initialize localization!");
			return false;
		}

		return true;
	}

	/// @copydoc ClientUiRuntime::Initialize
	bool ClientUiRuntime::Initialize(GameScript& gameScript, Minimap& minimap, Cursor& cursor)
	{
		if (m_initialized)
		{
			return true;
		}

		if (const auto window = GraphicsDevice::Get().GetAutoCreatedWindow())
		{
			m_connections += window->Resized.connect([](uint16 width, uint16 height)
				{
					FrameManager::Get().NotifyScreenSizeChanged(width, height);
				});
		}

		FrameManager::Initialize(&gameScript.GetLuaState(), m_localization);

		FrameManager::Get().RegisterFrameRenderer("ModelRenderer", [](const std::string& name)
			{
				return std::make_unique<ModelRenderer>(name);
			});

		FrameManager::Get().RegisterFrameFactory("Model", [](const std::string& name)
			{
				return std::make_shared<ModelFrame>(name);
			});
		FrameManager::Get().RegisterFrameFactory("UnitModel", [](const std::string& name)
			{
				return std::make_shared<UnitModelFrame>(name);
			});
		FrameManager::Get().RegisterFrameFactory("Minimap", [&minimap](const std::string& name)
			{
				return std::make_shared<MinimapFrame>(name, minimap);
			});
		FrameManager::Get().RegisterFrameFactory("Cooldown", [](const std::string& name)
			{
				return std::make_shared<CooldownFrame>(name);
			});

		cursor.LoadCursorTypeFromTexture(CursorType::Pointer, "Interface/Cursor/pointer001.htex");
		cursor.LoadCursorTypeFromTexture(CursorType::Interact, "Interface/Cursor/gears001.htex");
		cursor.LoadCursorTypeFromTexture(CursorType::Attack, "Interface/Cursor/sword001.htex");
		cursor.LoadCursorTypeFromTexture(CursorType::Loot, "Interface/Cursor/bag001.htex");
		cursor.LoadCursorTypeFromTexture(CursorType::Gossip, "Interface/Cursor/talk001.htex");
		cursor.SetCursorType(CursorType::Pointer);

		m_connections += EventLoop::Idle.connect([](float deltaSeconds, GameTime)
			{
				FrameManager::Get().Update(deltaSeconds);
			});

		m_connections += EventLoop::MouseMove.connect([](int32 x, int32 y)
			{
				FrameManager::Get().NotifyMouseMoved(Point(x, y));
				return false;
			});
		m_connections += EventLoop::MouseDown.connect([](EMouseButton button, int32 x, int32 y)
			{
				return FrameManager::Get().NotifyMouseDown(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			});
		m_connections += EventLoop::MouseUp.connect([](EMouseButton button, int32 x, int32 y)
			{
				return FrameManager::Get().NotifyMouseUp(static_cast<MouseButton>(1 << static_cast<int32>(button)), Point(x, y));
			});
		m_connections += EventLoop::KeyDown.connect([](int32 key, bool)
			{
				FrameManager::Get().NotifyKeyDown(key);
				return true;
			});
		m_connections += EventLoop::KeyChar.connect([](uint16 codepoint)
			{
				FrameManager::Get().NotifyKeyChar(codepoint);
				return false;
			});
		m_connections += EventLoop::KeyUp.connect([](int32 key)
			{
				FrameManager::Get().NotifyKeyUp(key);
				return false;
			});

		LUABIND_MODULE(&gameScript.GetLuaState(),
			luabind::scope(
				luabind::class_<ModelFrame, Frame>("ModelFrame")
				.def("SetModelFile", &ModelFrame::SetModelFile)
				.def("Yaw", &ModelFrame::Yaw)
				.def("SetZoom", &ModelFrame::SetZoom)
				.def("GetZoom", &ModelFrame::GetZoom)
				.def("GetYaw", &ModelFrame::GetYaw)
				.def("ResetYaw", &ModelFrame::ResetYaw)
				.def("InvalidateModel", &ModelFrame::InvalidateModel)
				.def("SetAutoRender", &ModelFrame::SetAutoRender)),

			luabind::scope(
				luabind::class_<UnitModelFrame, ModelFrame>("UnitModelFrame")
				.def("SetUnit", &UnitModelFrame::SetUnit)),

			luabind::scope(
				luabind::class_<CooldownFrame, Frame>("CooldownFrame")
				.def("SetProgress", &CooldownFrame::SetProgress)
				.def("GetProgress", &CooldownFrame::GetProgress)));

		m_initialized = true;
		return true;
	}

	/// @copydoc ClientUiRuntime::Shutdown
	void ClientUiRuntime::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_connections.disconnect();
		FrameManager::Get().RemoveFrameRenderer("ModelRenderer");
		FrameManager::Get().UnregisterFrameFactory("Model");
		FrameManager::Get().UnregisterFrameFactory("UnitModel");
		FrameManager::Get().UnregisterFrameFactory("Cooldown");
		FrameManager::Destroy();
		m_initialized = false;
	}
}
