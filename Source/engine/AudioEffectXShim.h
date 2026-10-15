#pragma once
/**************************************************************************************************
JUCE port of DtBlkFx.

Minimal stand-in for the VST 2.x SDK's AudioEffect / AudioEffectX base class. It provides only the
members and calls that DtBlkFx.cpp uses, so the original engine code can stay as it was. The JUCE
AudioProcessor (Source/PluginProcessor.*) owns a DtBlkFx and feeds it sample rate, tempo and
parameter changes through this interface.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 3 of the
License, or (at your option) any later version.
***************************************************************************************************/

#include <stdint.h>
#include <functional>

typedef int32_t VstInt32;
typedef void* audioMasterCallback; // unused by the port

enum {
  kVstMaxProgNameLen = 24,
  kVstMaxParamStrLen = 8,
  kVstMaxVendorStrLen = 64,
  kVstMaxProductStrLen = 64,
  kVstMaxEffectNameLen = 32
};

enum VstPlugCategory { kPlugCategUnknown = 0, kPlugCategEffect };

enum VstTimeInfoFlags {
  kVstTransportChanged = 1,
  kVstTransportPlaying = 1 << 1,
  kVstPpqPosValid = 1 << 9,
  kVstTempoValid = 1 << 10
};

struct VstTimeInfo {
  double samplePos = 0;
  double sampleRate = 44100;
  double nanoSeconds = 0;
  double ppqPos = 0;
  double tempo = 120;
  double barStartPos = 0;
  double cycleStartPos = 0;
  double cycleEndPos = 0;
  VstInt32 timeSigNumerator = 4;
  VstInt32 timeSigDenominator = 4;
  VstInt32 smpteOffset = 0;
  VstInt32 smpteFrameRate = 0;
  VstInt32 samplesToNextClock = 0;
  VstInt32 flags = 0;
};

class AEffEditor;

//-------------------------------------------------------------------------------------------------
class AudioEffectX {
public:
  AudioEffectX(audioMasterCallback, VstInt32 numPrograms_, VstInt32 numParams_)
      : numPrograms(numPrograms_), numParams(numParams_)
  {
  }
  virtual ~AudioEffectX() {}

  // --- called by the JUCE wrapper (were host callbacks in VST2)
  virtual void setSampleRate(float sr) { sampleRate = sr; }
  virtual void setBlockSize(VstInt32 sz) { blockSize = sz; }
  virtual void resume() {}
  virtual void suspend() {}
  virtual void setProgram(VstInt32 program) { curProgram = program; }
  virtual VstInt32 getProgram() { return curProgram; }

  float getSampleRate() const { return sampleRate; }
  VstInt32 getBlockSize() const { return blockSize; }
  VstInt32 getNumPrograms() const { return numPrograms; }
  VstInt32 getInitialDelay() const { return initialDelay; }

  // the wrapper fills this in before each process call; null means "no transport info"
  VstTimeInfo* hostTimeInfo = nullptr;

  // the wrapper sets this to forward latency changes to JUCE (setLatencySamples)
  std::function<void(VstInt32)> onInitialDelayChanged;

protected:
  // --- members that DtBlkFx.cpp accesses directly (same names as the VST2 SDK)
  float sampleRate = 44100.0f;
  VstInt32 blockSize = 1024;
  VstInt32 curProgram = 0;
  VstInt32 numPrograms = 0;
  VstInt32 numParams = 0;
  VstInt32 initialDelay = 0;
  void* editor = nullptr; // the original stored its VSTGUI editor here; see Gui.h

  // --- host calls DtBlkFx makes (VST2 audioMaster equivalents)
  VstTimeInfo* getTimeInfo(VstInt32 /*filter*/) { return hostTimeInfo; }
  void setInitialDelay(VstInt32 delay)
  {
    initialDelay = delay;
    if (onInitialDelayChanged)
      onInitialDelayChanged(delay);
  }
public:
  void setEditor(void* e) { editor = e; } // public in the port so the JUCE wrapper can attach its bridge

protected:
  void setNumInputs(VstInt32) {}
  void setNumOutputs(VstInt32) {}
  void canProcessReplacing(bool = true) {}
  void programsAreChunks(bool = true) {}
  void setUniqueID(VstInt32) {}
};

// DtBlkFx.cpp refers to AudioEffect:: in a couple of places
typedef AudioEffectX AudioEffect;
