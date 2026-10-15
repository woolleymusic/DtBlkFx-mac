#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: VST2 preset files (.fxp program, .fxb bank), so presets move both ways between
this port and the original Windows plug-in.

File layout (VST 2.4 SDK vstfxstore.h, every header field big-endian):

  .fxp  'CcnK' size 'FxCk'|'FPCh' version=1 fxID fxVersion numParams prgName[28]
        FxCk: numParams big-endian floats
        FPCh: int32 chunkSize, then chunkSize bytes of plug-in chunk
  .fxb  'CcnK' size 'FxBk'|'FBCh' version=1|2 fxID fxVersion numPrograms
        (v2: currentProgram + future[124], v1: future[128])
        FxBk: numPrograms complete 'FxCk' programs
        FBCh: int32 chunkSize, then chunkSize bytes of plug-in chunk

The original sets programsAreChunks(true), so a VST2 host saved its getChunk(isPreset=true) output
in 'FPCh' files and getChunk(isPreset=false) in 'FBCh' files. That chunk is the engine's own
little-endian format (tag 0x99887766, version 101) and is what this port writes too. Plain
parameter files ('FxCk' / 'FxBk'), which some hosts write, are read as well.

fxID is the original's registered unique ID: 'h526' for the stereo build ('g319' was the mono
build; it has the same parameters and is accepted on load). fxVersion is getVendorVersion() = 1200.
***************************************************************************************************/

#include <juce_core/juce_core.h>

class DtBlkFxProcessor;

namespace presetio
{
constexpr juce::int32 fourCC(const char (&s)[5])
{
    return (juce::int32) (((juce::uint32) (unsigned char) s[0] << 24) | ((juce::uint32) (unsigned char) s[1] << 16)
                          | ((juce::uint32) (unsigned char) s[2] << 8) | (juce::uint32) (unsigned char) s[3]);
}

constexpr juce::int32 kFxIdStereo = fourCC("h526");
constexpr juce::int32 kFxIdMono = fourCC("g319");
constexpr juce::int32 kFxVersion = 1200;

struct Result
{
    bool ok = false;
    juce::String message; // what was loaded, or why it failed
};

// .fxp ('FPCh') of the current settings and the current program name
juce::MemoryBlock saveProgram(DtBlkFxProcessor&);

// .fxb ('FBCh', version 2) of current settings + every program
juce::MemoryBlock saveBank(DtBlkFxProcessor&);

// load any of FxCk / FPCh / FxBk / FBCh
Result load(DtBlkFxProcessor&, const void* data, size_t size);
Result loadFile(DtBlkFxProcessor&, const juce::File&);

// writes saveProgram (".fxp") or saveBank (".fxb", decided by the file extension)
Result saveFile(DtBlkFxProcessor&, const juce::File&);

inline bool isPresetFile(const juce::String& path)
{
    return path.endsWithIgnoreCase(".fxp") || path.endsWithIgnoreCase(".fxb");
}
} // namespace presetio
