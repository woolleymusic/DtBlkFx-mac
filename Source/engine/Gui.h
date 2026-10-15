#pragma once
/**************************************************************************************************
JUCE port of DtBlkFx.

The original Gui class (see Source/original_gui/Gui.h) was a VSTGUI AEffGUIEditor that DtBlkFx
stored as its "editor" and called back into from the audio engine. This header keeps just those
callbacks as an interface so DtBlkFx.cpp is unchanged; the JUCE side implements it
(Source/GuiBridge.h) to feed the spectrograms and keep controls in sync.

All of these are called with DtBlkFx::_protect held (same as the original).
***************************************************************************************************/

class Gui {
public:
  Gui() {}
  explicit Gui(void* /*effect*/) {} // DtBlkFx.cpp still has "new Gui(this)" behind GlobalInitOk()
  virtual ~Gui() {}

  // 0 = input FFT data ready in DtBlkFx::FFTdata(), 1 = output (post-effect) data ready
  virtual void FFTDataRdy(int /*a*/) {}

  virtual void suspend() {}
  virtual void resume() {}
  virtual void close() {}
  virtual void setParameter(long /*index*/, float /*value*/) {}
};
