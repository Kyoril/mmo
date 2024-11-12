#pragma once

#include "scene_graph/movable_object.h"
#include "frame_ui/font.h"

namespace mmo
{
	/// A component that display text in the world.
	class WorldTextComponent : public MovableObject, public Renderable
	{
	public:
		WorldTextComponent(FontPtr font, const String& text);

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

	private:
		void OnTextChanged();

	private:
		FontPtr m_font;

		String m_text;

		bool m_textInvalidated{ true };

		AABB m_boundingBox;

		Matrix4 m_worldTransform;
	};
}
