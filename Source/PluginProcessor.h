#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: AudioProcessor wrapper around the original DtBlkFx engine (Darrell Tam, 2005-2006;
x64 fork by Dozius). The engine lives in Source/engine and is kept close to the original source.

This wrapper does what the VST2 host used to do: it hands the engine sample rate, block size, tempo
and parameter changes, and asks it for chunks, programs and display strings. Parameter count, order,
names, ranges (0..1) and display strings match the original VST2 plugin one to one.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation; either version 3 of the
License, or (at your option) any later version.
***************************************************************************************************/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

class DtBlkFx;
class GuiBridge;

class DtBlkFxProcessor : public juce::AudioProcessor, private juce::AudioProcessorParameter::Listener
{
public:
    DtBlkFxProcessor();
    ~DtBlkFxProcessor() override;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DtBlkFx"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- for the editor and the harness
    DtBlkFx& engine() { return *blkfx; }
    GuiBridge& guiBridge() { return *bridge; }

    // display string for parameter "index" at normalised value "v" (same as VST2 getParameterDisplay)
    juce::String displayFor(int index, float v) const;

    // set a parameter the way the VST2 host did (engine + host notification)
    void setParameterFromUi(int index, float value);

    // re-read every parameter from the engine (after program change / chunk load) and tell the host
    void syncParamsFromEngine();

    juce::AudioParameterFloat* param(int index) const { return params[(size_t) index]; }
    int numParams() const { return (int) params.size(); }

private:
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}

    void processChunk(float** io, int numSamples);

    std::unique_ptr<DtBlkFx> blkfx;
    std::unique_ptr<GuiBridge> bridge;
    std::vector<juce::AudioParameterFloat*> params;

    juce::HeapBlock<float> monoScratch;
    int maxChunk = 1024;

    // VstTimeInfo is declared in the engine shim; kept opaque here
    struct TimeInfoHolder;
    std::unique_ptr<TimeInfoHolder> timeInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DtBlkFxProcessor)
};
