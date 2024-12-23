// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui_node_editor.h>

namespace mmo
{
	/// @brief Helper class for building ImGui Node Editor items like links or new nodes.
	class ItemBuilder final
	{
	public:
	    /// @brief This helper class allows to build new nodes.
	    class NodeBuilder final
	    {
	    public:
	        /// @brief The pin id from which the new node will be created.
	        ax::NodeEditor::PinId pinId = ax::NodeEditor::PinId::Invalid;

	    public:
	        /// @brief Tries to accept the new node item.
	        /// @return true on success, false on error.
	        [[nodiscard]] bool Accept() const
	        {
				return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
	        }
			
	        /// @brief Rejects the new node item.
	        void Reject() const
	        {
				ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
	        }
	    };

	    /// @brief This helper class allows to build new links.
	    class LinkBuilder final
	    {
	    public:
	        /// @brief The starting pin id for the new link.
	        ax::NodeEditor::PinId startPinId = ax::NodeEditor::PinId::Invalid;
	        /// @brief The ending pin id for the new link.
	        ax::NodeEditor::PinId endPinId = ax::NodeEditor::PinId::Invalid;

	    public:
	        /// @brief Tries to accept the new link.
	        /// @return true on success, false on error.
	        [[nodiscard]] bool Accept() const
	        {
				return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
	        }

	        /// @brief Rejects the new link.
	        void Reject() const
	        {
				ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
	        }
	    };

	public:
	    /// @brief Creates a new instance of the ItemBuilder class and initializes it. Performs a call to
	    ///	       ax::NodeEditor::BeginCreate and stores the result.
	    ItemBuilder()
			: m_wasCreated(ax::NodeEditor::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight)))
	    {
	    }

		/// @brief Destructor which calls ax::NodeEditor::EndCreate for the previous BeginCreate call.
	    ~ItemBuilder()
	    {
			ax::NodeEditor::EndCreate();
	    }

	public:
	    /// @brief Whether the result from BeginCreate was true or false.
	    explicit operator bool() const { return m_wasCreated; }

	public:
	    /// @brief Starts querying a new node.
	    /// @return The configured node builder to use for accepting or rejecting the new node. nullptr on error.
	    NodeBuilder* QueryNewNode()
	    {
		    if (m_wasCreated && ax::NodeEditor::QueryNewNode(&m_nodeBuilder.pinId))
		    {
		    	return &m_nodeBuilder;
			}

			return nullptr;
	    }

	    /// @brief Starts querying a new link.
	    /// @return The configured link builder to use for accepting or rejecting the link. nullptr on error.
	    LinkBuilder* QueryNewLink()
	    {
		    if (m_wasCreated && ax::NodeEditor::QueryNewLink(&m_linkBuilder.startPinId, &m_linkBuilder.endPinId))
			{
		    	return &m_linkBuilder;
			}

			return nullptr;
	    }

	private:
	    bool m_wasCreated = false;
	    NodeBuilder m_nodeBuilder;
	    LinkBuilder m_linkBuilder;
	};
}
