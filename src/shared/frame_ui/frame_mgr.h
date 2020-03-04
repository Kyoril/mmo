// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame.h"

#include "base/non_copyable.h"
#include "base/utilities.h"

#include <string>
#include <functional>
#include <map>


namespace mmo
{
	/// Handler for layout xml.
	class FrameManager final 
		: public NonCopyable
	{
	public:
		typedef std::function<FramePtr(const std::string& name)> FrameFactory;

	private:
		/// Contains a hash map of all registered frame factories.
		std::map<std::string, FrameFactory, StrCaseIComp> m_frameFactories;

	private:
		FrameManager() = default;

	public:
		/// Singleton getter method.
		static FrameManager& Get();

	public:
		/// Initializes the frame manager by registering default frame factories.
		static void Initialize();
		static void Destroy();

	public:
		/// Loads files based on a given input stream with file contents.
		void LoadUIFile(const std::string& filename);

	public:
		/// Creates a new frame using the given type.
		FramePtr Create(const std::string& type, const std::string& name);
		FramePtr CreateOrRetrieve(const std::string& type, const std::string& name);
		FramePtr Find(const std::string& name);
		void SetTopFrame(const FramePtr& topFrame);
		void ResetTopFrame();
		void Draw() const;
		/// Gets the currently hovered frame.
		inline FramePtr GetHoveredFrame() const { return m_hoverFrame; }
		/// Notifies the FrameManager that the mouse cursor has been moved.
		void NotifyMouseMoved(const Point& position);

	public:
		/// Registers a new factory for a certain frame type.
		void RegisterFrameFactory(const std::string& elementName, FrameFactory factory);
		/// Removes a registered factory for a certain frame type.
		void UnregisterFrameFactory(const std::string& elementName);
		/// Removes all registered frame factories.
		void ClearFrameFactories();

	public:
		inline FramePtr GetTopFrame() const { return m_topFrame; }

	private:
		std::map<std::string, FramePtr, StrCaseIComp> m_framesByName;
		FramePtr m_topFrame;
		FramePtr m_hoverFrame;
	};
}