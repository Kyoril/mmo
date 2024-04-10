// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "geometry_buffer.h"
#include "rect.h"
#include "anchor_point.h"
#include "frame_renderer.h"
#include "mouse_event_args.h"
#include "property.h"
#include "key.h"
#include "font.h"

#include "imagery_section.h"
#include "state_imagery.h"

#include "base/typedefs.h"
#include "base/utilities.h"
#include "base/signal.h"
#include "log/default_log_levels.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "lua.hpp"
#include "luabind/object.hpp"


namespace mmo
{
	/// Enumerated type used for specifying Frame::update mode to be used. Note that
	/// the setting specified will also have an effect on child window content; for
	/// Never and Visible, if the parent's update function is not called, then no
	/// child frame will have it's update function called either - even if it
	/// specifies Always as it's FrameUpdateMode.
	enum class FrameUpdateMode
	{
		/// Always call the Frame::Update function for this frame.
		Always,
		/// Never call the Frame::Update function for this frame.
		Never,
		/// Only call the Frame::Update function for this frame if it is visible.
		Visible,
	};

	enum class FrameFlags
	{
		None = 0,

		ManualResetBuffer = 0x01
	};


	/// This is the base class to represent a simple frame.
	class Frame
		: public std::enable_shared_from_this<Frame>
	{
	public:
		typedef std::shared_ptr<Frame> Pointer;

	public:
		/// Fired when rendering of the frame began.
		signal<void()> RenderingStarted;
		/// Fired when rendering of the frame ended.
		signal<void()> RenderingEnded;
		/// Fired when the text of this frame was changed.
		signal<void()> TextChanged;
		/// Fired when the enabled state of this frame was changed.
		signal<void()> EnabledStateChanged;
		/// Fired when the frame's visibility changed.
		signal<void()> VisibilityChanged;
		/// Fired when the mouse button was pressed on this frame.
		signal<void(const MouseEventArgs& args)> MouseDown;
		/// Fired when the mouse button was released after being pressed on this frame.
		signal<void(const MouseEventArgs& args)> MouseUp;

	public:
		Frame(const std::string& type, const std::string& name);
		virtual ~Frame() = default;

	public:
		/// Called to copy this frame's properties over to another frame.
		virtual void Copy(Frame& other);

	public:
		/// Adds a property definition to this frame.
		Property& AddProperty(const std::string& name, std::string defaultValue = "");
		/// Tries to get a property by name.
		Property* GetProperty(const std::string& name);
		/// Removes a property from the frame.
		bool RemoveProperty(const std::string& name);

	public:
		// Lua wrappers
		bool Lua_IsVisible() const;
		inline void SetUserData(const luabind::object& data) { m_userData = data; }
		inline const luabind::object& GetUserData() const { return m_userData; }
		Pointer Clone();

	public:
		/// Register a lua function as an event handler.
		void RegisterEvent(const std::string& name, const luabind::object& fn);

		/// Removes a registered event function for a given event name.
		void UnregisterEvent(const std::string& name);

		/// 
		template<typename ...Args>
		bool TriggerEvent(const std::string& name, Args&&... args)
		{
			const auto it = m_eventFunctionsByName.find(name);
			if (it == m_eventFunctionsByName.end())
				return false;

			try
			{
				it->second(std::forward<Args>(args)...);
			}
			catch (const luabind::error& e)
			{
				ELOG("Lua error: " << e.what());
			}
			return true;
		}

		void SetOnLoad(const luabind::object& fn);

		void SetOnUpdate(const luabind::object& fn);

		void OnLoad();

	public:
		/// Adds a new state imagery.
		void AddImagerySection(ImagerySection& section);
		/// Removes a state imagery by name.
		void RemoveImagerySection(const std::string& name);
		/// Gets an imagery section by name.
		/// @param name Name of the imagery section.
		/// @return nullptr if no such imagery section exists.
		ImagerySection* GetImagerySectionByName(const std::string& name) const;
		/// Adds a new state imagery.
		void AddStateImagery(StateImagery& stateImagery);
		/// Removes a state imagery by name.
		void RemoveStateImagery(const std::string& name);
		/// Gets a state imagery by name. Don't keep a pointer on the result, as it is destroyed when
		/// the style instance is destroyed!
		/// @param name Name of the state imagery.
		/// @return nullptr if no such state imagery exists.
		StateImagery* GetStateImageryByName(const std::string& name) const;

	public:
		/// Gets the type name of this frame.
		inline const std::string& GetType() const { return m_type; }
		/// Gets the text of this frame.
		inline const std::string& GetText() const { return m_text; }
		/// Gets the text that is actually rendered.
		virtual const std::string& GetVisualText() const { return m_text; }
		/// Sets the text of this frame.
		void SetText(std::string text);
		/// Determines whether the frame is currently visible.
		/// @param localOnly If set to true, the parent frame's visibility setting is ignored.
		/// @returns true, if this frame is currently visible.
		bool IsVisible(bool localOnly = true) const;
		/// Sets the visibility of this frame.
		/// @param visible Whether the frame will be visible or not.
		void SetVisible(bool visible);
		/// Syntactic sugar for SetVisible(true).
		inline void Show() { SetVisible(true); }
		/// Syntactic sugar for SetVisible(false).
		inline void Hide() { SetVisible(false); }
		/// Determines whether the frame is currently enabled.
		/// @param localOnly If set to true, the parent frame's enabled setting is ignored.
		/// @returns true, if this frame is currently enabled.
		bool IsEnabled(bool localOnly = true) const;
		/// Enables or disables this frame.
		/// @param enable Whether the frame should be enabled or disabled.
		void SetEnabled(bool enable);
		/// Syntactic sugar for SetEnabled(true).
		inline void Enable() { SetEnabled(true); }
		/// Syntactic sugar for SetEnabled(false).
		inline void Disable() { SetEnabled(false); }
		/// Determines if this window is the root frame.
		bool IsRootFrame() const;
		/// Sets the renderer by name.
		void SetRenderer(const std::string& rendererName);
		/// Gets the renderer instance if there is any.
		inline const FrameRenderer* GetRenderer() const { return m_renderer.get(); }
		/// Determines whether this frame is clipped by the parent frame.
		inline bool IsClippedByParent() const { return m_clippedByParent; }
		/// Sets whether this frame is clipped by it's parent frame.
		void SetClippedByParent(bool clipped);
		/// Returns the position of this frame set by the position property. Keep in mind, that
		/// this might not represent the actual frame position on screen, as Anchors have higher
		/// priority than this setting, which is only a fallback if no anchors are set.
		inline const Point& GetPosition() const { return m_position; }
		/// Sets the position of this frame. Note that anchors have higher priority, so this function
		/// might have no effect at all.
		void SetPosition(const Point& position);
		/// Determines if the set anchors can be used to determine the frame's x position.
		bool AnchorsSatisfyXPosition() const;
		/// Determines if the anchors can be used to determine the frame's y position.
		bool AnchorsSatisfyYPosition() const;
		/// Determines if the set anchors can be used to determine the frame position.
		inline bool AnchorsSatisfyPosition() const { return AnchorsSatisfyXPosition() && AnchorsSatisfyYPosition(); }
		/// Determines if the width of this frame can be derived from anchors.
		bool AnchorsSatisfyWidth() const;
		/// Determines if the height of this frame can be derived from anchors.
		bool AnchorsSatisfyHeight() const;
		/// Determines if the set anchors can be used to determine the frame size.
		inline bool AnchorsSatisfySize() const { return AnchorsSatisfyWidth() && AnchorsSatisfyHeight(); }
		/// Sets an anchor for this frame.
		void SetAnchor(AnchorPoint point, AnchorPoint relativePoint = AnchorPoint::None, Pointer relativeTo = nullptr, float offset = 0.0f);

		/// Sets the size for this frame.
		void SetSize(float width, float height);

		/// Sets an width for this frame.
		void SetWidth(float width);

		/// Sets the height for this frame.
		void SetHeight(float height);
		
		float GetWidth() const { return m_pixelSize.width; }

		float GetHeight() const { return m_pixelSize.height; }

		/// Clears an anchor point.
		void ClearAnchor(AnchorPoint point);
		/// Gets the parent frame.
		inline Frame* GetParent() const { return m_parent; }
		/// Determines whether the frame is currently hovered.
		bool IsHovered() const;
		/// Invalidates the frame, causing a complete redraw the next time it is rendered.
		void Invalidate(bool includeLayout = true) noexcept;
		/// Tries to retrieve a child frame at the given position.
		Pointer GetChildFrameAt(const Point& position, bool allowDisabled = true);
		/// Makes this frame capture input events.
		void CaptureInput();
		/// Stops this frame from receiving input events.
		void ReleaseInput();
		/// Determines whether this frame currently received input events.
		bool HasInputCaptured() const;
		/// Called when the input was captured.
		virtual void OnInputCaptured() {}
		/// Called when the input was released.
		virtual void OnInputReleased() {}
		/// Whether this frame can receive the input focus.
		inline bool IsFocusable() const noexcept { return m_focusable; }
		/// Invalidates all children.
		void InvalidateChildren(bool recursive = true);
		/// Calculates the intrinsic frame size.
		Size GetIntrinsicSize();

		void SetProperty(const std::string& name, const std::string& value);

		bool IsChildOf(Frame& parent) const;

		Pointer FindChild(const std::string& name);

	public:
		/// Gets a string object holding the name of this frame.
		inline const std::string& GetName() const { return m_name; }

		/// Renders the frame and it's child frames if it needs to be rendered.
		void Render();

		/// Updates animation logic of the frame and it's component (like the frame renderer if there is any). 
		/// Should be called once per frame.
		virtual void Update(float elapsed);

		/// Gets the pixel size of this frame.
		virtual inline Size GetPixelSize() const { return m_pixelSize; }

		/// Sets the pixel size of this frame.
		virtual inline void SetPixelSize(Size newSize) { m_pixelSize = newSize; m_needsRedraw = true; }

		/// Adds a frame to the list of child frames.
		virtual void AddChild(Pointer frame);
		
		/// Adds a frame to the list of child frames.
		virtual uint32 GetChildCount() const { return m_children.size(); }

		/// Adds a frame to the list of child frames.
		virtual Frame* GetChild(uint32 index) const { return index < m_children.size() ? m_children[index].get() : nullptr; }

		/// Removes all child frames.
		void RemoveAllChildren();

		/// Gets the geometry buffer that is used to render this frame.
		GeometryBuffer& GetGeometryBuffer();

		/// 
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position);


