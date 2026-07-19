// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_undo_stack.h"

#include "terrain/terrain.h"

namespace mmo
{
	void TerrainUndoStack::Push(String label, std::vector<terrain::TerrainRegionSnapshot> before)
	{
		// Drop invalid snapshots (e.g. selections clamped to nothing).
		std::erase_if(before, [](const terrain::TerrainRegionSnapshot& s)
		{
			return !s.IsValid();
		});

		if (before.empty())
		{
			return;
		}

		m_undo.push_back(Entry{ std::move(label), std::move(before) });
		while (m_undo.size() > MaxEntries)
		{
			m_undo.pop_front();
		}

		m_redo.clear();
	}

	const String* TerrainUndoStack::GetUndoLabel() const
	{
		return m_undo.empty() ? nullptr : &m_undo.back().label;
	}

	const String* TerrainUndoStack::GetRedoLabel() const
	{
		return m_redo.empty() ? nullptr : &m_redo.back().label;
	}

	void TerrainUndoStack::Undo(terrain::Terrain& terrain)
	{
		if (m_undo.empty())
		{
			return;
		}

		Entry entry = std::move(m_undo.back());
		m_undo.pop_back();

		m_redo.push_back(CaptureCounterpart(terrain, entry));
		Apply(terrain, entry);
	}

	void TerrainUndoStack::Redo(terrain::Terrain& terrain)
	{
		if (m_redo.empty())
		{
			return;
		}

		Entry entry = std::move(m_redo.back());
		m_redo.pop_back();

		m_undo.push_back(CaptureCounterpart(terrain, entry));
		while (m_undo.size() > MaxEntries)
		{
			m_undo.pop_front();
		}
		Apply(terrain, entry);
	}

	void TerrainUndoStack::Clear()
	{
		m_undo.clear();
		m_redo.clear();
	}

	TerrainUndoStack::Entry TerrainUndoStack::CaptureCounterpart(terrain::Terrain& terrain, const Entry& entry)
	{
		Entry counterpart;
		counterpart.label = entry.label;
		counterpart.snapshots.reserve(entry.snapshots.size());
		for (const auto& snapshot : entry.snapshots)
		{
			counterpart.snapshots.push_back(terrain.CaptureRegion(snapshot.rect));
		}
		return counterpart;
	}

	void TerrainUndoStack::Apply(terrain::Terrain& terrain, const Entry& entry)
	{
		for (const auto& snapshot : entry.snapshots)
		{
			terrain.ApplyRegion(snapshot, snapshot.rect.minX, snapshot.rect.minZ);
		}
	}
}
