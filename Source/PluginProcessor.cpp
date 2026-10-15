#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GuiBridge.h"

#include "DtBlkFx.hpp"
#include "DtBlkFxGlobals.h"

#include <BinaryData.h>

namespace
{
// set while the wrapper itself pushes engine values out to the JUCE parameters, so the listener
// does not feed them straight back into the engine
thread_local bool syncingFromEngine = false;

// global set up has to happen before the first DtBlkFx is constructed
std::unique_ptr<DtBlkFx> makeEngine()
{
    DtBlkFxGlobalInit(BinaryData::stereo_presets_txt, (size_t) BinaryData::stereo_presets_txtSize);
    return std::make_unique<DtBlkFx>(nullptr);
}

juce::String engineParamName(DtBlkFx& e, int index)
{
    char name[64] = {};
    e.getParameterName(index, name);
    return juce::String(name);
}

// stable parameter IDs: index-based, in the original VST2 order
juce::String paramId(int index)
{
    using namespace BlkFxParam;
    static const char* glob[] = { "mixbk", "delay", "blksz", "ovrlp" };
    static const char* fx[] = { "frqa", "frqb", "amp", "fx", "val" };
    if (index < NUM_GLOBAL_PARAMS)
        return glob[index];
    int i = index - NUM_GLOBAL_PARAMS;
    return "fx" + juce::String(i / NUM_FX_PARAMS) + "_" + fx[i % NUM_FX_PARAMS];
}
} // namespace

struct DtBlkFxProcessor::TimeInfoHolder
{
    VstTimeInfo ti;
};

//==============================================================================
DtBlkFxProcessor::DtBlkFxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      blkfx(makeEngine()),
      bridge(std::make_unique<GuiBridge>(*blkfx)),
      timeInfo(std::make_unique<TimeInfoHolder>())
{
    // the engine's "editor" slot carries the bridge, so DtBlkFx.cpp's gui()->... calls land here
    blkfx->setEditor(static_cast<Gui*>(bridge.get()));

    // original reported setInitialDelay(_initial_delay) = 0; keep that wiring in case it changes
    blkfx->onInitialDelayChanged = [this](VstInt32 d) { setLatencySamples((int) d); };
    setLatencySamples((int) blkfx->getInitialDelay());

    // VST2 hosts select program 0 when a plug-in is created; do the same so we start on "User"
    blkfx->setProgram(0);

    for (int i = 0; i < BlkFxParam::TOTAL_NUM; ++i)
    {
        auto attrs = juce::AudioParameterFloatAttributes()
                         .withStringFromValueFunction([this, i](float v, int maxLen) {
                             auto s = displayFor(i, v);
                             return maxLen > 0 ? s.substring(0, maxLen) : s;
                         });
        auto* p = new juce::AudioParameterFloat(juce::ParameterID { paramId(i), 1 },
                                                engineParamName(*blkfx, i),
                                                juce::NormalisableRange<float>(0.0f, 1.0f),
                                                blkfx->getParameter(i),
                                                attrs);
        params.push_back(p);
        addParameter(p);
        p->addListener(this);
    }
}

DtBlkFxProcessor::~DtBlkFxProcessor()
{
    for (auto* p : params)
        p->removeListener(this);
    blkfx->setEditor(nullptr);
}

//==============================================================================
juce::String DtBlkFxProcessor::displayFor(int index, float v) const
{
    // same as DtBlkFx::getParameterDisplay, but for any value (JUCE asks for arbitrary values)
    char text[64] = {};
    CharRng str(text, /*max len inc. zero*/ 12);

    v = juce::jlimit(0.0f, 1.0f, v);
    BlkFxParam::SplitParamNum p(index);

    if (p.ok)
    {
        // Two displays in the original ignore "v" and read the current setting (fine for VST2,
        // which only ever asked about the current value). JUCE asks about any value, so these
        // two are computed from "v" here; for the current value the text is identical.
        if (p.glob_param == BlkFxParam::FFT_LEN && blkfx->_fft_len_param.vstParamIdx() == BlkFxParam::FFT_LEN)
        {
            // DtBlkFx::guessFFTLen with the block size param replaced by v
            DtBlkFx& b = *blkfx;
            long delay_n = b.getDelaySamps(b.get(&GetInput, b._delay_param));
            int plan = (int) BlkFxParam::getPlan(b._fft_len_param(v));
            int freq_fft_n = g_fft_sz[plan];
            int time_fft_n = (int) ((float) freq_fft_n * lin_interp(b.get(&GetInput, b._blk_shoulder_frac_param), 1.0f, .25f));
            if (time_fft_n > delay_n)
                freq_fft_n = g_fft_sz[DtBlkFx::reducePlan(plan, delay_n)];
            str << sprnum((float) freq_fft_n / b.getSampleRate()) << "sec";
            return juce::String(text);
        }
        if (p.fx_param == BlkFxParam::FX_TYPE && p.fx_set >= 0)
        {
            auto& st = blkfx->_fx1_0[p.fx_set];
            if (st._param[BlkFxParam::FX_TYPE].vstParamIdx() == p.param)
            {
                str << GetFxRun1_0((int) BlkFxParam::getEffectType(st._param[BlkFxParam::FX_TYPE](v)))->name();
                return juce::String(text);
            }
        }

        if (blkfx->getParamDisplayGlobal(p, v, str))
            return juce::String(text);
        if (p.fx_set >= 0 && blkfx->_fx1_0[p.fx_set].getParamDisplay(p, v, str))
            return juce::String(text);
    }
    str << spr_percent(v);
    return juce::String(text);
}

void DtBlkFxProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    if (syncingFromEngine)
        return;

    // the VST2 host called setParameter from whichever thread it liked; the engine locks itself
    blkfx->setParameter(parameterIndex, newValue);
}

void DtBlkFxProcessor::setParameterFromUi(int index, float value)
{
    if (auto* p = param(index))
        p->setValueNotifyingHost(value); // goes through parameterValueChanged -> engine
}

void DtBlkFxProcessor::syncParamsFromEngine()
{
    syncingFromEngine = true;
    for (int i = 0; i < (int) params.size(); ++i)
        params[(size_t) i]->setValueNotifyingHost(blkfx->getParameter(i));
    syncingFromEngine = false;
    updateHostDisplay(ChangeDetails().withProgramChanged(true).withParameterInfoChanged(true));
}

//==============================================================================
void DtBlkFxProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // engine buffers are sized in samples (x3 is 5*44100); keep host chunks to a sane size
    maxChunk = juce::jlimit(32, 8192, samplesPerBlock > 0 ? samplesPerBlock : 1024);
    monoScratch.allocate((size_t) maxChunk, true);

    blkfx->suspend();
    blkfx->setSampleRate((float) sampleRate);
    blkfx->setBlockSize(maxChunk);
    blkfx->resume();
}

void DtBlkFxProcessor::releaseResources()
{
    blkfx->suspend();
}

void DtBlkFxProcessor::reset()
{
    blkfx->suspend();
    blkfx->resume();
}

bool DtBlkFxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DtBlkFxProcessor::processChunk(float** io, int n)
{
    // in-place is fine: the engine copies the input into its own FIFO before writing output
    blkfx->processReplacing(io, io, n);
}

void DtBlkFxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    if (numSamples <= 0)
        return;

    // tempo / beat position for beat sync and beat-based delay (VST2 getTimeInfo)
    VstTimeInfo* ti = nullptr;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            auto& t = timeInfo->ti;
            t.flags = 0;
            t.sampleRate = getSampleRate();
            if (auto bpm = pos->getBpm(); bpm && *bpm > 0.0)
            {
                t.tempo = *bpm;
                t.flags |= kVstTempoValid;
            }
            if (auto ppq = pos->getPpqPosition())
            {
                t.ppqPos = *ppq;
                t.flags |= kVstPpqPosValid;
            }
            if (pos->getIsPlaying())
                t.flags |= kVstTransportPlaying;
            ti = &t;
        }
    }
    blkfx->hostTimeInfo = ti;

    const bool mono = numIn < 2 || numOut < 2;

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = juce::jmin(maxChunk, numSamples - start);
        float* io[2];
        io[0] = buffer.getWritePointer(0, start);
        if (mono)
        {
            // the engine is the stereo build; run the mono signal through both sides
            juce::FloatVectorOperations::copy(monoScratch.get(), io[0], n);
            io[1] = monoScratch.get();
        }
        else
        {
            io[1] = buffer.getWritePointer(1, start);
        }
        processChunk(io, n);
    }

    // Port safety net (not in the original): if anything ever goes non-finite, the engine's
    // overlap-add buffer would keep it forever. Silence this block and flush the engine instead.
    bool finite = true;
    for (int ch = 0; ch < juce::jmin(2, numOut) && finite; ++ch)
    {
        const float* d = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(d[i]))
            {
                finite = false;
                break;
            }
    }
    if (!finite)
    {
        buffer.clear();
        blkfx->suspend();
        blkfx->resume();
    }
}

//==============================================================================
int DtBlkFxProcessor::getNumPrograms()
{
    return juce::jmax(1, (int) blkfx->_program.size());
}

int DtBlkFxProcessor::getCurrentProgram()
{
    return blkfx->currProgramNum();
}

void DtBlkFxProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;
    blkfx->setProgram(index);
    syncParamsFromEngine();
}

const juce::String DtBlkFxProcessor::getProgramName(int index)
{
    char text[64] = {};
    if (blkfx->getProgramNameIndexed(0, index, text))
        return juce::String(text);
    return {};
}

void DtBlkFxProcessor::changeProgramName(int index, const juce::String& newName)
{
    if (index >= 0 && index < (int) blkfx->_program.size())
        blkfx->_program[(size_t) index].setName(newName.substring(0, 31).toRawUTF8());
}

//==============================================================================
void DtBlkFxProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // the original's own chunk format (tag 0x99887766, version 101): current params + every program
    // getChunk fills a member buffer; the editor's preset save uses it too, so take the lock
    ScopeCriticalSection lock(blkfx->_protect);
    void* data = nullptr;
    VstInt32 n = blkfx->getChunk(&data, /*isPreset*/ false);
    if (data != nullptr && n > 0)
        destData.append(data, (size_t) n);
}

void DtBlkFxProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    bool ok;
    {
        ScopeCriticalSection lock(blkfx->_protect);
        ok = blkfx->setChunk(const_cast<void*>(data), sizeInBytes, /*isPreset*/ false) != 0;
    }
    if (ok)
        syncParamsFromEngine();
}

//==============================================================================
juce::AudioProcessorEditor* DtBlkFxProcessor::createEditor()
{
    return new DtBlkFxEditor(*this);
}

#ifndef DTBLKFX_HEADLESS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DtBlkFxProcessor();
}
#endif