		/// 
		virtual void OnMouseUp(MouseButton button, int32 buttons, const Point& position);

		/// 
		virtual void OnKeyDown(Key key) {}

		/// 
		virtual void OnKeyChar(uint16 codepoint) {}

		/// 
		virtual void OnKeyUp(Key key);

	public:
		virtual Rect GetRelativeFrameRect();
		/// Used to get the frame rectangle.
		virtual Rect GetAbsoluteFrameRect();
		/// Gets the font of this frame, or it's parent frames.
		FontPtr GetFont() const;

		inline uint32 GetFlags() const noexcept { return m_flags; }
		inline void AddFlags(uint32 flags) noexcept { m_flags |= flags; }
		inline void RemoveFlags(uint32 flags) noexcept { m_flags &= ~flags; }
		inline void SetFlags(uint32 flags) noexcept { m_flags = flags; }

		void SetOnTabPressed(const luabind::object& func) { m_onTabPressed = func; }
		void SetOnEnterPressed(const luabind::object& func) { m_onEnterPressed = func; }

	protected:
		virtual void DrawSelf();

		void BufferGeometry();

		void QueueGeometry();
		/// Allows for custom geometry buffer population for custom frame classes.
		virtual void PopulateGeometryBuffer() {}
		/// Gets the parent rectangle.
		Rect GetParentRect();
		/// Executed when the text was changed.
		virtual void OnTextChanged();

