// Options for stb_image. cover.c and image_impl.c must agree.
#pragma once

// Only the formats album art uses; fewer parsers means less attack surface.
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
// Images come from memory; cover.c reads files itself, with a size limit.
#define STBI_NO_STDIO
// stb_image refuses larger images before allocating for them. Covers stop
// at COVER_MAX_DIMENSION (cover.c); background pictures, photos often, go
// further and are held to a pixel count (background.c).
#define STBI_MAX_DIMENSIONS 12000
