#pragma once

#include <string>
#include <cstdlib>

#include "color.h"

namespace mmo
{
    inline bool ConsumeColourTag(const std::string& txt,
        std::size_t& idx,
        argb_t& currentColor,
        argb_t defaultColor)
    {
        if (idx + 1 >= txt.length() || txt[idx] != '|')
            return false;

        const char directive = txt[idx + 1];

        // |cAARRGGBB -------------------------------------------------------------
        if ((directive == 'c' || directive == 'C') && idx + 9 < txt.length())
        {
            currentColor = static_cast<argb_t>(std::strtoul(txt.substr(idx + 2, 8).c_str(), nullptr, 16));
            idx += 10; // |c + 8 hex digits
            return true;
        }

        // |r — reset -------------------------------------------------------------
        if (directive == 'r' || directive == 'R')
        {
            currentColor = defaultColor;
            idx += 2; // |r
            return true;
        }

        return false; // Not a colour tag
    }
}
