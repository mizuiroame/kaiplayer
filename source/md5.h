// MD5, for the Subsonic sign-in's token (md5 of the password and a salt).
// Not for anything that needs to be secure on its own.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MD5_HEX_SIZE 33  // 32 hex digits and the NUL.

// The digest of size bytes, as 32 lowercase hex digits.
void md5Hex(const void *data, size_t size, char out[MD5_HEX_SIZE]);