		void OnTabPressed();
		void OnEnterPressed();

	private:
		/// Executed when the clippedByParent property was changed.
		void OnClippedByParentChanged(const Property& property);
		/// Executed when the text property was changed.
		void OnTextPropertyChanged(const Property& property);
		/// Executed when the focusable property was changed.
		void OnFocusablePropertyChanged(const Property& property);
		/// Executed when the enabled property was changed.
		void OnEnabledPropertyChanged(const Property& property);
		/// Executed when the visible property was changed.
		void OnVisiblePropertyChanged(const Property& property);
		/// 
		void OnFontPropertyChanged(const Property& property);

	protected:
		typedef std::vector<Pointer> ChildList;

		/// The type name of this frame.
		std::string m_type;
		/// The name of this frame. Must be unique.
		std::string m_name;
		/// Whether the frame needs to be fully redrawn (geometry recreated).
		bool m_needsRedraw;
		/// The text of this frame that is rendered by TextComponents in the frame's style.
		std::string m_text;
		/// Whether the frame is visibile and thus can be rendered (can be used to hide the frame).
		bool m_visible;
		/// Whether the frame is enabled and thus responds to click events and can receive the input focus.
		bool m_enabled;
		/// Whether the frame is clipped by it's parent frame.
		bool m_clippedByParent;
		/// The frame's position if no or not enough anchor points are set.
		Point m_position;
		/// A container of attached child frames.
		ChildList m_children;
		/// The geometry buffer of this frame.
		GeometryBuffer m_geometryBuffer;
		/// The current size of this frame in pixels.
		Size m_pixelSize = { 200.0f, 96.0f };
		/// The parent frame (or nullptr if there is no parent frame).
		Frame* m_parent;
		/// Window renderer instance.
		std::unique_ptr<FrameRenderer> m_renderer;
		/// A map of anchor points.
		std::map<AnchorPoint, std::unique_ptr<Anchor>> m_anchors;
		/// Whether the layout needs to be recalculated.
		bool m_needsLayout;
		/// The cached absolute frame rect.
		Rect m_absRectCache;
		/// Contains all state imageries of this style by name.
		std::map<std::string, StateImagery> m_stateImageriesByName;
		/// Contains all state imagery sections of this style by name.
		std::map<std::string, ImagerySection> m_sectionsByName;
		/// Event function callbacks by name.
		std::map<std::string, luabind::object, StrCaseIComp> m_eventFunctionsByName;
		/// 
		std::map<std::string, Property, StrCaseIComp> m_propertiesByName;
		/// Whether this frame can receive the input focus on click.
		bool m_focusable;
		/// Sets the font of this frame.
		FontPtr m_font;
		/// User data used by lua.
		luabind::object m_userData{};

		luabind::object m_onLoad{};

		luabind::object m_onUpdate{};

		uint32 m_flags = 0;

		bool m_loaded = false;

		luabind::object m_onEnterPressed;

		luabind::object m_onTabPressed;

	protected:
		scoped_connection_container m_propConnections;
	};

	/// A shared pointer of a frame.
	typedef Frame::Pointer FramePtr;
}
