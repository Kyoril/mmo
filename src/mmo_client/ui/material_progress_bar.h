// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_ui/progress_bar.h"
#include "graphics/material_instance.h"
#include "graphics/vertex_buffer.h"

namespace mmo
{
	/// @brief Client progress bar that can draw its fill with a UI material instead of imagery.
	///
	/// Without a "FillMaterial" it behaves exactly like ProgressBar. With one, the background and
	/// overlay imagery are still drawn from the geometry buffer, and a single material quad covering
	/// the inset fill rect is drawn between them. The material receives these parameters:
	/// - Scalar "Progress": the displayed fill (0..1)
	/// - Scalar "Trail": the displayed trail end behind a loss (>= Progress)
	/// - Scalar "Time": seconds since the bar was created, for animated highlights
	/// - Scalar "Aspect": fill rect width / height in pixels
	/// - Vector "FillColor" / "TrailColor": the bar's ProgressColor (times "FillMaterialTint") / TrailColor
	///
	/// "FillMaterialTint" stands in for the tint of the replaced fill imagery, so a bar whose colour came
	/// from its Progress imagery (e.g. a green health fill) keeps it with the material.
	class MaterialProgressBar final : public ProgressBar
	{
	public:
		/// @brief Creates a new material progress bar.
		/// @param name The name of the frame.
		explicit MaterialProgressBar(const String& name);

		/// @brief Destructor.
		~MaterialProgressBar() override = default;

	public:
		/// @copydoc Frame::Copy
		void Copy(Frame& other) override;

		/// @copydoc Frame::Update
		void Update(float elapsed) override;

		/// @copydoc Frame::DrawSelf
		void DrawSelf() override;

	protected:
		/// @copydoc ProgressBar::PopulateGeometryBuffer
		void PopulateGeometryBuffer() override;

	private:
		/// @brief Loads the fill material when the "FillMaterial" property changes.
		void OnFillMaterialChanged(const Property& prop);

		/// @brief Parses the "FillMaterialTint" property.
		void OnFillMaterialTintChanged(const Property& prop);

		/// @brief Draws the material quad over the fill rect.
		void DrawMaterialFill();

	private:
		scoped_connection_container m_materialConnections;

		/// Fill material, or null to fall back to the imagery fill.
		std::shared_ptr<MaterialInstance> m_material;

		/// Quad covering the inset fill rect.
		VertexBufferPtr m_hwBuffer;

		/// Vertex count of the background part of the geometry buffer; the overlay follows it.
		uint32 m_overlayStartVertex = 0;

		/// Multiplied into ProgressColor for the material, replacing the fill imagery's tint.
		Color m_fillMaterialTint = Color::White;

		/// Fill rect aspect ratio, updated whenever the geometry is rebuilt.
		float m_aspect = 1.0f;

		/// Animation clock for the material.
		float m_time = 0.0f;
	};
}
