#pragma once
#include <stddef.h>

// One-time global set up (FFTW plans + factory presets). Safe to call from several instances;
// only the first call does anything. Must run before the first DtBlkFx is constructed.
void DtBlkFxGlobalInit(const char* presets_text, size_t presets_len);
