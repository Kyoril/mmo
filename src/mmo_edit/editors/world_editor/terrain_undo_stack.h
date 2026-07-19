// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "terrain/terrain_region_snapshot.h"

#include <deque>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	/// Bounded undo/redo stack for region-based terrain operations (cut/paste/move/stamp).
	/// Each entry holds the before-state snapshots of every region an operation touched;
	/// undoing captures the current state as the redo entry, then re-applies the snapshots
	/// at their original positions.
	class TerrainUndoStack final : public NonCopyable
	{
	public:
		/// Maximum retained entries; the oldest entry is dropped beyond this.
		static constexpr size_t MaxEntries = 16;

		/// Registers a completed operation. `before` holds the state of each affected region
		/// captured immediately before the operation mutated it. Clears the redo stack.
		void Push(String label, std::vector<terrain::TerrainRegionSnapshot> before);

		/// True if there is an operation to undo.
		[[nodiscard]] bool CanUndo() const
		{
			return !m_undo.empty();
		}

		/// True if there is an operation to redo.
		[[nodiscard]] bool CanRedo() const
		{
			return !m_redo.empty();
		}

		/// Label of the next undo operation, or nullptr.
		[[nodiscard]] const String* GetUndoLabel() const;

		/// Label of the next redo operation, or nullptr.
		[[nodiscard]] const String* GetRedoLabel() const;

		/// Reverts the most recent operation and moves it to the redo stack.
		void Undo(terrain::Terrain& terrain);

		/// Re-applies the most recently undone operation.
		void Redo(terrain::Terrain& terrain);

		/// Drops all undo and redo entries.
		void Clear();

	private:
		struct Entry
		{
			String label;
			std::vector<terrain::TerrainRegionSnapshot> snapshots;
		};

		/// Captures the current terrain state of every region in `entry` (the counterpart
		/// entry that makes the operation reversible in the other direction).
		static Entry CaptureCounterpart(terrain::Terrain& terrain, const Entry& entry);

		/// Applies every snapshot of the entry at its original position.
		static void Apply(terrain::Terrain& terrain, const Entry& entry);

		std::deque<Entry> m_undo;
		std::deque<Entry> m_redo;
	};
}
