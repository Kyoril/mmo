// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"

#include <string>

struct lua_State;

namespace mmo
{
	/// @brief Stand-in for the client's ModelFrame: same Lua methods, but renders no 3D model.
	///
	/// The frame still draws whatever its XML layout defines, so the layout around it is authentic.
	class PlaceholderModelFrame : public Frame
	{
	public:
		explicit PlaceholderModelFrame(const std::string& type, const std::string& name)
			: Frame(type, name)
		{
		}

	public:
		void SetModelFile(const std::string& filename) { m_modelFile = filename; }
		void Yaw(const float angleDegrees) { m_yaw += angleDegrees; }
		void ResetYaw() { m_yaw = 0.0f; }
		void SetAutoRender(bool) {}
		void SetZoom(const float zoom) { m_zoom = zoom; }
		float GetZoom() const { return m_zoom; }
		float GetYaw() const { return m_yaw; }
		void InvalidateModel() {}

	private:
		std::string m_modelFile;
		float m_zoom = 1.0f;
		float m_yaw = 0.0f;
	};

	/// @brief Stand-in for the client's UnitModelFrame.
	class PlaceholderUnitModelFrame final : public PlaceholderModelFrame
	{
	public:
		explicit PlaceholderUnitModelFrame(const std::string& name)
			: PlaceholderModelFrame("UnitModel", name)
		{
		}

	public:
		void SetUnit(const std::string& unitName) { m_unit = unitName; }

	private:
		std::string m_unit;
	};

	/// @brief Stand-in for the client's CooldownFrame: keeps the progress, draws no sweep.
	class PlaceholderCooldownFrame final : public Frame
	{
	public:
		explicit PlaceholderCooldownFrame(const std::string& name)
			: Frame("Cooldown", name)
		{
		}

	public:
		void SetProgress(const float progress) { m_progress = progress; }
		float GetProgress() const { return m_progress; }

	private:
		float m_progress = 0.0f;
	};

	/// @brief Registers frame factories, renderers and Lua classes for the frame types and
	///        renderers only the game client provides (models, minimap, world).
	/// @param luaState The UI's Lua state.
	void RegisterPlaceholderFrames(lua_State* luaState);
}
