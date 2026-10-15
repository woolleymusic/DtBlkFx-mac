#pragma once
/**************************************************************************************************
DtBlkFx for JUCE: the original editor rebuilt from the original skin and layout (Gui.cpp):

    global controls   410 x 26
    spectrogram 1     410 x 95   (Input L+R by default)
    spectrogram 2     410 x 95   (Output L+R by default)
    8 effect rows     410 x 24 each

The window can be scaled (fixed aspect) since 410 px is small on today's screens; at 100% it is
pixel for pixel the original layout.
***************************************************************************************************/

#include "PluginProcessor.h"

#include "gui/FxCtrlView.h"
#include "gui/GlobalCtrlView.h"
#include "gui/SpectrogramView.h"

class DtBlkFxEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit DtBlkFxEditor(DtBlkFxProcessor&);
    ~DtBlkFxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;

    // drop a .fxp / .fxb (VST2 preset or bank, from the original or saved here) to load it
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

    // preset menu actions (also reachable from the spectrogram menu)
    enum PresetMenu
    {
        kLoadPreset = 100, // = SpectrogramView::kExtraMenuBase
        kSavePreset,
        kSaveBank
    };
    void presetMenuAction(int id);
    void addPresetItems(juce::PopupMenu& m) const;

    // message from the last load / save (for the harness; errors also show an alert)
    juce::String lastPresetMessage;

private:
    void timerCallback() override;
    void dispatch(int index, float v);
    void reportPreset(bool ok, const juce::String& message);

    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHover = false;

    DtBlkFxProcessor& proc;

    struct Content : juce::Component
    {
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::black); }
    } content;

    std::unique_ptr<dtgui::GlobalCtrlView> global;
    std::unique_ptr<dtgui::SpectrogramView> sgram[2];
    std::vector<std::unique_ptr<dtgui::FxCtrlView>> fx;

    std::vector<float> cache;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DtBlkFxEditor)
};
