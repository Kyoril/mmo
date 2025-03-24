
#include "world_text_component.h"

#include <algorithm>

#include "graphics/vertex_index_data.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	WorldTextComponent::WorldTextComponent(FontPtr font, MaterialPtr materialPtr, const String& text)
		: m_font(font)
		, m_text(text)
		, m_material(materialPtr)
	{
		m_textInvalidated = true;
		ASSERT(materialPtr);

		m_vertexData = std::make_unique<VertexData>();
		m_vertexData->vertexCount = 0;
		m_vertexData->vertexStart = 0;

		// Setup vertex buffer binding
		uint32 offset = 0;
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
		offset += m_vertexData->vertexDeclaration->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

		UpdateGeometry();
	}

	void WorldTextComponent::PrepareRenderOperation(RenderOperation& operation)
	{
		operation.vertexData = m_vertexData.get();
		operation.indexData = nullptr;
		operation.material = m_material;
		operation.topology = TopologyType::TriangleList;
		operation.vertexFormat = VertexFormat::PosColorNormalBinormalTangentTex1;
	}

	const Matrix4& WorldTextComponent::GetWorldTransform() const
	{
		return GetParentNodeFullTransform();
	}

	float WorldTextComponent::GetSquaredViewDepth(const Camera& camera) const
	{
		return GetParentSceneNode()->GetSquaredViewDepth(camera);
	}

	MaterialPtr WorldTextComponent::GetMaterial() const
	{
		return m_material;
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
		return GetBoundingRadiusFromAABB(m_boundingBox);
	}

	void WorldTextComponent::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
	{
		visitor.Visit(*this, 0, false);
	}

	void WorldTextComponent::PopulateRenderQueue(RenderQueue& queue)
	{
		if (m_text.empty())
		{
			return;
		}

		queue.AddRenderable(*this, m_renderQueueId, m_renderQueuePriority);
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
		UpdateGeometry();
	}

	void WorldTextComponent::OnTextChanged()
	{
		m_textInvalidated = false;
	}

	void WorldTextComponent::UpdateGeometry()
	{
		if (!m_textInvalidated)
		{
			return;
		}

		struct VertexStruct
		{
			Vector3 position;
			uint32 color;
			Vector3 normal;
			Vector3 binormal;
			Vector3 tangent;
			float u, v;
		};

		m_boundingBox.SetNull();
		m_boundingBox.min.z = -1.0f;
		m_boundingBox.max.z = 1.0f;
		m_boundingBox.min.x = std::numeric_limits<float>::max();
		m_boundingBox.min.y = std::numeric_limits<float>::max();
		m_boundingBox.max.x = std::numeric_limits<float>::lowest();
		m_boundingBox.max.y = std::numeric_limits<float>::lowest();

		std::vector<VertexStruct> vertices;
		vertices.reserve(m_text.size() * 6);  // 6 verts per glyph

		// Decide how big or small you want the text to be.
		// E.g. 0.01f if you are in "world" space units.
		constexpr float scale = 0.005f;

		// Start drawing at some "anchor" point
		// (You can make this your baseline or top-left; adjust as needed.)
		Point cursor(0.0f, 0.0f);

		std::vector<float> lineWidths;
		std::vector<uint32> lineStarts;

		for (size_t i = 0; i < m_text.length(); ++i)
		{
			char c = m_text[i];

			if (c == '\n')
			{
				lineStarts.push_back(vertices.size());
				lineWidths.push_back(cursor.x);

				cursor.x = 0.0f;
				cursor.y -= m_font->GetHeight(scale);
				continue;
			}

			if (c == '\t')
			{
				if (const FontGlyph* spaceGlyph = m_font->GetGlyphData(' '))
				{
					float spaceAdvance = spaceGlyph->GetAdvance(scale);
					cursor.x += 4.0f * spaceAdvance;
				}
				continue;
			}

			// Grab the glyph for this character
			const FontGlyph* glyph = m_font->GetGlyphData(c);
			if (!glyph)
				continue;  // skip missing glyph

			// The glyph refers to an image with offset, size, texture coords, etc.
			const FontImage* image = glyph->GetImage();
			if (!image)
				continue;

			const FontImageset* imageset = image->GetImageset();
			if (!imageset || !imageset->GetTexture())
				continue;

			m_material->SetTextureParameter("FontImage", imageset->GetTexture());

			// The "offset" is how the glyph is positioned relative to the text cursor
			// (typically its top-left corner or bearing).
			const Point& offset = image->GetOffset();
			// The "size" is how wide/tall the glyph's actual image is.
			const Size& size = image->GetSize();

			// Build the quad's screen‐space (or world‐space) corners
			//   left   = cursor.x + offset.x * scale
			//   top    = cursor.y + offset.y * scale
			float left = cursor.x + offset.x * scale;
			float top = cursor.y - offset.y * scale;
			float width = size.width * scale;
			float height = size.height * scale;

			// Because many UIs treat "top < bottom" in Y,
			// we compute bottom as top + height:
			float bottom = top - height;
			float right = left + width;

			// Get the glyph's sub‐texture coords
			const Rect& texArea = image->GetSourceTextureArea();
			float texW = static_cast<float>(imageset->GetTexture()->GetWidth());
			float texH = static_cast<float>(imageset->GetTexture()->GetHeight());

			float u1 = texArea.left / texW;
			float v1 = texArea.top / texH;
			float u2 = texArea.right / texW;
			float v2 = texArea.bottom / texH;

			// Build two triangles (6 vertices).
			// If your coordinate system is "Y down," the "top < bottom" is correct.
			// Adjust normal/tangent if needed, or set them to e.g. UnitZ or NegativeUnitZ.
			const uint32 color = 0xFFFFFFFF;

			// Triangle #1
			vertices.push_back({ { left,  top,    0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u1, v1 });
			vertices.push_back({ { left,  bottom, 0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u1, v2 });
			vertices.push_back({ { right, bottom, 0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u2, v2 });

			// Triangle #2
			vertices.push_back({ { right, bottom, 0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u2, v2 });
			vertices.push_back({ { right, top,    0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u2, v1 });
			vertices.push_back({ { left,  top,    0.0f }, color,
								 Vector3::UnitZ, Vector3::UnitX, Vector3::UnitY, u1, v1 });

			// Advance cursor horizontally by the glyph's advance value
			cursor.x += glyph->GetAdvance(scale);
		}

		lineStarts.push_back(vertices.size());
		lineWidths.push_back(cursor.x);

		// Iterate through all vertices again and modify the vertex x position by reducing it half of max.x from m_boundingBox
		uint32 i = 0, line = 0;
		uint32 nextLineIndex = lineStarts[line];
		float currentLineWidth = lineWidths[line];

		for (auto& vertex : vertices)
		{
			if (i++ >= nextLineIndex)
			{
				nextLineIndex = lineStarts[line + 1];
				currentLineWidth = lineWidths[line + 1];
				line++;
			}

			vertex.position.x -= currentLineWidth * 0.5f;
			vertex.position.y += m_font->GetHeight(scale) * static_cast<float>(lineWidths.size() - 1);

			m_boundingBox.min.x = std::min(m_boundingBox.min.x, vertex.position.x);
			m_boundingBox.min.y = std::min(m_boundingBox.min.y, vertex.position.y);
			m_boundingBox.max.x = std::max(m_boundingBox.max.x, vertex.position.x);
			m_boundingBox.max.y = std::max(m_boundingBox.max.y, vertex.position.y);
		}

		m_vertexData->vertexCount = vertices.size();

		if (!vertices.empty())
		{
			VertexBufferPtr bufferPtr = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, m_vertexData->vertexDeclaration->GetVertexSize(0), BufferUsage::StaticWriteOnly, vertices.data());
			m_vertexData->vertexBufferBinding->SetBinding(0, bufferPtr);
		}
		else
		{
			m_boundingBox.min.x = 0.0f;
			m_boundingBox.min.y = 0.0f;
			m_boundingBox.max.x = 0.0f;
			m_boundingBox.max.y = 0.0f;

			m_vertexData->vertexBufferBinding->UnsetAllBindings();
		}
	}
}
