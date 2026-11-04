#pragma once
/**
 * @file TinyTimeGrapher.h
 * @brief Header-only timegrapher for mechanical watches: tick detection and
 * KPIs (TimeGrapherEngine), arduino-audio-tools integration
 * (TimeGrapherOutput) and a TinyMaterialDesign touch GUI (TimeGrapherApp).
 */
// audio-tools first: it sets up the platform (Arduino API) for everything else
#include "AudioTools.h"
#include "TinyTimeGrapher/TinyTimeGrapherConfig.h"
#include "TinyTimeGrapher/Models.h"
#include "TinyTimeGrapher/TickDetector.h"
#include "TinyTimeGrapher/KpiCalculator.h"
#include "TinyTimeGrapher/HistoryBuffer.h"
#include "TinyTimeGrapher/ClockCalibrator.h"
#include "TinyTimeGrapher/TimeGrapherEngine.h"
#include "TinyTimeGrapher/TimeGrapherOutput.h"
#include "TinyTimeGrapher/WatchSimulator.h"

#include "TinyTimeGrapher/UI/Theme.h"
#include "TinyTimeGrapher/UI/Icons.h"
#include "TinyTimeGrapher/UI/TimegrapherChart.h"
#include "TinyTimeGrapher/UI/TimeGrapherApp.h"

#if defined(ARDUINO) || defined(TTG_AUTO_NAMESPACE)
using namespace tinytimegrapher;
#endif
