// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "frame.h"
#include "frame_mgr.h"
#include "default_renderer.h"
#include "button_renderer.h"

#include "base/utilities.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

#include <algorithm>


namespace mmo
{
	Frame::Frame(const std::string& type, const std::string & name)
		: m_type(type)
		, m_name(name)
		, m_needsRedraw(true)
		, m_parent(nullptr)
		, m_visible(true)
		, m_enabled(true)
		, m_clippedByParent(false)
		, m_needsLayout(true)
	{
		// Register text property for frame
		auto& textProp = AddProperty("Text");
		m_propConnections += textProp.Changed.connect(this, &Frame::OnTextPropertyChanged);
	}

	void Frame::Copy(Frame & other)
	{
		// Apply renderer property
		other.SetRenderer(m_renderer->GetName());

		// Apply other properties
		other.m_enabled = m_enabled;
		other.m_visible = m_visible;
		other.m_clippedByParent = m_clippedByParent;
		other.m_pixelSize = m_pixelSize;
		other.m_position = m_position;
		other.m_text = m_text;

		// Copy registered event handlers
		for (const auto& pair : m_eventsByName)
		{
			other.m_eventsByName[pair.first] = pair.second;
		}

		// Add section reference
		for (const auto& pair : m_sectionsByName)
		{
			auto& added = (other.m_sectionsByName[pair.first] = pair.second);
			added.SetComponentFrame(other);
		}
		// Add state imagery reference
		for (const auto& pair : m_stateImageriesByName)
		{
			auto& added = (other.m_stateImageriesByName[pair.first] = pair.second);
			
			added.m_layers.clear();

			for (const auto& layer : pair.second.m_layers)
			{
				FrameLayer newLayer;
				
				for (const auto& section : layer.m_sections)
				{
					auto* otherSection = other.GetImagerySectionByName(section->GetName());
					if (otherSection)
					{
						newLayer.AddSection(*otherSection);
					}
				}

				added.AddLayer(newLayer);
			}
		}

		// Copy all children and their children
		for (const auto& child : m_children)
		{
			// Create a copy of the child frame
			FramePtr copiedChild = FrameManager::Get().Create(m_type, "", true);
			ASSERT(copiedChild);

			// Copy properties over to child frame
			child->Copy(*copiedChild);

			// Add child copy to the copied frame
			other.m_children.emplace_back(std::move(copiedChild));
		}
	}

	Property & Frame::AddProperty(const std::string& name, std::string defaultValue)
	{
		// First, see if the property already exists and if so, return a reference on it
		Property* prop = GetProperty(name);
		if (prop)
		{
			// Apply the value for existing property
			prop->Set(defaultValue);
			return *prop;
		}

		// Doesn't exist yet, create it and return the reference
		const auto insertElem = m_propertiesByName.insert(std::make_pair(name, Property(std::move(defaultValue))));
		return insertElem.first->second;
	}

	Property * Frame::GetProperty(const std::string & name)
	{
		const auto propertyIt = m_propertiesByName.find(name);
		if (propertyIt != m_propertiesByName.end())
		{
			return &propertyIt->second;
		}

		return nullptr;
	}

	bool Frame::RemoveProperty(const std::string & name)
	{
		const auto it = m_propertiesByName.find(name);
		if (it != m_propertiesByName.end())
		{
			m_propertiesByName.erase(it);
			return true;
		}

		return false;
	}

	int Frame::Lua_SetText(lua_State * state)
	{
		// Try to grab the text
		const char* text = luaL_checkstring(state, 1);
		SetText(text);

		// No return values
		return 0;
	}

	int Frame::Lua_GetText(lua_State * state)
	{
		// Return the text as string value
		lua_pushstring(state, GetText().c_str());
		return 1;
	}

	int Frame::Lua_Show(lua_State * state)
	{
		SetVisible(true);
		return 0;
	}

	int Frame::Lua_Hide(lua_State * state)
	{
		SetVisible(false);
		return 0;
	}

	int Frame::Lua_Enable(lua_State * state)
	{
		SetEnabled(true);
		return 0;
	}

	int Frame::Lua_Disable(lua_State * state)
	{
		SetEnabled(false);
		return 0;
	}

	FrameEvent & Frame::RegisterEvent(std::string name)
	{
		auto* evt = FindEvent(name);
		if (evt)
		{
			return *evt;
		}

		const auto result = m_eventsByName.insert(std::make_pair(name, FrameEvent()));
		return result.first->second;
	}

