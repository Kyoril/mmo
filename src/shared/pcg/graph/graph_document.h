// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graph.h"
#include "node_registry.h"

#include <map>
#include <vector>

namespace mmo::pcg::graph
{
	/// @brief Editable graph with stable editor ids, the model behind the graph editor (no UI dependency).
	///
	///        Pin id = (uint64(nodeId) << 16) | (isOutput << 15) | pinIndex, where pinIndex counts the node
	///        type's pins of that direction first, then the pin names links use that the type does not declare
	///        (sorted). Link id = a hash of (fromPinId, toPinId). Both only depend on the graph content, so they
	///        are the same after Save and Load.
	///
	///        Unknown node types (not in the registry) keep their settings, links and pin names verbatim; their
	///        pins are the names their links use, and those pins accept any kind.
	class GraphDocument
	{
	public:
		/// @brief A decoded pin id.
		struct PinRef
		{
			uint32 node = 0;
			bool isOutput = false;
			uint32 index = 0;
		};

		/// @brief A graph link with its editor ids.
		struct ResolvedLink
		{
			uint64 id = 0;
			uint64 fromPin = 0;
			uint64 toPin = 0;

			/// @brief Index of the link in GetGraph().links.
			size_t index = 0;
		};

		/// @brief Highest pin index a pin id can hold.
		static constexpr uint32 MaxPinIndex = 0x7FFF;

	public:
		/// @brief Creates an empty document that uses the default registry.
		GraphDocument();

	public:
		/// @brief Builds a pin id.
		static uint64 MakePinId(uint32 node, bool isOutput, uint32 index);

		/// @brief Decodes a pin id.
		static PinRef DecodePinId(uint64 pinId);

		/// @brief Builds the id of the link between two pins (never 0).
		static uint64 MakeLinkId(uint64 fromPin, uint64 toPin);

	public:
		/// @brief Replaces the document with a parsed graph file.
		/// @param json The .hpcgg text.
		/// @param registry The node types; must outlive the document.
		/// @param error Receives the parse error on failure.
		/// @return False if the text cannot be parsed; the document is unchanged then.
		bool Load(const String& json, const NodeRegistry& registry, String& error);

		/// @brief Writes the graph as .hpcgg text (WriteGraph); unknown nodes round-trip unchanged.
		String Save() const;

		/// @brief Adds a node of a registered type with every setting at its default.
		/// @return The new node id, or 0 if the type is unknown or no node id is left.
		uint32 AddNode(const String& type, float x, float y);

		/// @brief Removes a node and every link from or to it.
		/// @return False if there is no such node.
		bool RemoveNode(uint32 id);

		/// @brief Whether a link from an output pin to an input pin may be added: both pins exist, the nodes
		///        differ, the kinds are compatible, the link does not exist yet and does not close a cycle.
		///        A single-link input that already has a link is fine (AddLink replaces that link).
		/// @param reason Receives why the link is refused.
		bool CanLink(uint64 fromPin, uint64 toPin, String& reason) const;

		/// @brief Adds a link if CanLink allows it, first removing the link into a single-link input.
		bool AddLink(uint64 fromPin, uint64 toPin);

		/// @brief Removes a link by its id.
		/// @return False if no link has that id.
		bool RemoveLink(uint64 linkId);

		/// @brief Sets a node setting. A setting the node type declares needs its default value's type or a
		///        parameter binding ("$name"); other settings (and every setting of an unknown node) take any value.
		/// @return False if there is no such node or the type does not fit.
		bool SetSetting(uint32 node, const String& name, const AttributeValue& value);

		/// @brief Sets a node's editor position.
		/// @return True if the position changed. Bumps Revision but not EvalRevision.
		bool SetNodePosition(uint32 node, float x, float y);

		/// @brief Gets the graph.
		const Graph& GetGraph() const;

		/// @brief Gets the graph for direct edits (parameters, comments, positions). Always bumps Revision and EvalRevision.
		///        Callers must not add nodes or links through it (use AddNode / AddLink).
		Graph& EditGraph();

		/// @brief Sets a node's free-form comment. Bumps Revision but not EvalRevision (comments do not affect
		///        evaluation).
		/// @return True if the comment changed.
		bool SetNodeComment(uint32 node, const String& comment);

		/// @brief Sets whether the editor shows a node's output in the world (NodeInstance::debug). Bumps Revision
		///        (the flag is saved) but not EvalRevision. Does nothing for a missing node or an unchanged flag.
		void SetNodeDebug(uint32 id, bool on);

		/// @brief Gets a counter that every mutation increases: the dirty / save counter.
		uint64 Revision() const;

		/// @brief Gets a counter that every mutation that can change the evaluation result increases: every
		///        mutation except node positions (SetNodePosition), node comments (SetNodeComment) and debug flags
		///        (SetNodeDebug). EditGraph bumps it too (conservative). Views that evaluate the graph (the debug
		///        view) re-evaluate when it changes, so dragging nodes, editing comments or toggling debug never
		///        re-evaluates.
		uint64 EvalRevision() const;

		/// @brief Whether a node exists and its type is not registered.
		bool IsUnknownNode(uint32 id) const;

		/// @brief Finds a node by id, or nullptr.
		const NodeInstance* FindNode(uint32 id) const;

		/// @brief Gets the node type of a node, or nullptr if there is no such node or the type is unknown.
		const NodeTypeDef* FindNodeType(uint32 id) const;

		/// @brief Gets the registry the document was loaded with.
		const NodeRegistry& GetRegistry() const;

		/// @brief Gets a node's pin names of one direction, in pin index order (empty for missing nodes).
		std::vector<String> GetPinNames(uint32 node, bool isOutput) const;

		/// @brief Gets the id of a node's pin by name, or 0 if the node has no such pin.
		uint64 FindPinId(uint32 node, bool isOutput, const String& name) const;

		/// @brief Gets the declaration of a pin, or nullptr for missing pins and pins the type does not declare.
		const PinDef* FindPinDef(uint64 pinId) const;

		/// @brief Gets the pin name of a pin id, or nullptr if the pin does not exist.
		const String* FindPinName(uint64 pinId) const;

		/// @brief Gets every link whose pins exist, with its ids, in graph order.
		std::vector<ResolvedLink> ResolveLinks() const;

	private:
		/// Pin names links use that a node's type does not declare (sorted), per direction.
		struct ExtraPins
		{
			std::vector<String> inputs;
			std::vector<String> outputs;
		};

	private:
		NodeInstance* FindMutableNode(uint32 id);

		/// Whether `target` is reachable from `start` along links.
		bool Reaches(uint32 start, uint32 target) const;

		void RebuildExtraPins();

		/// Records a mutation: bumps Revision, and EvalRevision if the mutation can change evaluation.
		void Touch(bool affectsEvaluation);

	private:
		const NodeRegistry* m_registry = nullptr;
		Graph m_graph;
		std::map<uint32, ExtraPins> m_extraPins;
		uint64 m_revision = 0;
		uint64 m_evalRevision = 0;
	};
}
