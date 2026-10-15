// JUCE port: stand-in for the original Visual Studio precompiled header (projects/StdAfx.h).
// The original pulled in windows.h, the VST2 SDK and VSTGUI; the engine only needs the
// standard library here.
#pragma once

#ifndef STEREO
#  define STEREO 1
#endif

#include <stdio.h>
#include <string.h>
#include <string>
#include <valarray>
#include <vector>

using namespace std;
