// Compiles the single-file decoder libraries once. Their licenses are in
// third_party/ (public domain or MIT-0).
#include "codecs_config.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include "stb_vorbis.c"
