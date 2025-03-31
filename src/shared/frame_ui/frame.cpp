// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "frame.h"
#include "frame_mgr.h"
#include "font_mgr.h"
#include "default_renderer.h"

#include "base/macros.h"

#include <algorithm>


namespace mmo
{
	Frame::Frame(const std::string& type, const std::string& name)
		: m_type(type)
		, m_name(name)
		, m_needsRedraw(true)
		, m_visible(true)
		, m_enabled(true)
		, m_clippedByParent(false)
		, m_parent(nullptr)
		, m_needsLayout(true)
		, m_focusable(false)
	{
		// Register text property for frame
		m_propConnections += AddProperty("ClippedByParent").Changed.connect(this, &Frame::OnClippedByParentChanged);
		m_propConnections += AddProperty("Text").Changed.connect(this, &Frame::OnTextPropertyChanged);
		m_propConnections += AddProperty("Focusable").Changed.connect(this, &Frame::OnFocusablePropertyChanged);
		m_propConnections += AddProperty("Enabled").Changed.connect(this, &Frame::OnEnabledPropertyChanged);
		m_propConnections += AddProperty("Visible").Changed.connect(this, &Frame::OnVisiblePropertyChanged);
		m_propConnections += AddProperty("Font").Changed.connect(this, &Frame::OnFontPropertyChanged);
		m_propConnections += AddProperty("Color").Changed.connect(this, &Frame::OnColorPropertyChanged);
		m_propConnections += AddProperty("Clickable").Changed.connect(this, &Frame::OnClickablePropertyChanged);
	}

