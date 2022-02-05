// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <functional>

#include "node_registry.h"

#include "base/id_generator.h"

#include <memory>

namespace io
{
	class Reader;
	class Writer;
}

namespace mmo
{
	class NodeRegistry;

	/// @brief Interface which describes a load context which is able to collect post-load actions for later execution.
	///	       This is required as we first need to load all nodes, before we can start to link their respective pins
	///		   together, otherwise the nodes for the pins would not yet exist.
	class IMaterialGraphLoadContext
	{
	public:
		/// @brief A post load action. If this returns false, loading will fail.
		typedef std::function<bool()> PostLoadAction;

	protected:
		/// @brief Protected default constructor to prevent public instantiation.
		explicit IMaterialGraphLoadContext() = default;

	public:
		/// @brief Virtual default destructor because of inheritance.
		virtual ~IMaterialGraphLoadContext() = default;

	public:
		/// @brief Adds an action to the context for execution after loading has been done.
		/// @param action The action to perform. The action will be moved.
		virtual void AddPostLoadAction(PostLoadAction&& action) = 0;
	};

	/// @brief This class manages a material graph, which contains nodes with instructions of a material. It is used by a
	///	       MaterialCompiler instance to generate required shaders. This is an editor-only class, although the content's
	///		   of the MaterialGraph will be stored in the Material asset files. They might be removed during content deployment
	///		   however, as they aren't required by the game client during runtime.
	class MaterialGraph
	{
	public:
	    /// @brief Creates a new instance of the MaterialGraph class and initializes it.
	    /// @param nodeRegistry An optional existing node registry to use. If nullptr, a new one is created.
	    MaterialGraph(std::shared_ptr<NodeRegistry> nodeRegistry = nullptr);

	    /// @brief Copies an existing MaterialGraph.
	    /// @param other The MaterialGraph instance to copy.
	    MaterialGraph(const MaterialGraph& other);

	    /// @brief Moves an existing MaterialGraph instance to a new memory local value.
	    /// @param other The MaterialGraph instance to move.
	    MaterialGraph(MaterialGraph&& other) noexcept;

		/// @brief Default destructor which frees memory.
	    ~MaterialGraph();

	public:
		io::Writer& Serialize(io::Writer& writer) const;

		io::Reader& Deserialize(io::Reader& reader, IMaterialGraphLoadContext& context);

	public:
	    /// @brief Copy assignment operator override.
	    /// @param other The MaterialGraph instance to copy.
	    /// @return A reference on this object.
	    MaterialGraph& operator=(const MaterialGraph& other);
		
	    /// @brief Move assignment operator override.
	    /// @param other The MaterialGraph instance to move.
	    /// @return A reference on this object.
	    MaterialGraph& operator=(MaterialGraph&& other) noexcept;

	public:
		/// @brief Creates a new node based on a given node type.
		/// @tparam T Type of the node to create.
		/// @return The new node or nullptr if the node could not be created.
		template <typename T>
	    T* CreateNode()
	    {
	        if (auto node = CreateNode(T::GetStaticTypeInfo().id))
	        {
		        return static_cast<T*>(node);
	        }

			return nullptr;
	    }

	    /// @brief Creates a new node by it's node type id.
	    /// @param nodeTypeId The type id of the node to create.
	    /// @return The new node or nullptr if the node could not be created.
	    Node* CreateNode(uint32 nodeTypeId);

	    /// @brief Creates a new node by it's type name.
	    /// @param nodeTypeName The node's type name.
	    /// @return The new node or nullptr if the node could not be created.
	    Node* CreateNode(std::string_view nodeTypeName);

	    /// @brief Deletes the given node, freeing memory in advance and cutting all links.
	    /// @param node The node to delete.
	    void DeleteNode(const Node* node);

	    /// @brief Removes a pin from the list of available pins in the material.
	    /// @param pin The pin to remove.
	    void ForgetPin(const Pin* pin);

	    /// @brief Clears the whole material graph, resetting it into an empty state.
	    void Clear(bool destroy = false);

	    /// @brief Gets a view on the nodes.
	    /// @return The nodes as view.
	    std::span<Node*> GetNodes() { return m_nodes; }

	    /// @brief Gets a view on the current nodes.
	    /// @return The nodes as view.
	    [[nodiscard]] std::span<const Node* const> GetNodes() const;

	    /// @brief Gets a view on the pins.
	    /// @return The pins as view.
	    std::span<Pin*> GetPins() { return m_pins; }

	    /// @brief Gets a view on the pins.
	    /// @return The pins as view.
	    [[nodiscard]] std::span<const Pin* const> GetPins() const;

		/// @brief Finds a node by it's id.
		/// @param nodeId The id of the node to look for.
		/// @return nullptr if the node could not be found, otherwise a pointer to the node.
		Node* FindNode(uint32 nodeId);
		
		/// @brief Finds a node by it's id.
		/// @param nodeId The id of the node to look for.
		/// @return nullptr if the node could not be found, otherwise a pointer to the node.
	    [[nodiscard]] const Node* FindNode(uint32 nodeId) const;

		/// @brief Finds a pin by it's id.
		/// @param pinId The id of the pin to look for.
		/// @return nullptr if the pin could not be found, otherwise a pointer to the pin.
		Pin* FindPin(uint32 pinId);
		
		/// @brief Finds a pin by it's id.
		/// @param pinId The id of the pin to look for.
		/// @return nullptr if the pin could not be found, otherwise a pointer to the pin.
	    [[nodiscard]] const Pin* FindPin(uint32 pinId) const;

		/// @brief Gets the node registry.
	    [[nodiscard]] std::shared_ptr<NodeRegistry> GetNodeRegistry() const { return m_nodeRegistry; }

		/// @brief Creates a new node id.
		/// @param node The node who is asking for a new id.
		/// @return A new unique node id to use.
		uint32 MakeNodeId(Node* node);

	    /// @brief Creates a new pin id.
	    /// @param pin The pin who is asking for a new id.
	    /// @return A new unique pin id to use.
	    uint32 MakePinId(Pin* pin);

	    /// @brief Determines whether the given pin has any link.
	    /// @param pin The pin for which the request is done.
	    /// @return true if the pin has any link.
	    [[nodiscard]] bool HasPinAnyLink(const Pin& pin) const;

	    /// @brief Gets a list of linked pins for the given pin.
	    /// @param pin The pin whose linked pins should be returned.
	    /// @return A list of pointers to linked pins for the given pin.
	    [[nodiscard]] std::vector<Pin*> FindPinsLinkedTo(const Pin& pin) const;


		void Compile(MaterialCompiler& compiler) const;

	private:
	    std::shared_ptr<NodeRegistry> m_nodeRegistry;
	    IdGenerator<uint32> m_idGenerator { 1 };
	    std::vector<Node*> m_nodes;
	    std::vector<Pin*> m_pins;
		Node* m_rootNode { nullptr };
	};

}
