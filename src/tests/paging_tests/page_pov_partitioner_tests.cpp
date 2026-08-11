// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "paging/page_pov_partitioner.h"
#include "paging/page_visibility_listener.h"

#include <algorithm>
#include <set>
#include <vector>

using namespace mmo;

namespace
{
	/// Records visibility changes in order, and separately as sets, so a test can assert on
	/// which pages changed without depending on iteration order.
	class RecordingVisibilityListener final : public IPageVisibilityListener
	{
	public:
		std::vector<std::pair<PagePosition, bool>> events;

		void OnPageVisibilityChanged(const PagePosition& page, bool isVisible) override
		{
			events.emplace_back(page, isVisible);
		}

		void Clear()
		{
			events.clear();
		}

		std::set<std::pair<size_t, size_t>> Shown() const
		{
			return Collect(true);
		}

		std::set<std::pair<size_t, size_t>> Hidden() const
		{
			return Collect(false);
		}

	private:
		std::set<std::pair<size_t, size_t>> Collect(bool visible) const
		{
			std::set<std::pair<size_t, size_t>> result;
			for (const auto& event : events)
			{
				if (event.second == visible)
				{
					result.emplace(event.first[0], event.first[1]);
				}
			}

			return result;
		}
	};

	std::set<std::pair<size_t, size_t>> PagesInSquare(
		const PagePosition& terrainSize, const PagePosition& center, size_t radius)
	{
		std::set<std::pair<size_t, size_t>> result;
		ForEachPageInSquare(terrainSize, center, radius,
			[&result](const PagePosition& page) { result.emplace(page[0], page[1]); });

		return result;
	}
}

TEST_CASE("Distance is symmetric and never underflows", "[paging]")
{
	CHECK(Distance(7, 3) == 4);
	CHECK(Distance(3, 7) == 4);
	CHECK(Distance(5, 5) == 0);

	// The arguments are size_t. A naive first - second would wrap to a huge value here.
	CHECK(Distance(0, 4) == 4);
}

TEST_CASE("IsInRange is a square, not a circle", "[paging]")
{
	const PagePosition center(5, 5);

	CHECK(IsInRange(center, 1, PagePosition(5, 5)));
	CHECK(IsInRange(center, 1, PagePosition(6, 6)));	// diagonal is still in range
	CHECK(IsInRange(center, 1, PagePosition(4, 4)));
	CHECK_FALSE(IsInRange(center, 1, PagePosition(7, 5)));
	CHECK_FALSE(IsInRange(center, 1, PagePosition(5, 7)));
}

TEST_CASE("ForEachPageInSquare covers the full square away from the edges", "[paging]")
{
	const auto pages = PagesInSquare(PagePosition(10, 10), PagePosition(5, 5), 1);

	CHECK(pages.size() == 9);
	CHECK(pages.count({ 4, 4 }) == 1);
	CHECK(pages.count({ 6, 6 }) == 1);
	CHECK(pages.count({ 3, 5 }) == 0);
}

TEST_CASE("ForEachPageInSquare clamps at the origin instead of wrapping", "[paging]")
{
	// begin is max(center, radius) - radius. With unsigned page coordinates a plain
	// center - radius would wrap to near SIZE_MAX and the loop would produce nothing.
	const auto pages = PagesInSquare(PagePosition(10, 10), PagePosition(0, 0), 2);

	CHECK(pages.size() == 9);	// x and y each run 0..2
	CHECK(pages.count({ 0, 0 }) == 1);
	CHECK(pages.count({ 2, 2 }) == 1);
}

TEST_CASE("ForEachPageInSquare clamps at the far edge", "[paging]")
{
	const auto pages = PagesInSquare(PagePosition(4, 4), PagePosition(3, 3), 2);

	// Clipped to x,y in 1..3 rather than running past the last page.
	CHECK(pages.size() == 9);
	CHECK(pages.count({ 3, 3 }) == 1);
	CHECK(pages.count({ 4, 4 }) == 0);
}

TEST_CASE("ForEachPageInSquare yields nothing for an empty terrain", "[paging]")
{
	CHECK(PagesInSquare(PagePosition(0, 0), PagePosition(0, 0), 3).empty());
}

TEST_CASE("ForEachPageInSquare yields a single page at radius zero", "[paging]")
{
	const auto pages = PagesInSquare(PagePosition(10, 10), PagePosition(4, 7), 0);

	CHECK(pages.size() == 1);
	CHECK(pages.count({ 4, 7 }) == 1);
}

