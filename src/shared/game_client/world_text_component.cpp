
#include "world_text_component.h"

namespace mmo
{
	WorldTextComponent::WorldTextComponent(FontPtr font, const String& text)
		: m_font(font)
		, m_text(text)
	{
		// TODO
		//ASSERT(m_font);
	}

	void WorldTextComponent::PrepareRenderOperation(RenderOperation& operation)
	{
		// TODO
	}

	const Matrix4& WorldTextComponent::GetWorldTransform() const
	{
		// TODO
		return m_worldTransform;
	}

	float WorldTextComponent::GetSquaredViewDepth(const Camera& camera) const
	{
		return 0.0f;
	}

	MaterialPtr WorldTextComponent::GetMaterial() const
	{
		return nullptr;
	}

	const String& WorldTextComponent::GetMovableType() const
	{
		static String s_worldTextType = "WorldText";
		return s_worldTextType;
	}

	const AABB& WorldTextComponent::GetBoundingBox() const
	{
		return m_boundingBox;
	}

	float WorldTextComponent::GetBoundingRadius() const
	{
		return 0.0f;
	}

	void WorldTextComponent::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		// TODO:
	}

	void WorldTextComponent::PopulateRenderQueue(RenderQueue& queue)
	{
		// TODO:
	}

	void WorldTextComponent::SetFont(FontPtr font)
	{
		ASSERT(font);

		if (font == m_font)
		{
			return;
		}

		m_font = font;
		m_textInvalidated = true;
	}

	void WorldTextComponent::SetText(const String& text)
	{
		if (text == m_text)
		{
			return;
		}

		m_text = text;
		m_textInvalidated = true;
	}

	void WorldTextComponent::OnTextChanged()
	{
		if (!m_textInvalidated)
		{
			return;
		}

		if (!m_text.empty())
		{
		}

		m_textInvalidated = false;
	}
}
