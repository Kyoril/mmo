#pragma once

#include <functional>

namespace mmo
{
	bool ListBox(const char* label, int* current_item, std::function<bool(void*, int, const char**)> items_getter, void* data, int items_count, int height_in_items);
}