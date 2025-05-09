#pragma once

#include "imgui_node_editor.h"

namespace mmo
{
	// Wrapper over flat API for item deletion
	class ItemDeleter final
	{
	public:
	    class NodeDeleter final
	    {
	    public:
	        ax::NodeEditor::NodeId nodeId = ax::NodeEditor::NodeId::Invalid;
			
	    public:
	        bool Accept(const bool deleteLinks = true)
	        {
	        	return ax::NodeEditor::AcceptDeletedItem(deleteLinks);
	        }

	        void Reject()
	        {
				ax::NodeEditor::RejectDeletedItem();
	        }
	    };

	    class LinkDeleter final
	    {
	    public:
	        ax::NodeEditor::LinkId linkId     = ax::NodeEditor::LinkId::Invalid;
	        ax::NodeEditor::PinId  startPinId = ax::NodeEditor::PinId::Invalid;
	        ax::NodeEditor::PinId  endPinId   = ax::NodeEditor::PinId::Invalid;
			
	    public:
	        bool Accept()
	        {
				return ax::NodeEditor::AcceptDeletedItem();
	        }

	        void Reject()
	        {
				ax::NodeEditor::RejectDeletedItem();
	        }
	    };

	    ItemDeleter()
			: m_inDelete(ax::NodeEditor::BeginDelete())
		{
		}

	    ~ItemDeleter()
		{
		    ax::NodeEditor::EndDelete();
		}

	    explicit operator bool() const
		{
		    return m_inDelete;
		}

	    NodeDeleter* QueryDeletedNode()
	    {
		    if (m_inDelete && ax::NodeEditor::QueryDeletedNode(&m_nodeDeleter.nodeId))
		    {
	    		return &m_nodeDeleter;
			}

			return nullptr;
		}

	    LinkDeleter* QueryDeleteLink()
	    {	
		    if (m_inDelete && ax::NodeEditor::QueryDeletedLink(&m_linkDeleter.linkId, &m_linkDeleter.startPinId, &m_linkDeleter.endPinId))
		    {
	    		return &m_linkDeleter;
			}

	        return nullptr;
	    }

	private:
	    bool m_inDelete = false;
	    NodeDeleter m_nodeDeleter;
	    LinkDeleter m_linkDeleter;
	};
}