	FrameEvent * Frame::FindEvent(const std::string & name)
	{
		const auto it = m_eventsByName.find(name);
		if (it != m_eventsByName.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	void Frame::UnregisterEvent(const std::string & name)
	{
		const auto it = m_eventsByName.find(name);
		if (it != m_eventsByName.end())
		{
			m_eventsByName.erase(it);
		}
	}

	bool Frame::TriggerEvent(const std::string & name)
	{
		// Find the named event and try to execute it's assigned lua script code.
		const FrameEvent* evt = FindEvent(name);
		if (evt)
		{
			(*evt)();
			return true;
		}

		return false;
	}

	void Frame::AddImagerySection(ImagerySection& section)
	{
		ASSERT(m_sectionsByName.find(section.GetName()) == m_sectionsByName.end());

		m_sectionsByName[section.GetName()] = section;
	}

	void Frame::RemoveImagerySection(const std::string & name)
	{
		const auto it = m_sectionsByName.find(name);
		ASSERT(it != m_sectionsByName.end());

		// TODO: Removing an imagery section will result in undefined behavior if the section
		// is still referenced by a layer in a state imagery.

		m_sectionsByName.erase(it);
	}

	ImagerySection * Frame::GetImagerySectionByName(const std::string & name) const
	{
		const auto it = m_sectionsByName.find(name);
		return (it == m_sectionsByName.end()) ? nullptr : (ImagerySection*)&it->second;
	}

	void Frame::AddStateImagery(StateImagery& stateImagery)
	{
		ASSERT(m_stateImageriesByName.find(stateImagery.GetName()) == m_stateImageriesByName.end());

		m_stateImageriesByName[stateImagery.GetName()] = stateImagery;
	}

	void Frame::RemoveStateImagery(const std::string & name)
	{
		const auto it = m_stateImageriesByName.find(name);
		ASSERT(it != m_stateImageriesByName.end());

		m_stateImageriesByName.erase(it);
	}

	StateImagery * Frame::GetStateImageryByName(const std::string & name) const
	{
		const auto it = m_stateImageriesByName.find(name);
		return (it == m_stateImageriesByName.end()) ? nullptr : (StateImagery*)&it->second;
	}

	void Frame::SetText(std::string text)
	{
		// Apply new text and invalidate rendering
		m_text = std::move(text);

		// Notify observers
		OnTextChanged();

		// Invalidate
		m_needsRedraw = true;
		m_needsLayout = true;
	}

	bool Frame::IsVisible(bool localOnly) const
	{
		// If this is the root frame or we only care for local visibility setting...
		if (localOnly || m_parent == nullptr || IsRootFrame())
		{
			// ... return local setting
			return m_visible;
		}

		// Return parent frame visibility AND local setting
		return m_parent->IsVisible(localOnly) && m_visible;
	}

	void Frame::SetVisible(bool visible)
	{
		if (m_visible != visible)
		{
			m_visible = visible;
			VisibilityChanged();
		}
	}

	bool Frame::IsEnabled(bool localOnly) const
	{
		if (localOnly || m_parent == nullptr || IsRootFrame())
		{
			return m_enabled;
		}

		return m_parent->IsEnabled(localOnly) && m_enabled;
	}

	void Frame::SetEnabled(bool enable)
	{
		if (m_enabled != enable)
		{
			m_enabled = enable;
			m_needsRedraw = true;

			// Release input on disable
			if (!m_enabled)
			{
				ReleaseInput();
			}

			EnabledStateChanged();
		}
	}

	bool Frame::IsRootFrame() const
	{
		FramePtr rootFrame = FrameManager::Get().GetTopFrame();
		if (rootFrame == nullptr)
			return false;

		return (rootFrame.get() == this);
	}

	void Frame::SetRenderer(const std::string & rendererName)
	{
		if (m_renderer != nullptr)
		{
			m_renderer->NotifyFrameDetached();

			m_renderer->m_frame = nullptr;
			m_renderer.reset();
		}

		// TODO: This is only a temporary test until there is a renderer factory
		if (rendererName == "ButtonRenderer")
		{
			m_renderer = std::make_unique<ButtonRenderer>("ButtonRenderer");
		}
		else
		{
			m_renderer = std::make_unique<DefaultRenderer>("DefaultRenderer");
		}

		if (m_renderer)
		{
			m_renderer->m_frame = this;
			m_renderer->NotifyFrameAttached();

			m_needsRedraw = true;
		}

		// TODO: Create new renderer instance by name

		// TODO: Register this frame for the new renderer
	}
	
	void Frame::SetClippedByParent(bool clipped)
	{
		if (m_clippedByParent != clipped)
		{
			m_clippedByParent = clipped;
			m_needsRedraw = true;
		}
	}

	void Frame::SetPosition(const Point & position)
	{
		m_position = position;

		// If this change somehow affects this frame, invalidate it
		if (!AnchorsSatisfyPosition())
		{
			m_needsRedraw = true;
			m_needsLayout = true;
		}
	}

	bool Frame::AnchorsSatisfyXPosition() const
	{
		return
			m_anchors.find(anchor_point::Left) != m_anchors.end() ||
			m_anchors.find(anchor_point::Right) != m_anchors.end() ||
			m_anchors.find(anchor_point::HorizontalCenter) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyYPosition() const
	{
		return
			m_anchors.find(anchor_point::Top) != m_anchors.end() ||
			m_anchors.find(anchor_point::Bottom) != m_anchors.end() ||
			m_anchors.find(anchor_point::VerticalCenter) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyWidth() const
	{
		// For this to work, there need to be an anchor set for left and right
		return 
			m_anchors.find(anchor_point::Left) != m_anchors.end() &&
			m_anchors.find(anchor_point::Right) != m_anchors.end();
	}

	bool Frame::AnchorsSatisfyHeight() const
	{
		// For this to work, there need to be an anchor set for left and right
		return
			m_anchors.find(anchor_point::Top) != m_anchors.end() &&
			m_anchors.find(anchor_point::Bottom) != m_anchors.end();
	}

	void Frame::SetAnchor(AnchorPoint point, AnchorPoint relativePoint, Frame::Pointer relativeTo, float offset)
	{
		// Create a new anchor
		m_anchors[point] = std::make_unique<Anchor>(point, relativePoint, relativeTo, offset);
		m_needsRedraw = true;
		m_needsLayout = true;
	}

	void Frame::ClearAnchor(AnchorPoint point)
	{
		const auto it = m_anchors.find(point);
		if (it != m_anchors.end())
		{
			m_anchors.erase(it);
			m_needsRedraw = true;
			m_needsLayout = true;
		}
	}

	bool Frame::IsHovered() const
	{
		return this == FrameManager::Get().GetHoveredFrame().get();
	}

	void Frame::Invalidate()
	{
		m_needsRedraw = true;
	}

	Frame::Pointer Frame::GetChildFrameAt(const Point & position, bool allowDisabled)
	{
		// Iterate through all children
		for (auto childIt = m_children.rbegin(); childIt != m_children.rend(); ++childIt)
		{
			const auto& child = *childIt;

			// Check the rectangle
			const Rect childRect = child->GetAbsoluteFrameRect();
			if (childRect.IsPointInRect(position) &&
				child->IsVisible() &&
				(allowDisabled || child->IsEnabled()))
			{
				return child->GetChildFrameAt(position, allowDisabled);
			}
		}

		return shared_from_this();
	}

	void Frame::CaptureInput()
	{
		FrameManager::Get().SetCaptureWindow(shared_from_this());
	}

	void Frame::ReleaseInput()
	{
		// Reset capture frame if it is this frame
		if (FrameManager::Get().GetCaptureFrame().get() == this)
		{
			FrameManager::Get().SetCaptureWindow(nullptr);
		}
	}

	void Frame::Render()
	{
		// If this frame is hidden, we don't have anything to do
		if (!IsVisible())
			return;

		auto& gx = GraphicsDevice::Get();

		// If this is a top level frame, prepare render common render states so that
		// we don't have to care about them in every single child frame or child frame
		// element.
		if (m_parent == nullptr)
		{
			int32 vpW, vpH;
			gx.GetViewport(nullptr, nullptr, &vpW, &vpH);

			gx.SetTopologyType(TopologyType::TriangleList);
			gx.SetBlendMode(BlendMode::Alpha);
			gx.SetTransformMatrix(TransformType::World, Matrix4::Identity);
			gx.SetTransformMatrix(TransformType::View, Matrix4::Identity);
			gx.SetTransformMatrix(TransformType::Projection, Matrix4::MakeOrthographic(0.0f, static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 0.0f, 100.0f));
		}

		// Draw self
		DrawSelf();

		// Whether a clip rect has been set by this function call to avoid calling ResetClipRect on
		// the graphics library more often than needed
		bool hasClipRectSet = false;

		// Draw children
		for (const auto& child : m_children)
		{
			if (child->IsClippedByParent())
			{
				// TODO: set clip rect
				hasClipRectSet = true;
			}
			else if (hasClipRectSet)
			{
				// Reset clip rect
				gx.ResetClipRect();
				hasClipRectSet = false;
			}

			// Render child frame
			child->Render();
		}
	}

	void Frame::Update(float elapsed)
	{
		// Try to call the renderer's update method if we have a valid renderer
		if (m_renderer != nullptr)
		{
			m_renderer->Update(elapsed);
		}
	}

	void Frame::AddChild(Frame::Pointer frame)
	{
		// We can't add ourself as child frame
		if (frame.get() == this)
		{
			throw std::runtime_error("Frame can't be it's own child frame!");
		}

		// Add to the list of children
		m_children.push_back(frame);

		// Register ourself as parent frame
		frame->m_parent = this;
	}

	Rect Frame::GetRelativeFrameRect()
	{
		// Use the internal size property as the default value
		Size mySize = GetPixelSize();

		// Return the rectangle with the calculated size
		return Rect(Point(), mySize);
	}

	static void AdjustRectToAnchor(Rect& r, const Rect& parentRect, AnchorPoint p, const Point& offset)
	{
		switch (p)
		{
		case AnchorPoint::Top:
			r.Offset(Point(0.0f, offset.y));
			break;
		case AnchorPoint::Left:
			r.Offset(Point(offset.x, 0.0f));
			break;
		case AnchorPoint::Right:
			r.Offset(Point(
				parentRect.GetWidth() - r.GetWidth() - offset.x,
				0.0f));
			break;
		case AnchorPoint::Bottom:
			r.Offset(Point(
				0.0f,
				parentRect.bottom - r.GetHeight() - offset.y));
			break;

		case AnchorPoint::HorizontalCenter:
			r.Offset(Point(
				parentRect.GetWidth() * 0.5f - r.GetWidth() * 0.5f + offset.x,
				0.0f
			));
			break;

		case AnchorPoint::VerticalCenter:
			r.Offset(Point(
				0.0f,
				parentRect.GetHeight() * 0.5f - r.GetHeight() * 0.5f + offset.y
			));
			break;
		}
	}

	Rect Frame::GetAbsoluteFrameRect()
	{
		// Try to use cached abs rect
		if (!m_needsLayout)
			return m_absRectCache;

		// First, obtain the relative frame rect
		m_absRectCache = GetRelativeFrameRect();

		// This rect will contain the absolute parent rectangle
		const Rect parentRect = GetParentRect();

		// Add parent rect offset to the relative rect
		m_absRectCache.Offset(parentRect.GetPosition());
		m_absRectCache.Offset(m_position);

		// Apply anchor points
		for (const auto& anchor : m_anchors)
		{
			bool hasOpposite = false;

			const AnchorPoint oppositePoint = OppositeAnchorPoint(anchor.first);
			if (oppositePoint != anchor_point::None)
			{
				hasOpposite = m_anchors.find(oppositePoint) != m_anchors.end();
			}

			// Try to use other frame's rectangle if referenced
			Rect anchorParentRect = parentRect;
			if (anchor.second->GetRelativeTo() != nullptr)
			{
				anchorParentRect = anchor.second->GetRelativeTo()->GetAbsoluteFrameRect();
			}

			anchor.second->ApplyToAbsRect(
				m_absRectCache, anchorParentRect, hasOpposite);
		}

		// Move rectangle
		m_needsLayout = false;

		// Return the current rect
		return m_absRectCache;
	}
	
	void Frame::DrawSelf()
	{
		BufferGeometry();
		QueueGeometry();
	}

	void Frame::BufferGeometry()
	{
		if (m_needsRedraw)
		{
			// dispose of already cached geometry.
			m_geometryBuffer.Reset();

			// Signal rendering started
			RenderingStarted();

			// If there is a renderer created, use it to render this frame, otherwise call the frame's internal
			// PopulateGeometryBuffer virtual method.
			if (m_renderer != nullptr)
			{
				m_renderer->Render();
			}
			else
			{
				PopulateGeometryBuffer();
			}

			// Signal rendering ended
			RenderingEnded();

			// mark ourselves as no longer needed a redraw.
			m_needsRedraw = false;
		}
	}

	void Frame::QueueGeometry()
	{
		// Draw the geometry buffer
		m_geometryBuffer.Draw();
	}

	Rect Frame::GetParentRect()
	{
		Rect parentRect;

		if (m_parent == nullptr && m_parent != this)
		{
			// Obtain the current viewport size in pixels in case this 
			int32 vpW, vpH;
			GraphicsDevice::Get().GetViewport(nullptr, nullptr, &vpW, &vpH);

			// No parent frame available, then the screen rect is the parent rect
			parentRect.SetSize(Size(vpW, vpH));
		}
		else
		{
			// Use the absolute frame rect of the parent
			parentRect = m_parent->GetAbsoluteFrameRect();
		}

		return parentRect;
	}

	void Frame::OnTextChanged()
	{
		// Invoke the signal
		TextChanged();
	}

	void Frame::OnTextPropertyChanged(const Property & property)
	{
		SetText(property.GetValue());
	}

	GeometryBuffer & Frame::GetGeometryBuffer()
	{ 
		return m_geometryBuffer;
	}

	void Frame::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// Simply raise the signal
		MouseDown(MouseEventArgs(buttons, position.x, position.y));
	}

	void Frame::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		// Simply raise the signal
		MouseUp(MouseEventArgs(buttons, position.x, position.y));
	}
}
