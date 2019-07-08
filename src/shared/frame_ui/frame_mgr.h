#pragma once

#include "frame_ui/frame.h"
//
#include "base/non_copyable.h"
#include "base/utilities.h"
//
#include <string>
#include <map>


namespace mmo
{
	class FrameManager final : public NonCopyable
	{
	private:
		FrameManager()
		{}

	public:
		/// Singleton getter method.
		static FrameManager& Get();

	public:
		FramePtr CreateOrRetrieve(const std::string& name);

		void SetTopFrame(const FramePtr& topFrame);

		void ResetTopFrame();

		void Draw() const;

	private:
		std::map<std::string, FramePtr, StrCaseIComp> m_framesByName;
		FramePtr m_topFrame;
	};
}