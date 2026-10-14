/**
 * Applies brightness + color temperature to the primary display. No UI code in here.
 *
 * Windows only accepts gamma ramps within a limited range (about 50 % per channel) unless an
 * admin edits the registry and reboots. To work for everyone out of the box:
 *   - init() measures what this PC accepts (or reads that the limit is lifted),
 *   - the gamma ramp does as much as Windows allows,
 *   - any extra dimming is done by a black overlay (see win_overlay.h; alpha = overlayAlpha()),
 *   - the warmest selectable color temperature (minKelvin()) is the one that still fits.
 *
 * Copyright (C) milad ramzany, GPLv3. See LICENSE.
 */
#ifndef GL_ENGINE_H
#define GL_ENGINE_H

#include "core.h"
#include "win_gamma.h"

class Engine
{
public:
	bool init();                              // opens the primary display and probes its limits
	void set(int brightnessPct, int kelvin);  // clamps and applies immediately
	void reapply();                           // Windows can silently reset the ramp
	void restore();                           // put the original gamma back

	int    minKelvin() const    { return minKelvin_; }     // warmest usable setting (2000..6500)
	double overlayAlpha() const { return overlayAlpha_; }  // 0 = no overlay, 0.9 = very dark
	double gammaLimit() const   { return wMin_; }          // lowest accepted white point (0..1)

private:
	double probeLimit();
	void   apply();

	GammaDevice gamma_;
	int    brightnessPct_ = 100;
	int    kelvin_        = 6500;
	int    minKelvin_     = 2000;
	double wMin_          = 0.0;
	double overlayAlpha_  = 0.0;
	bool   ready_ = false;
};

#endif
