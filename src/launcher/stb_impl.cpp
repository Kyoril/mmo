// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The single translation unit that instantiates the stb header-only libraries for
// the launcher. Keep it free of anything else so it stays cheap to rebuild.

#define STB_IMAGE_IMPLEMENTATION

// Embedded and remotely published artwork uses PNG exclusively. Remote payloads
// and dimensions are bounded by the content service before decoding.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR

#include "stb_image/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image/stb_image_resize2.h"
