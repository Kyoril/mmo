#pragma once

#include "selectable.h"

#include "base/signal.h"

#include <list>
#include <memory>

namespace mmo
{
	class Selection final
	{
	public:
		typedef signal<void()> SelectionChangedSignal;

		SelectionChangedSignal changed;

		typedef size_t Index;
		typedef std::list<std::unique_ptr<Selectable>> SelectionList;

	public:
		void Clear();

		void AddSelectable(std::unique_ptr<Selectable> selectable);

		void RemoveSelectable(Index index);

		const size_t GetSelectedObjectCount() const;

		const SelectionList& GetSelectedObjects() const;

		const bool IsEmpty() const;

	private:
		SelectionList m_selected;
	};
}
