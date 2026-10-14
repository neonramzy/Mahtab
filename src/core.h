/**
 * Mahtab core: pure logic, no OS headers (so it can be unit-tested anywhere).
 *
 * Copyright (C) Milad ramzany, GPLv3.
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v3. See LICENSE.
 */
#ifndef GL_CORE_H
#define GL_CORE_H

#include <cstdint>

namespace core {

constexpr int kBrtStepsMax = 500;   // brightness is expressed in 0..500 steps (like Mahtab)

struct Settings {
	int brtMin    = 250;   // steps, 0..500  (50 %)
	int brtMax    = 500;   // steps          (100 %)
	int brtOffset = 166;   // steps          (~33 %)
	int speedMs   = 1000;  // transition duration
	int threshold = 8;     // accumulated luma change needed to re-evaluate
	int pollMs    = 100;   // minimum time between screen measurements
	int kelvin    = 6500;  // manual color temperature, 2000..6500
};

// Mean luminance (0..255) of a BGRA8 image, sampling every `step` pixels in x and y.
// `pitchBytes` is the distance in bytes between rows (may exceed width*4).
int meanLuma(const uint8_t *bgra, int width, int height, int pitchBytes, int step);

double lerp(double a, double b, double t);
double remap(double x, double a, double b, double ay, double by);
double easeOutExpo(double t, double b, double c, double d);

// Target brightness step (kBrtStepsMax = full) for a given screen luminance.
int targetStep(int luma, const Settings &s);

// Per-channel multipliers (0..1) for a color temperature in Kelvin (clamped to 2000..6500).
void tempMultipliers(int kelvin, double rgb[3]);

// Fills a 3x256 gamma ramp. brightness01 in 0..1; identity ramp is brightness 1, mult {1,1,1}.
void buildRamp(uint16_t ramp[3][256], double brightness01, const double rgbMult[3]);

// Heart shape (donate button). x,y roughly in [-1.3, 1.3]; true when inside.
bool inHeart(double x, double y);

// Fraction (0..1) of pixel (px,py) of an n x n image covered by the heart (4x4 supersampling).
double heartCoverage(int px, int py, int n);

} // namespace core

#endif
