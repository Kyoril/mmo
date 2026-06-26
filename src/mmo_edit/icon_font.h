// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

// Icon font definitions for FontAwesome 4.4.1 (data/client/Editor/FontAwesome.ttf).
//
// These macros expand to the UTF-8 byte sequence of the corresponding glyph so they
// can be concatenated directly into regular narrow string literals, e.g.:
//
//     ImGui::Button(ICON_FA_FLOPPY_O " Save All");
//
// The icon font is merged into the editor's default/header fonts in MainWindow::InitImGui,
// so any ImGui text rendered with those fonts can embed these glyphs inline.
//
// The codepoints are encoded as explicit UTF-8 bytes (rather than \u escapes) so that the
// MSVC source/execution charset never mangles them.

// Glyph range covered by FontAwesome 4.4.1 (used when building the font atlas).
#define ICON_MIN_FA 0xf000
#define ICON_MAX_FA 0xf2e0

// Action / toolbar
#define ICON_FA_FLOPPY_O               "\xEF\x83\x87"
#define ICON_FA_SAVE                   "\xEF\x83\x87"
#define ICON_FA_UPLOAD                 "\xEF\x82\x93"
#define ICON_FA_DOWNLOAD               "\xEF\x80\x99"
#define ICON_FA_SHARE_SQUARE_O         "\xEF\x81\x85"
#define ICON_FA_REFRESH                "\xEF\x80\xA1"
#define ICON_FA_COG                    "\xEF\x80\x93"
#define ICON_FA_COGS                   "\xEF\x82\x85"
#define ICON_FA_WRENCH                 "\xEF\x82\xAD"
#define ICON_FA_SLIDERS                "\xEF\x87\x9E"
#define ICON_FA_PLUS                   "\xEF\x81\xA7"
#define ICON_FA_MINUS                  "\xEF\x81\xA8"
#define ICON_FA_TIMES                  "\xEF\x80\x8D"
#define ICON_FA_CHECK                  "\xEF\x80\x8C"
#define ICON_FA_PENCIL                 "\xEF\x81\x80"
#define ICON_FA_TRASH_O                "\xEF\x80\x94"
#define ICON_FA_TRASH                  "\xEF\x87\xB8"
#define ICON_FA_COPY                   "\xEF\x83\x85"
#define ICON_FA_CLONE                  "\xEF\x89\x8D"
#define ICON_FA_SEARCH                 "\xEF\x80\x82"
#define ICON_FA_PLAY                   "\xEF\x81\x8B"
#define ICON_FA_PAUSE                  "\xEF\x81\x8C"
#define ICON_FA_STOP                   "\xEF\x81\x8D"
#define ICON_FA_BARS                   "\xEF\x83\x89"
#define ICON_FA_FOLDER_OPEN            "\xEF\x81\xBC"
#define ICON_FA_FOLDER                 "\xEF\x81\xBB"
#define ICON_FA_FILE_O                 "\xEF\x80\x96"
#define ICON_FA_HOME                   "\xEF\x80\x95"
#define ICON_FA_INFO_CIRCLE            "\xEF\x81\x9A"
#define ICON_FA_EXCLAMATION_TRIANGLE   "\xEF\x81\xB1"
#define ICON_FA_QUESTION_CIRCLE        "\xEF\x81\x99"
#define ICON_FA_ARROWS                 "\xEF\x81\x87"
#define ICON_FA_EXPAND                 "\xEF\x81\xA5"
#define ICON_FA_EYE                    "\xEF\x81\xAE"
#define ICON_FA_BELL                   "\xEF\x83\xB3"
#define ICON_FA_BUG                    "\xEF\x86\x88"
#define ICON_FA_ROCKET                 "\xEF\x84\xB5"

// Data categories / content
#define ICON_FA_DATABASE               "\xEF\x87\x80"
#define ICON_FA_MAGIC                  "\xEF\x83\x90"
#define ICON_FA_BOLT                   "\xEF\x83\xA7"
#define ICON_FA_FLASK                  "\xEF\x83\x83"
#define ICON_FA_STAR                   "\xEF\x80\x85"
#define ICON_FA_SHIELD                 "\xEF\x84\xB2"
#define ICON_FA_HEART                  "\xEF\x80\x84"
#define ICON_FA_TROPHY                 "\xEF\x82\x91"
#define ICON_FA_GIFT                   "\xEF\x81\xAB"
#define ICON_FA_SHOPPING_CART          "\xEF\x81\xBA"
#define ICON_FA_BOOK                   "\xEF\x80\xAD"
#define ICON_FA_GRADUATION_CAP         "\xEF\x86\x9D"
#define ICON_FA_USERS                  "\xEF\x83\x80"
#define ICON_FA_USER                   "\xEF\x80\x87"
#define ICON_FA_PAW                    "\xEF\x86\xB0"
#define ICON_FA_MAP                    "\xEF\x89\xB9"
#define ICON_FA_MAP_O                  "\xEF\x89\xB8"
#define ICON_FA_MAP_SIGNS              "\xEF\x89\xB7"
#define ICON_FA_GLOBE                  "\xEF\x82\xAC"
#define ICON_FA_CUBE                   "\xEF\x86\xB2"
#define ICON_FA_CUBES                  "\xEF\x86\xB3"
#define ICON_FA_PICTURE_O              "\xEF\x80\xBE"
#define ICON_FA_FILM                   "\xEF\x80\x88"
#define ICON_FA_PAINT_BRUSH            "\xEF\x87\xBC"
#define ICON_FA_LIST                   "\xEF\x80\xBA"
#define ICON_FA_LIST_UL                "\xEF\x83\x8A"
#define ICON_FA_SITEMAP                "\xEF\x83\xA8"
#define ICON_FA_TAGS                   "\xEF\x80\xAC"
#define ICON_FA_TAG                    "\xEF\x80\xAB"
#define ICON_FA_LOCK                   "\xEF\x80\xA3"
#define ICON_FA_UNLOCK                 "\xEF\x82\x9C"
#define ICON_FA_CODE                   "\xEF\x84\xA1"
#define ICON_FA_TERMINAL               "\xEF\x84\xA0"
#define ICON_FA_LANGUAGE               "\xEF\x86\xAB"
#define ICON_FA_COMMENT                "\xEF\x81\xB5"
#define ICON_FA_COMMENTS               "\xEF\x82\x86"
#define ICON_FA_BULLSEYE               "\xEF\x85\x80"
#define ICON_FA_CROSSHAIRS             "\xEF\x81\x9B"
#define ICON_FA_BRIEFCASE              "\xEF\x82\xB1"
#define ICON_FA_LEAF                   "\xEF\x81\xAC"
#define ICON_FA_TREE                   "\xEF\x86\xBB"
#define ICON_FA_SUN_O                  "\xEF\x86\x85"
#define ICON_FA_TINT                   "\xEF\x81\x83"
#define ICON_FA_COMPASS                "\xEF\x85\x8E"
#define ICON_FA_LOCATION_ARROW         "\xEF\x84\xA4"
#define ICON_FA_CHECK_SQUARE_O         "\xEF\x81\x86"
#define ICON_FA_CUTLERY                "\xEF\x83\xB5"
#define ICON_FA_MALE                   "\xEF\x86\x83"
#define ICON_FA_VENUS_MARS             "\xEF\x88\xA8"
#define ICON_FA_FONT                   "\xEF\x80\xB1"