	void Frame::Copy(Frame & other)
	{
		if (m_renderer)
		{
			// Apply renderer property
			other.SetRenderer(m_renderer->GetName());
		}

		// Apply other properties
		other.m_enabled = m_enabled;
		other.m_visible = m_visible;
		other.m_clippedByParent = m_clippedByParent;
		other.m_pixelSize = m_pixelSize;
		other.m_position = m_position;
		other.m_text = m_text;
		other.m_onLoad = m_onLoad;
		other.m_onUpdate = m_onUpdate;
		other.m_needsLayout = true;
		other.m_needsRedraw = true;
		other.m_onEnterPressed = m_onEnterPressed;
		other.m_onTabPressed = m_onTabPressed;
		other.m_onSpacePressed = m_onSpacePressed;
		other.m_onEscapePressed = m_onEscapePressed;
		other.m_onEnter = m_onEnter;
		other.m_onLeave = m_onLeave;
		other.m_onShow = m_onShow;
		other.m_onHide = m_onHide;
		other.m_onClick = m_onClick;
		other.m_id = m_id;
		other.m_focusable = m_focusable;
		other.m_clickable = m_clickable;
		other.m_opacity = m_opacity;
		other.RemoveAllChildren();
		
		// Set all properties
		for (const auto& pair : m_propertiesByName)
		{
			// It is valid to define new property in xml only to refer to them, so
			// not all properties have to exist on the other frame already.
			auto* otherProp = other.GetProperty(pair.first);
			if (!otherProp)
			{
				// Other property doesn't yet exist, add a new one.
				otherProp = &other.AddProperty(pair.first);
				otherProp->Set(pair.second.GetValue());
			}

			// Apply property value
			otherProp->Set(pair.second.GetValue());
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
			FramePtr copiedChild = FrameManager::Get().Create(child->GetType(), other.GetName() + "_" + child->GetName(), true);
			ASSERT(copiedChild);

			// Copy properties over to child frame
			child->Copy(*copiedChild);

			// Add child copy to the copied frame
			copiedChild->m_parent = &other;
			other.m_children.push_back(copiedChild);

			// Copy anchors
			for (auto& anchor : child->m_anchors)
			{
				auto relativeTo = anchor.second->GetRelativeTo();
				if (relativeTo)
				{
					// Check if other frame is part of our template
					if (relativeTo->IsChildOf(*this))
					{
						relativeTo = other.FindChild(other.GetName() + "_" + relativeTo->GetName());
					}
				}

				copiedChild->SetAnchor(anchor.first, anchor.second->GetRelativePoint(), relativeTo, anchor.second->GetOffset());
			}
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

	bool Frame::Lua_IsVisible() const
	{
		return IsVisible(false);
	}

	Frame::Pointer Frame::Clone()
	{
		static uint32 s_counter = 0;

		// Duplicate list item and copy attributes over
		auto newFrame = FrameManager::Get().Create(m_type, m_name + "_" + std::to_string(s_counter++), true);
		Copy(*newFrame);

		// Return new frame
		return newFrame;
	}

	void Frame::RegisterEvent(const std::string & name, const luabind::object & fn)
	{
		m_eventFunctionsByName[name] = fn;

		FrameManager::Get().FrameRegisterEvent(shared_from_this(), name);
	}

	void Frame::UnregisterEvent(const std::string & name)
	{
		const auto it = m_eventFunctionsByName.find(name);
		if (it != m_eventFunctionsByName.end())
		{
			m_eventFunctionsByName.erase(it);
		}

		FrameManager::Get().FrameUnregisterEvent(shared_from_this(), name);
	}

	void Frame::SetOnLoad(const luabind::object& fn)
	{
		m_onLoad = fn;
	}

	void Frame::SetOnUpdate(const luabind::object& fn)
	{
		m_onUpdate = fn;
	}

	void Frame::OnLoad()
	{
		if (!m_loaded)
		{
			m_loaded = true;

			if (m_onLoad.is_valid())
			{
				try
				{
					m_onLoad(this);
				}
				catch (const luabind::error& e)
				{
					ELOG("Error calling " << GetName() << ":OnLoad: " << e.what());
				}
			}
		}

		for (const auto& child : m_children)
		{
			child->OnLoad();
		}
	}

	ImagerySection* Frame::AddImagerySection(ImagerySection& section)
	{
		ASSERT(m_sectionsByName.find(section.GetName()) == m_sectionsByName.end());

		return &(m_sectionsByName[section.GetName()] = section);
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
		if (text == m_text)
		{
			return;
		}

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
		if (m_visible == visible)
		{
			return;
		}

		m_visible = visible;
		VisibilityChanged();

		if (m_visible && IsVisible(false))
		{
			OnShow();
		}
		else if (!m_visible)
		{
			// We were visible locally before, see if parent was visible as well (not just locally) which means we were actually visible and are now no longer visible
			if (m_parent && m_parent->IsVisible(false))
			{
				OnHide();
			}
		}
	}

	bool Frame::IsEnabled(bool localOnly) const
	{
		if (!m_enabled)
		{
			return false;
		}

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

		// Create the frame renderer instance
		m_renderer = FrameManager::Get().CreateRenderer(rendererName);

		// Attach this frame to the renderer instance
		if (m_renderer)
		{
			m_renderer->m_frame = this;
			m_renderer->NotifyFrameAttached();

			m_needsRedraw = true;
		}

		// TODO: Create new renderer instance by name

		// TODO: Register this frame for the new renderer
	}

	bool Frame::IsClippedByParent() const
	{
		return m_clippedByParent;
	}

	void Frame::SetClippedByParent(bool clipped)
	{
		if (m_clippedByParent != clipped)
		{
			m_clippedByParent = clipped;
			m_needsRedraw = true;

			if (m_parent)
			{
				m_parent->Invalidate(false);
			}
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

		Invalidate();
	}

	void Frame::SetSize(const float width, const float height)
	{
		m_pixelSize = Size(width, height);

		if (!AnchorsSatisfySize())
		{
			Invalidate();	
		}
	}

	void Frame::SetWidth(const float width)
	{
		m_pixelSize.width = width;
		
		if (!AnchorsSatisfyWidth())
		{
			Invalidate();	
		}
	}

	void Frame::SetHeight(const float height)
	{
		m_pixelSize.height = height;

		if (!AnchorsSatisfyHeight())
		{
			Invalidate();	
		}
	}

	float Frame::GetWidth() const
	{
		if (!AnchorsSatisfyWidth())
		{
			return m_pixelSize.width;
		}

		return m_absRectCache.GetWidth();
	}

	float Frame::GetHeight() const
	{
		if (!AnchorsSatisfyHeight())
		{
			return m_pixelSize.height;
		}

		return m_absRectCache.GetHeight();
	}

	float Frame::GetTextWidth()
	{
		const FontPtr font = GetFont();
		if (!font)
		{
			return 0.0f;
		}

		return font->GetTextWidth(m_text, 1.0f /*FrameManager::Get().GetUIScale().y*/);
	}

	float Frame::GetTextHeight()
	{
		const FontPtr font = GetFont();
		if (!font)
		{
			return 0.0f;
		}

		const Rect rect = GetAbsoluteFrameRect();
		const uint32 lineCount = font->GetLineCount(m_text, rect, FrameManager::Get().GetUIScale().y);
		const float result = font->GetHeight() * lineCount;
		return result;
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

	void Frame::ClearAnchors()
	{
		m_anchors.clear();
		m_needsRedraw = true;
		m_needsLayout = true;
	}

	bool Frame::IsHovered() const
	{
		return this == FrameManager::Get().GetHoveredFrame().get();
	}

	void Frame::Invalidate(bool includeLayout) noexcept
	{
		m_needsRedraw = true;

		for (auto& child : m_children)
		{
			child->Invalidate(includeLayout);
		}

		if (includeLayout)
		{
			m_needsLayout = true;
		}
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
		// Only allow input capture if the frame is in the enabled state and focusable is enabled
		if (m_enabled && m_focusable)
		{
			FrameManager::Get().SetCaptureWindow(shared_from_this());
		}
	}

	void Frame::ReleaseInput()
	{
		// Reset capture frame if it is this frame
		if (FrameManager::Get().GetCaptureFrame().get() == this)
		{
			FrameManager::Get().SetCaptureWindow(nullptr);
		}
	}

	bool Frame::HasInputCaptured() const
	{
		return FrameManager::Get().GetCaptureFrame().get() == this;
	}

	void Frame::InvalidateChildren(bool recursive)
	{
		for (auto& child : m_children)
		{
			child->Invalidate();

			if (recursive)
			{
				child->InvalidateChildren(recursive);
			}
		}
	}

	Size Frame::GetIntrinsicSize()
	{
		Size intrinsicSize;

		// Iterate through all child frames and use their intrinsic sizes as well
		for (auto& child : m_children)
		{
			// Grab the child's intrinsic size value
			auto childIntrinsic = child->GetIntrinsicSize();

			// Take the maximum value of it
			intrinsicSize.width = std::max(childIntrinsic.width, intrinsicSize.width);
			intrinsicSize.height = std::max(childIntrinsic.height, intrinsicSize.height);
		}

		return intrinsicSize;
	}

	void Frame::SetProperty(const std::string& name, const std::string& value)
	{
		Property* prop = GetProperty(name);
		if (prop)
		{
			prop->Set(value);
		}
	}

	const char* Frame::GetPropertyValue(const std::string& name)
	{
		const Property* prop = GetProperty(name);
		if (prop)
		{
			return prop->GetValue().c_str();
		}

		return nullptr;
	}

	bool Frame::IsChildOf(Frame& parent) const
	{
		if (m_parent == &parent)
		{
			return true;
		}

		if (m_parent == nullptr)
		{
			return false;
		}

		return m_parent->IsChildOf(parent);
	}

	Frame::Pointer Frame::FindChild(const std::string& name)
	{
		for (auto& child : m_children)
		{
			if (child->GetName() == name)
			{
				return child;
			}

			if (auto found = child->FindChild(name))
			{
				return found;
			}
		}

		return nullptr;
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
			gx.SetTransformMatrix(TransformType::Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 100.0f));
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
				hasClipRectSet = true;

				// Set clip rect
				const Rect clipRect = GetAbsoluteFrameRect();
				gx.SetClipRect(clipRect.left, clipRect.top, clipRect.right - clipRect.left, clipRect.bottom - clipRect.top);
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

		if (hasClipRectSet)
		{
			gx.ResetClipRect();
		}
	}

	void Frame::Update(float elapsed)
	{
		if (!m_visible)
		{
			return;
		}

		// Try to call the renderer's update method if we have a valid renderer
		if (m_renderer != nullptr)
		{
			m_renderer->Update(elapsed);
		}

		if (m_onUpdate.is_valid())
		{
			try
			{
				m_onUpdate(this, elapsed);
			}
			catch(const luabind::error& e)
			{
				ELOG("Error calling OnUpdate: " << e.what());
			}
		}

		// Update child frames
		for (auto& child : m_children)
		{
			child->Update(elapsed);
		}
	}

	void Frame::AddChild(Frame::Pointer frame)
	{
		// We can't add ourself as child frame
		if (frame.get() == this)
		{
			ELOG("Frame can't be it's own child frame!");
			return;
		}

		// Add to the list of children
		m_children.push_back(frame);

		// Register ourself as parent frame
		frame->m_parent = this;

		if (frame->m_onLoad.is_valid())
		{
			frame->m_onLoad(frame.get());
		}
	}

	void Frame::RemoveAllChildren()
	{
		// Clear children vector
		m_children.clear();

		// Invalidate
		m_needsLayout = true;
		m_needsRedraw = true;
	}

	void Frame::OnKeyUp(Key key)
	{
		// Tab
		if (key == 9)
		{
			OnTabPressed();
		}
		// Enter
		else if (key == 13)
		{
			OnEnterPressed();
		}
		else if (key == 0x20)
		{
			OnSpacePressed();
		}
		else if (key == 0x1B)
		{
			OnEscapePressed();
		}

	}

	void Frame::OnTabPressed()
	{
		if (m_onTabPressed.is_valid())
		{
			m_onTabPressed(this);
			abort_emission();
		}
	}

	void Frame::OnEnterPressed()
	{
		if (m_onEnterPressed.is_valid())
		{
			m_onEnterPressed(this);
			abort_emission();
		}
	}

	void Frame::OnSpacePressed()
	{
		if (m_onSpacePressed.is_valid())
		{
			m_onSpacePressed(this);
			abort_emission();
		}
	}

	void Frame::OnEscapePressed()
	{
		if (m_onEscapePressed.is_valid())
		{
			m_onEscapePressed(this);
			abort_emission();
		}
	}

	void Frame::SetOnClick(const luabind::object& func)
	{
		m_onClick = func;

		if (m_onClick)
		{
			m_focusable = true;
		}
	}

	void Frame::OnShow()
	{
		if (m_onShow)
		{
			try
			{
				m_onShow(this);
			}
			catch (const luabind::error& e)
			{
				ELOG("Error calling " << GetName() << ":OnShow: " << e.what());
			}
		}

		for(const auto& child : m_children)
		{
			child->OnShow();
		}
	}

	void Frame::OnHide()
	{
		if (m_onHide)
		{
			try
			{
				m_onHide(this);
			}
			catch (const luabind::error& e)
			{
				ELOG("Error calling " << GetName() << ":OnHide: " << e.what());
			}
		}

		for (const auto& child : m_children)
		{
			child->OnHide();
		}
	}

	void Frame::OnMouseEnter()
	{
		if (m_onEnter.is_valid())
		{
			try
			{
				m_onEnter(this);
			}
			catch (const luabind::error& e)
			{
				ELOG("Error calling " << GetName() << ":OnEnter: " << e.what());
			}
		}
	}

	void Frame::OnMouseLeave()
	{
		if (m_onLeave.is_valid())
		{
			try
			{
				m_onLeave(this);
			}
			catch (const luabind::error& e)
			{
				ELOG("Error calling " << GetName() << ":OnLeave: " << e.what());
			}
		}
	}

	void Frame::OnClick(MouseButton button)
	{
		static const char* buttonNames[] = { "LEFT", "RIGHT", "MIDDLE", "BUTTON4", "BUTTON5" };

		if (m_onClick.is_valid())
		{
			try
			{
				const char* buttonName = nullptr;
				switch(button)
				{
				case Left:
					buttonName = buttonNames[0];
					break;
				case Right:
					buttonName = buttonNames[1];
					break;
				case Middle:
					buttonName = buttonNames[2];
					break;
				case Button4:
					buttonName = buttonNames[3];
					break;
				case Button5:
					buttonName = buttonNames[4];
					break;
				}

				m_onClick(this, buttonName);
			}
			catch(const luabind::error& e)
			{
				ELOG("Error calling " << GetName() << ":OnClick: " << e.what());
			}
		}

		Clicked();
	}

	Rect Frame::GetRelativeFrameRect(const bool withScale)
	{
		// Use the internal size property as the default value
		Size mySize = GetPixelSize();
		if (withScale)
		{
			mySize *= FrameManager::Get().GetUIScaleSize();
		}

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
		{
			return m_absRectCache;
		}
		
		// First, obtain the relative frame rect
		m_absRectCache = GetRelativeFrameRect();

		// This rect will contain the absolute parent rectangle
		const Rect parentRect = GetParentRect();
		
		// Add parent rect offset to the relative rect
		m_absRectCache.Offset(parentRect.GetPosition());

		if (!AnchorsSatisfyPosition())
		{
			m_absRectCache.Offset(m_position * FrameManager::Get().GetUIScale());
		}

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

		OnAreaChanged(m_absRectCache);

		// Return the current rect
		return m_absRectCache;
	}

	FontPtr Frame::GetFont() const
	{
		if (m_font)
		{
			return m_font;
		}

		if (m_parent)
		{
			return m_parent->GetFont();
		}

		// No font!
		return nullptr;
	}

	float Frame::GetOpacity(bool inherit) const
	{
		if (inherit && m_parent)
		{
			return m_opacity * m_parent->GetOpacity(true);
		}

		return m_opacity;
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
			if (!(m_flags & static_cast<uint32>(FrameFlags::ManualResetBuffer)))
			{
				m_geometryBuffer.Reset();
			}

			// Signal rendering started
			RenderingStarted();

			// If there is a renderer created, use it to render this frame, otherwise call the frame's internal
			// PopulateGeometryBuffer virtual method.
			if (m_renderer != nullptr)
			{
				Color color = m_color;
				color.SetAlpha(GetOpacity(true));
				m_renderer->Render(optional<Color>(color));
			}
			else
			{
				PopulateGeometryBuffer();
			}

			// mark ourselves as no longer needed a redraw.
			m_needsRedraw = false;

			// Signal rendering ended
			RenderingEnded();
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

		if (m_parent == nullptr)
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

	void Frame::OnClippedByParentChanged(const Property& property)
	{
		SetClippedByParent(property.GetBoolValue());
	}

	void Frame::OnTextPropertyChanged(const Property & property)
	{
		SetText(property.GetValue());
	}

	void Frame::OnFocusablePropertyChanged(const Property & property)
	{
		m_focusable = property.GetBoolValue();

		// If this frame is no longer focusable and has input captured, release it
		if (!m_focusable && HasInputCaptured())
		{
			ReleaseInput();
		}
	}

	void Frame::OnEnabledPropertyChanged(const Property & property)
	{
		SetEnabled(property.GetBoolValue());
	}

	void Frame::OnVisiblePropertyChanged(const Property & property)
	{
		SetVisible(property.GetBoolValue());
	}

	void Frame::OnFontPropertyChanged(const Property & property)
	{
		// Reset current font
		m_font.reset();

		// Try to find named font
		auto* fontMap = FrameManager::Get().GetFontMap(property.GetValue());
		if (!fontMap)
		{
			return;
		}

		// Try to load font
		m_font = FontManager::Get().CreateOrRetrieve(fontMap->FontFile, fontMap->Size, fontMap->Outline, fontMap->ShadowX, fontMap->ShadowY);

		// Invalidate
		m_needsRedraw = true;
		m_needsLayout = true;

		// Invalidate all children as they might depend on our font
		InvalidateChildren(true);
	}

	void Frame::OnColorPropertyChanged(const Property& property)
	{
		argb_t argb;

		std::stringstream colorStream;
		colorStream.str(property.GetValue());
		colorStream.clear();
		colorStream >> std::hex >> argb;

		m_color = argb;

		Invalidate(false);
	}

	void Frame::OnClickablePropertyChanged(const Property& property)
	{
		m_clickable = property.GetBoolValue();
	}

	GeometryBuffer & Frame::GetGeometryBuffer()
	{ 
		return m_geometryBuffer;
	}

	void Frame::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// Capture the input if this frame is focusable
		if (m_focusable)
		{
			CaptureInput();
		}

		// Simply raise the signal
		MouseDown(MouseEventArgs(buttons, position.x, position.y));

		if (m_onClick.is_valid())
		{
			abort_emission();
		}
	}

	void Frame::OnMouseUp(MouseButton button, int32 buttons, const Point & position)
	{
		if (const Rect frame = GetAbsoluteFrameRect(); frame.IsPointInRect(position))
		{
			// Trigger lua clicked event handler if there is any
			OnClick(button);
		}

		// Simply raise the signal
		MouseUp(MouseEventArgs(buttons, position.x, position.y));

		if (m_onClick.is_valid())
		{
			abort_emission();
		}
	}
}
