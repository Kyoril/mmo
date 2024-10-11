
#include "selection.h"
#include "base/macros.h"


namespace mmo
{
	void Selection::Clear()
	{
		for (auto& selected : m_selected)
		{
			selected->Deselect();
		}

		m_selected.clear();
		changed();
	}

	void Selection::AddSelectable(std::unique_ptr<Selectable> selectable)
	{
		m_selected.emplace_back(std::move(selectable));
		changed();
	}

	void Selection::RemoveSelectable(Index index)
	{
		ASSERT(index < m_selected.size());

		auto it = m_selected.begin();
		std::advance(it, index);

		(*it)->Deselect();

		m_selected.erase(it);
		changed();
	}

	const size_t Selection::GetSelectedObjectCount() const
	{
		return m_selected.size();
	}

	const Selection::SelectionList& Selection::GetSelectedObjects() const
	{
		return m_selected;
	}

	const bool Selection::IsEmpty() const
	{
		return m_selected.empty();
	}

	void Selection::CopySelectedObjects()
	{

	}
}
