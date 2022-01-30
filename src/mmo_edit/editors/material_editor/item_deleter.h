#pragma once

#include <imgui_node_editor.h>

namespace mmo
{
	// Wrapper over flat API for item deletion
	class ItemDeleter final
	{
	public:
	    class NodeDeleter final
	    {
	    public:
	        ax::NodeEditor::NodeId m_NodeId = ax::NodeEditor::NodeId::Invalid;
			
	    public:
	        bool Accept(bool deleteLinks = true)
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
	        ax::NodeEditor::LinkId m_LinkId     = ax::NodeEditor::LinkId::Invalid;
	        ax::NodeEditor::PinId  m_StartPinId = ax::NodeEditor::PinId::Invalid;
	        ax::NodeEditor::PinId  m_EndPinId   = ax::NodeEditor::PinId::Invalid;
			
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
			: m_InDelete(ax::NodeEditor::BeginDelete())
		{
		}

	    ~ItemDeleter()
		{
		    ax::NodeEditor::EndDelete();
		}

	    explicit operator bool() const
		{
		    return m_InDelete;
		}

	    NodeDeleter* QueryDeletedNode()
	    {
		    if (m_InDelete && ax::NodeEditor::QueryDeletedNode(&m_NodeDeleter.m_NodeId))
		    {
	    		return &m_NodeDeleter;
			}

			return nullptr;
		}

	    LinkDeleter* QueryDeleteLink()
	    {	
		    if (m_InDelete && ax::NodeEditor::QueryDeletedLink(&m_LinkDeleter.m_LinkId, &m_LinkDeleter.m_StartPinId, &m_LinkDeleter.m_EndPinId))
		    {
	    		return &m_LinkDeleter;
			}

	        return nullptr;
	    }

	private:
	    bool m_InDelete = false;
	    NodeDeleter m_NodeDeleter;
	    LinkDeleter m_LinkDeleter;
	};
}
