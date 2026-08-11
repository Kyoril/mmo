// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The single translation unit that instantiates Catch2's default main(). Every test
// executable links the catch_main library instead of carrying its own main.cpp, so this
// header -- by far the most expensive one in the tree -- is expanded with
// CATCH_CONFIG_MAIN exactly once for the whole build.
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
