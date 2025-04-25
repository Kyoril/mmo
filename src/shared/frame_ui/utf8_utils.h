// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <cstdint>
#include <stdexcept>

namespace mmo
{
    namespace utf8
    {
        // Get the number of Unicode codepoints in a UTF-8 encoded string
        inline size_t length(const std::string& str)
        {
            size_t length = 0;
            for (size_t i = 0; i < str.size(); ++i)
            {
                // Count leading bytes (those that don't start with 10)
                if ((str[i] & 0xC0) != 0x80)
                    ++length;
            }
            return length;
        }

        // Get the next UTF-8 codepoint from a string starting at given position
        // Returns the codepoint and updates the position to the next character
        inline uint32_t next_codepoint(const std::string& str, size_t& pos)
        {
            if (pos >= str.size())
                return 0;

            uint32_t codepoint = 0;
            unsigned char ch = static_cast<unsigned char>(str[pos++]);
            
            // ASCII character (0-127)
            if (ch < 0x80)
            {
                return ch;
            }
            // 2-byte sequence (128-2047)
            else if ((ch & 0xE0) == 0xC0)
            {
                if (pos < str.size())
                {
                    codepoint = ((ch & 0x1F) << 6) | (static_cast<unsigned char>(str[pos++]) & 0x3F);
                }
            }
            // 3-byte sequence (2048-65535)
            else if ((ch & 0xF0) == 0xE0)
            {
                if (pos + 1 < str.size())
                {
                    codepoint = ((ch & 0x0F) << 12) | 
                                ((static_cast<unsigned char>(str[pos++]) & 0x3F) << 6) | 
                                (static_cast<unsigned char>(str[pos++]) & 0x3F);
                }
            }
            // 4-byte sequence (65536-1114111)
            else if ((ch & 0xF8) == 0xF0)
            {
                if (pos + 2 < str.size())
                {
                    codepoint = ((ch & 0x07) << 18) | 
                                ((static_cast<unsigned char>(str[pos++]) & 0x3F) << 12) | 
                                ((static_cast<unsigned char>(str[pos++]) & 0x3F) << 6) |
                                (static_cast<unsigned char>(str[pos++]) & 0x3F);
                }
            }
            
            return codepoint;
        }

        // Convert a Unicode codepoint to UTF-8 and append to a string
        inline void append_codepoint(std::string& str, uint32_t codepoint)
        {
            if (codepoint < 0x80)
            {
                // 1-byte sequence
                str.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint < 0x800)
            {
                // 2-byte sequence
                str.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint < 0x10000)
            {
                // 3-byte sequence
                str.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint < 0x110000)
            {
                // 4-byte sequence
                str.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                str.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                str.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                str.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }

        // Get the byte index in a string for the given character index
        inline size_t byte_index(const std::string& str, size_t char_index)
        {
            size_t byte_idx = 0;
            size_t char_count = 0;
            
            while (byte_idx < str.size() && char_count < char_index)
            {
                if ((static_cast<unsigned char>(str[byte_idx]) & 0xC0) != 0x80)
                    ++char_count;
                ++byte_idx;
            }
            
            return byte_idx;
        }

        // Get the character index in a string for the given byte index
        inline size_t char_index(const std::string& str, size_t byte_index)
        {
            size_t char_count = 0;
            
            for (size_t i = 0; i < byte_index && i < str.size(); ++i)
            {
                if ((static_cast<unsigned char>(str[i]) & 0xC0) != 0x80)
                    ++char_count;
            }
            
            return char_count;
        }
    }
}