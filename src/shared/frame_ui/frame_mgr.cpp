#include "frame_mgr.h"

namespace mmo
{
	FrameManager& FrameManager::Get()
	{
		static FrameManager s_frameMgr;
		return s_frameMgr;
	}

	FramePtr FrameManager::CreateOrRetrieve(const std::string& name)
	{
		auto it = m_framesByName.find(name);
		if (it != m_framesByName.end())
		{
			return it->second;
		}

		auto newFrame = std::make_shared<Frame>(name);

		m_framesByName[name] = newFrame;

		return newFrame;
	}

	void FrameManager::SetTopFrame(const FramePtr& topFrame)
	{
		m_topFrame = topFrame;
	}

	void FrameManager::ResetTopFrame()
	{
		m_topFrame.reset();
	}

	void FrameManager::Draw() const
	{
		m_topFrame->Render();
	}


}