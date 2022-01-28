// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "node_header_renderer.h"

namespace mmo
{
	NodeHeaderRenderer::NodeHeaderRenderer(OnDrawCallback drawCallback)
	    : m_drawList(ImGui::GetWindowDrawList())
		, m_drawCallback(std::move(drawCallback))
	{
		m_splitter.Split(m_drawList, 2);
	    m_splitter.SetCurrentChannel(m_drawList, 1);
	}

	NodeHeaderRenderer::~NodeHeaderRenderer()
	{
	    Commit();
	}

	void NodeHeaderRenderer::Commit()
	{
	    if (m_splitter._Current == 0)
	    {
		    return;
	    }

	    m_splitter.SetCurrentChannel(m_drawList, 0);

	    if (m_drawCallback)
	    {
		    m_drawCallback(m_drawList);
	    }

	    m_splitter.Merge(m_drawList);
	}

	void NodeHeaderRenderer::Discard()
	{
	    if (m_splitter._Current == 1)
	    {
			m_splitter.Merge(m_drawList);   
	    }
	}

}
