#pragma once

#include "scene_graph/movable_object.h"
#include "frame_ui/font.h"
#include "graphics/vertex_index_data.h"

namespace mmo
{
	/// A component that display text in the world.
	class WorldTextComponent : public MovableObject, public Renderable
	{
	public:
		WorldTextComponent(FontPtr font, MaterialPtr material, const String& text);

		void PrepareRenderOperation(RenderOperation& operation) override;

		[[nodiscard]] const Matrix4& GetWorldTransform() const override;

		[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

		[[nodiscard]] MaterialPtr GetMaterial() const override;

		[[nodiscard]] const String& GetMovableType() const override;

		[[nodiscard]] const AABB& GetBoundingBox() const override;

		[[nodiscard]] float GetBoundingRadius() const override;

		void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

		void PopulateRenderQueue(RenderQueue& queue) override;

		void SetFont(FontPtr font);

		[[nodiscard]] FontPtr GetFont() const { return m_font; }

	void SetText(const String& text);

	[[nodiscard]] const String& GetText() const { return m_text; }

	/// Sets the font color for the text
	void SetFontColor(const Color& color);

	/// Gets the current font color
	[[nodiscard]] const Color& GetFontColor() const { return m_fontColor; }

	private:
		void OnTextChanged();

		void UpdateGeometry();

	private:
		FontPtr m_font;

		String m_text;

		bool m_textInvalidated{ true };

		AABB m_boundingBox;

		Matrix4 m_worldTransform;

	MaterialPtr m_material;

	std::unique_ptr<VertexData> m_vertexData;

	std::unique_ptr<IndexData> m_indexData;

	Color m_fontColor{ Color::White };
	};
}
