// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/frame.h"
#include "graphics/material_instance.h"
#include "graphics/vertex_buffer.h"

namespace mmo
{
	/// @brief A frame type for displaying cooldown overlays using a material instance.
	/// This frame renders a material with a "Percentage" scalar parameter that controls
	/// the cooldown display (0.0 = just started, 1.0 = ready).
	class CooldownFrame final : public Frame
	{
	public:
		/// @brief Creates a new CooldownFrame instance.
		/// @param name The name of the frame.
		explicit CooldownFrame(const String& name);

		/// @brief Destructor.
		~CooldownFrame() override = default;

	public:
		/// @brief Sets the cooldown progress.
		/// @param progress The progress value from 0.0 (just started) to 1.0 (ready).
		void SetProgress(float progress);

		/// @brief Gets the current cooldown progress.
		/// @return The progress value from 0.0 to 1.0.
		[[nodiscard]] float GetProgress() const { return m_progress; }

		/// @brief Sets the material to use for rendering.
		/// @param materialPath The path to the material instance file.
		void SetMaterial(const String& materialPath);

	public:
		/// @copydoc Frame::Copy
		void Copy(Frame& other) override;

		/// @copydoc Frame::DrawSelf
		void DrawSelf() override;

		/// @copydoc Frame::PopulateGeometryBuffer
		void PopulateGeometryBuffer() override;

	protected:
		/// @brief Called when the material property was changed.
		/// @param prop The changed property.
		void OnMaterialChanged(const Property& prop);

	private:
		/// Contains a list of all property connections.
		scoped_connection_container m_propConnections;

		/// The material instance used for rendering.
		std::shared_ptr<MaterialInstance> m_material;

		/// The hardware (GPU) vertex buffer.
		VertexBufferPtr m_hwBuffer;

		/// The current progress value (0.0 to 1.0).
		float m_progress = 1.0f;
	};
}