TEST_CASE("PagePOVPartitioner shows the initial square on construction", "[paging]")
{
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(PagePosition(10, 10), 1, PagePosition(5, 5), listener);

	CHECK(listener.Hidden().empty());
	CHECK(listener.Shown() == PagesInSquare(PagePosition(10, 10), PagePosition(5, 5), 1));
}

TEST_CASE("PagePOVPartitioner shows a clipped square when constructed at the origin",
	"[paging]")
{
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(PagePosition(10, 10), 2, PagePosition(0, 0), listener);

	CHECK(listener.Shown().size() == 9);
	CHECK(listener.Shown().count({ 0, 0 }) == 1);
}

TEST_CASE("PagePOVPartitioner emits nothing when the center does not move", "[paging]")
{
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(PagePosition(10, 10), 1, PagePosition(5, 5), listener);
	listener.Clear();

	partitioner.UpdateCenter(PagePosition(5, 5));

	CHECK(listener.events.empty());
}

TEST_CASE("PagePOVPartitioner emits only the delta for a one-page step", "[paging]")
{
	const PagePosition size(10, 10);
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(size, 1, PagePosition(5, 5), listener);
	listener.Clear();

	partitioner.UpdateCenter(PagePosition(6, 5));

	// The two squares overlap in a 2x3 block, so only the leaving column is hidden and only
	// the entering column is shown. Nothing in the overlap is touched.
	CHECK(listener.Hidden() == std::set<std::pair<size_t, size_t>>{ { 4, 4 }, { 4, 5 }, { 4, 6 } });
	CHECK(listener.Shown() == std::set<std::pair<size_t, size_t>>{ { 7, 4 }, { 7, 5 }, { 7, 6 } });
}

TEST_CASE("PagePOVPartitioner hides and shows everything on a jump beyond sight range",
	"[paging]")
{
	const PagePosition size(20, 20);
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(size, 1, PagePosition(2, 2), listener);
	listener.Clear();

	partitioner.UpdateCenter(PagePosition(15, 15));

	// Disjoint squares, so every old page goes and every new page arrives.
	CHECK(listener.Hidden() == PagesInSquare(size, PagePosition(2, 2), 1));
	CHECK(listener.Shown() == PagesInSquare(size, PagePosition(15, 15), 1));
}

TEST_CASE("PagePOVPartitioner never re-announces a page still in range", "[paging]")
{
	const PagePosition size(10, 10);
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(size, 2, PagePosition(5, 5), listener);
	listener.Clear();

	partitioner.UpdateCenter(PagePosition(5, 6));

	// A page must never appear in both lists for a single move -- that would make a
	// listener that loads on show and frees on hide do redundant work, or worse, free
	// something it had just loaded.
	const auto shown = listener.Shown();
	for (const auto& page : listener.Hidden())
	{
		CHECK(shown.count(page) == 0);
	}
}

TEST_CASE("PagePOVPartitioner clamps rather than wrapping when moving along the origin edge",
	"[paging]")
{
	const PagePosition size(10, 10);
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(size, 1, PagePosition(0, 0), listener);
	listener.Clear();

	partitioner.UpdateCenter(PagePosition(1, 0));

	// Moving away from the edge only adds the new column; nothing leaves, because the old
	// square was clipped and is wholly contained in the new one.
	CHECK(listener.Hidden().empty());
	CHECK(listener.Shown() == std::set<std::pair<size_t, size_t>>{ { 2, 0 }, { 2, 1 } });
}

TEST_CASE("PagePOVPartitioner tracks its center across successive moves", "[paging]")
{
	const PagePosition size(10, 10);
	RecordingVisibilityListener listener;
	PagePOVPartitioner partitioner(size, 1, PagePosition(5, 5), listener);

	partitioner.UpdateCenter(PagePosition(6, 5));
	listener.Clear();

	// Stepping back has to undo the previous step exactly, which only holds if the
	// partitioner recorded the new center rather than the one it was built with.
	partitioner.UpdateCenter(PagePosition(5, 5));

	CHECK(listener.Hidden() == std::set<std::pair<size_t, size_t>>{ { 7, 4 }, { 7, 5 }, { 7, 6 } });
	CHECK(listener.Shown() == std::set<std::pair<size_t, size_t>>{ { 4, 4 }, { 4, 5 }, { 4, 6 } });
}
