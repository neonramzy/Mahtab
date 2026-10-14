/**
 * Copyright (C) milad ramzany, GPLv3. See LICENSE.
 */
#include "engine.h"
#include <algorithm>

namespace {

void normalized(int kelvin, double rgb[3])
{
	core::tempMultipliers(kelvin, rgb);
	const double mx = std::max({ rgb[0], rgb[1], rgb[2] });
	for (int c = 0; c < 3; ++c)
		rgb[c] /= mx;
}

double ratio(int kelvin)
{
	double rgb[3];
	normalized(kelvin, rgb);
	return std::min({ rgb[0], rgb[1], rgb[2] });   // max is 1 after normalizing
}

} // namespace

// Finds the dimmest uniform ramp this display accepts (bisection). Rejected calls change nothing
// on screen, accepted ones are replaced by the real ramp right after, so this is invisible in practice.
double Engine::probeLimit()
{
	if (gamma_.rangeUnlocked())
		return 0.0;

	uint16_t r[3][256];
	const double one[3] = { 1, 1, 1 };

	core::buildRamp(r, 1.0, one);
	if (!gamma_.apply(r))
		return 1.0;                // gamma ramps unusable here (e.g. remote session): overlay only

	double lo = 0.0, hi = 1.0;     // lo = rejected, hi = accepted
	for (int i = 0; i < 9; ++i) {
		const double mid = (lo + hi) / 2.0;
		core::buildRamp(r, mid, one);
		if (gamma_.apply(r)) hi = mid; else lo = mid;
	}

	core::buildRamp(r, 1.0, one);
	gamma_.apply(r);

	return std::min(1.0, hi + 0.004);   // small safety margin against rounding
}

bool Engine::init()
{
	if (!gamma_.open(primaryDisplayName()))
		return false;

	wMin_ = probeLimit();

	minKelvin_ = 6500;
	for (int k = 6500; k >= 2000; k -= 100) {
		if (ratio(k) >= wMin_) minKelvin_ = k; else break;
	}

	ready_ = true;
	return true;
}

void Engine::set(int brightnessPct, int kelvin)
{
	brightnessPct_ = std::clamp(brightnessPct, 10, 100);
	kelvin_        = std::clamp(kelvin, minKelvin_, 6500);

	if (ready_)
		apply();
}

void Engine::reapply()
{
	if (ready_)
		apply();
}

void Engine::restore()
{
	gamma_.restore();
	ready_ = false;
}

void Engine::apply()
{
	double rgb[3];
	normalized(kelvin_, rgb);

	// Wanted final factor per channel: e = color * brightness. It is split into
	// gamma part g (must stay >= wMin_) and overlay transmission T:   e = g * T
	const double b = brightnessPct_ / 100.0;
	double e[3];
	for (int c = 0; c < 3; ++c)
		e[c] = rgb[c] * b;

	const double minE = std::min({ e[0], e[1], e[2] });

	// largest T (least overlay) that keeps every gamma channel at or above wMin_, but at least b
	double T = wMin_ > 0.0 ? std::min(1.0, minE / wMin_) : 1.0;
	T = std::clamp(T, b, 1.0);

	double g[3];
	for (int c = 0; c < 3; ++c)
		g[c] = std::min(1.0, e[c] / T);

	overlayAlpha_ = 1.0 - T;

	uint16_t ramp[3][256];
	core::buildRamp(ramp, 1.0, g);
	gamma_.apply(ramp);   // can fail while the display is unavailable (lock screen, UAC); retried by reapply()
}
