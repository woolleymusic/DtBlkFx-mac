#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: implements the engine's Gui callback interface (Source/engine/Gui.h).

DtBlkFx.cpp calls these with its lock held, from the audio thread (FFTDataRdy) or from whichever
thread set a parameter. The bridge feeds the spectrogram model (same work, same place as the
original Gui::FFTDataRdy); the JUCE editor polls everything else on a timer, the way the original
VSTGUI editor redrew in idle().
***************************************************************************************************/

#include "Gui.h"
#include "gui/SgramModel.h"

class DtBlkFx;

class GuiBridge : public Gui
{
public:
    explicit GuiBridge(DtBlkFx& e) : blkfx(e), sgram(e) {}

    void FFTDataRdy(int a) override { sgram.fftDataReady(a); }
    void suspend() override { sgram.suspend(); }
    void resume() override {}
    void close() override {}
    void setParameter(long, float) override {}

    DtBlkFx& blkfx;
    SgramModel sgram;
};
