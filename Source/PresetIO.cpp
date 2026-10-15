#include "PresetIO.h"
#include "PluginProcessor.h"

#include "DtBlkFx.hpp"

#include <cstring>

namespace presetio
{
namespace
{
constexpr int kProgHeader = 56; // through prgName[28]
constexpr int kBankHeader = 156; // through future[128] (or currentProgram + future[124])
constexpr int kPrgNameLen = 28;

struct BEWriter
{
    juce::MemoryOutputStream out;
    void i32(juce::int32 v) { out.writeIntBigEndian(v); }
    void f32(float v) { out.writeFloatBigEndian(v); }
    void name(const juce::String& s)
    {
        char buf[kPrgNameLen] = {};
        s.copyToUTF8(buf, kPrgNameLen); // always zero-terminated, at most 27 bytes of text
        out.write(buf, kPrgNameLen);
    }
};

struct BEReader
{
    const juce::uint8* p;
    size_t n, pos = 0;
    bool has(size_t k) const { return pos + k <= n; }
    juce::int32 i32()
    {
        auto v = (juce::int32) juce::ByteOrder::bigEndianInt(p + pos);
        pos += 4;
        return v;
    }
    float f32()
    {
        juce::uint32 u = juce::ByteOrder::bigEndianInt(p + pos);
        pos += 4;
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    juce::String name()
    {
        auto s = juce::String::fromUTF8((const char*) p + pos, (int) strnlen((const char*) p + pos, kPrgNameLen));
        pos += kPrgNameLen;
        return s;
    }
};

juce::String fourCCString(juce::int32 v)
{
    char s[5] = { (char) ((v >> 24) & 0xff), (char) ((v >> 16) & 0xff), (char) ((v >> 8) & 0xff), (char) (v & 0xff), 0 };
    return juce::String(s);
}

// the engine's own chunk, taken under its lock (getChunk fills a member buffer)
juce::MemoryBlock engineChunk(DtBlkFx& e, bool isPreset)
{
    ScopeCriticalSection lock(e._protect);
    void* data = nullptr;
    const VstInt32 n = e.getChunk(&data, isPreset);
    return data != nullptr && n > 0 ? juce::MemoryBlock(data, (size_t) n) : juce::MemoryBlock();
}

juce::MemoryBlock wrap(juce::int32 fxMagic, int countField, const juce::String& prgName, bool bank,
                       int currentProgram, const juce::MemoryBlock& chunk)
{
    BEWriter w;
    w.i32(fourCC("CcnK"));
    w.i32(0); // byteSize, patched below
    w.i32(fxMagic);
    w.i32(bank ? 2 : 1); // fxProgram version 1; fxBank version 2 (adds currentProgram)
    w.i32(kFxIdStereo);
    w.i32(kFxVersion);
    w.i32(countField); // numParams (program) or numPrograms (bank)
    if (bank)
    {
        w.i32(currentProgram);
        for (int i = 0; i < 124; ++i)
            w.out.writeByte(0);
    }
    else
    {
        w.name(prgName);
    }
    w.i32((juce::int32) chunk.getSize());
    w.out.write(chunk.getData(), chunk.getSize());

    juce::MemoryBlock mb = w.out.getMemoryBlock();
    // byteSize = size of everything after the byteSize field (VST SDK: "size of this chunk, excl.
    // magic + byteSize")
    const juce::uint32 byteSize = (juce::uint32) mb.getSize() - 8;
    auto* d = static_cast<juce::uint8*>(mb.getData());
    d[4] = (juce::uint8) (byteSize >> 24);
    d[5] = (juce::uint8) (byteSize >> 16);
    d[6] = (juce::uint8) (byteSize >> 8);
    d[7] = (juce::uint8) byteSize;
    return mb;
}

void setProgramName(DtBlkFx& e, int index, const juce::String& name)
{
    if (index >= 0 && index < (int) e._program.size())
        e._program[(size_t) index].setName(name.substring(0, 31).toRawUTF8());
}
} // namespace

//==================================================================================================
juce::MemoryBlock saveProgram(DtBlkFxProcessor& p)
{
    auto& e = p.engine();
    return wrap(fourCC("FPCh"), BlkFxParam::TOTAL_NUM, p.getProgramName(p.getCurrentProgram()), false, 0,
                engineChunk(e, /*isPreset*/ true));
}

juce::MemoryBlock saveBank(DtBlkFxProcessor& p)
{
    auto& e = p.engine();
    return wrap(fourCC("FBCh"), (int) e._program.size(), {}, true, p.getCurrentProgram(),
                engineChunk(e, /*isPreset*/ false));
}

Result load(DtBlkFxProcessor& p, const void* data, size_t size)
{
    auto& e = p.engine();
    BEReader r { static_cast<const juce::uint8*>(data), size };

    if (data == nullptr || !r.has(28))
        return { false, "file too short to be a VST preset" };
    if (r.i32() != fourCC("CcnK"))
        return { false, "not a VST preset (no 'CcnK' header)" };
    const juce::int32 byteSize = r.i32();
    const juce::int32 fxMagic = r.i32();
    const juce::int32 version = r.i32();
    const juce::int32 fxID = r.i32();
    /*fxVersion*/ r.i32();
    const juce::int32 count = r.i32();

    if (fxID != kFxIdStereo && fxID != kFxIdMono)
        return { false, "preset is for another plug-in (ID '" + fourCCString(fxID) + "', DtBlkFx is 'h526')" };
    if (byteSize < 20 || (size_t) byteSize + 8 > size)
        return { false, "preset file is truncated" };
    r.n = (size_t) byteSize + 8; // ignore anything after the declared size

    const bool isProgram = fxMagic == fourCC("FxCk") || fxMagic == fourCC("FPCh");
    const bool isBank = fxMagic == fourCC("FxBk") || fxMagic == fourCC("FBCh");
    if (!isProgram && !isBank)
        return { false, "unknown preset type '" + fourCCString(fxMagic) + "'" };

    juce::String name;
    int currentProgram = -1;
    if (isProgram)
    {
        if (!r.has(kPrgNameLen))
            return { false, "preset file is truncated" };
        name = r.name();
    }
    else
    {
        if (!r.has(128))
            return { false, "bank file is truncated" };
        const auto cur = r.i32();
        if (version >= 2)
            currentProgram = cur;
        r.pos += 124;
    }

    const int numSlots = (int) e._program.size() - 1; // the last program is "> reset current <"
    juce::String what;

    if (fxMagic == fourCC("FPCh") || fxMagic == fourCC("FBCh"))
    {
        if (!r.has(4))
            return { false, "preset file is truncated" };
        const juce::int32 chunkSize = r.i32();
        if (chunkSize <= 0 || !r.has((size_t) chunkSize))
            return { false, "preset chunk is truncated" };
        juce::HeapBlock<char> copy((size_t) chunkSize);
        std::memcpy(copy.get(), r.p + r.pos, (size_t) chunkSize);

        VstInt32 ok;
        {
            ScopeCriticalSection lock(e._protect);
            ok = e.setChunk(copy.get(), chunkSize, /*isPreset*/ isProgram);
        }
        if (!ok)
            return { false, "the DtBlkFx data inside the file was not recognised" };
        what = isProgram ? "program \"" + p.getProgramName(p.getCurrentProgram()) + "\""
                         : "bank of " + juce::String(juce::jmin(count, numSlots)) + " programs";
    }
    else if (fxMagic == fourCC("FxCk"))
    {
        // what a host did with a parameter-list preset: name the current program, set each param
        if (!r.has((size_t) juce::jmax(0, count) * 4))
            return { false, "preset file is truncated" };
        const int cur = p.getCurrentProgram();
        setProgramName(e, cur, name);
        for (int i = 0; i < count; ++i)
        {
            const float v = r.f32();
            if (i < BlkFxParam::TOTAL_NUM)
                e.setParameter(i, v);
        }
        what = "program \"" + name + "\"";
    }
    else // FxBk: numPrograms complete FxCk programs
    {
        int loaded = 0;
        {
            ScopeCriticalSection lock(e._protect);
            for (int i = 0; i < count; ++i)
            {
                if (!r.has(kProgHeader))
                    return { false, "bank file is truncated" };
                const size_t start = r.pos;
                if (r.i32() != fourCC("CcnK"))
                    return { false, "bank program " + juce::String(i) + " has no 'CcnK' header" };
                const juce::int32 progSize = r.i32();
                const juce::int32 progMagic = r.i32();
                r.pos += 12; // version, fxID, fxVersion
                const juce::int32 numParams = r.i32();
                const juce::String progName = r.name();
                if (progMagic != fourCC("FxCk") || numParams < 0 || !r.has((size_t) numParams * 4))
                    return { false, "bank program " + juce::String(i) + " is not a parameter list" };
                // same rule as setChunk: never overwrite the "> reset current <" slot
                const bool use = i < numSlots;
                if (use)
                    e._program[(size_t) i].clear();
                for (int k = 0; k < numParams; ++k)
                {
                    const float v = r.f32();
                    if (use && k < BlkFxParam::TOTAL_NUM)
                        e._program[(size_t) i].params[k] = juce::jlimit(0.0f, 1.0f, v);
                }
                if (use)
                {
                    e._program[(size_t) i].setName(progName.substring(0, 31).toRawUTF8());
                    ++loaded;
                }
                r.pos = juce::jmax(r.pos, start + 8 + (size_t) juce::jmax(0, progSize));
            }
            if (loaded > 0 && std::strcmp(e._program[(size_t) loaded - 1].getName(), "> reset current <") == 0)
                e._program[(size_t) loaded - 1].setName("unnamed");
        }
        int target = currentProgram >= 0 ? currentProgram : p.getCurrentProgram();
        if (target < 0 || target >= numSlots)
            target = 0;
        e.setProgram(target);
        what = "bank of " + juce::String(loaded) + " programs";
    }

    p.syncParamsFromEngine();
    return { true, "loaded " + what };
}

Result loadFile(DtBlkFxProcessor& p, const juce::File& f)
{
    juce::MemoryBlock mb;
    if (!f.existsAsFile() || f.getSize() > 16 * 1024 * 1024 || !f.loadFileAsData(mb))
        return { false, "cannot read " + f.getFileName() };
    auto res = load(p, mb.getData(), mb.getSize());
    if (!res.ok)
        res.message = f.getFileName() + ": " + res.message;
    return res;
}

Result saveFile(DtBlkFxProcessor& p, const juce::File& f)
{
    const bool bank = f.hasFileExtension("fxb");
    const auto mb = bank ? saveBank(p) : saveProgram(p);
    if (mb.isEmpty())
        return { false, "nothing to save" };
    if (!f.replaceWithData(mb.getData(), mb.getSize()))
        return { false, "cannot write " + f.getFullPathName() };
    return { true, "saved " + f.getFileName() };
}
} // namespace presetio
