/**************************************************************************************************
JUCE port of DtBlkFx.

Replaces the global, one-time set up that DtBlkFxMain.cpp did in VSTPluginMain (see
Source/original_gui/DtBlkFxMain.cpp): build the FFTW plans and load the factory presets. The
original read "<dll dir>/dtblkfx/stereo_presets.txt" at load time; the port embeds the same file
in the binary and hands its text in here.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 3 of the
License, or (at your option) any later version.
***************************************************************************************************/
#include <StdAfx.h>

#include "DtBlkFxGlobals.h"
#include "BlkFxParam.h"
#include "VstProgram.h"
#include "rfftw_float.h"

#include <mutex>
#include <string>

vector<VstProgram<BlkFxParam::TOTAL_NUM>> g_blk_fx_presets;

static std::mutex g_init_protection;
static bool g_init_done = false;

// the original returned true when the VSTGUI images loaded; the JUCE port never builds the old
// VSTGUI editor, so this is always false and DtBlkFx's constructor skips "new Gui"
bool GlobalInitOk()
{
  return false;
}

void DtBlkFxGlobalInit(const char* presets_text, size_t presets_len)
{
  std::lock_guard<std::mutex> lock(g_init_protection);
  if (g_init_done)
    return;

  // FFTW's planner is not thread safe, which is why this sits behind the mutex
  CreateFFTWfPlans();

  // same as LoadPresets in DtBlkFxMain.cpp: one program per line (fgets semantics)
  std::string text(presets_text ? presets_text : "", presets_text ? presets_len : 0);
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    size_t end = eol == std::string::npos ? text.size() : eol + 1;
    std::string line = text.substr(pos, end - pos);
    g_blk_fx_presets.push_back(VstProgram<BlkFxParam::TOTAL_NUM>(line.c_str()));
    pos = end;
  }

  if (g_blk_fx_presets.empty()) {
    // mirrors the "file not found" fallback in the original
    g_blk_fx_presets.resize(20);
    g_blk_fx_presets[0].name << "file not found";
    for (int i = 1; i < (int)g_blk_fx_presets.size(); i++)
      g_blk_fx_presets[i].name << i << " user";
  }

  g_init_done = true;
}
