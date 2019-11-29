#pragma once

#include "frame_ui/frame.h"
//
#include "base/non_copyable.h"
#include "base/utilities.h"
//
#include <string>
#include <functional>
#include <map>


namespace mmo
{
	class FrameManager final : public NonCopyable
	{
	public:
		typedef std::function<FramePtr(FramePtr parent)> FrameFactory;

	private:
		/// Contains a hash map of all registered frame factories.
		std::map<std::string, FrameFactory, StrCaseIComp> m_frameFactories;

	private:
		FrameManager()
		{}

	public:
		/// Singleton getter method.
		static FrameManager& Get();

	public:
		/// Loads files based on a given input stream with file contents.
		void LoadUIFile(const std::string& filename);

	public:
		FramePtr Create(const std::string& name);
		FramePtr CreateOrRetrieve(const std::string& name);
		FramePtr Find(const std::string& name);

		void SetTopFrame(const FramePtr& topFrame);

		void ResetTopFrame();

		void Draw() const;

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
	};
}