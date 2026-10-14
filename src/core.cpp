/**
 * Copyright (C) , GPLv3. See LICENSE.
 */
#include "core.h"
#include <algorithm>
#include <cmath>

namespace core {

static const double kTempTable[46][3] =
#include "temp_table.inc"
;

int meanLuma(const uint8_t *bgra, int width, int height, int pitchBytes, int step)
{
	if (!bgra || width <= 0 || height <= 0)
		return 0;
	if (step < 1)
		step = 1;

	uint64_t r = 0, g = 0, b = 0, n = 0;

	for (int y = 0; y < height; y += step) {
		const uint8_t *row = bgra + size_t(y) * size_t(pitchBytes);
		for (int x = 0; x < width; x += step) {
			const uint8_t *p = row + size_t(x) * 4;
			b += p[0];
			g += p[1];
			r += p[2];
			++n;
		}
	}

	if (n == 0)
		return 0;

	const double l = (0.2126 * double(r) + 0.7152 * double(g) + 0.0722 * double(b)) / double(n);
	return std::clamp(int(l + 0.5), 0, 255);
}

double lerp(double a, double b, double t)
{
	return (1.0 - t) * a + t * b;
}

double remap(double x, double a, double b, double ay, double by)
{
	return lerp(ay, by, (x - a) / (b - a));
}

double easeOutExpo(double t, double b, double c, double d)
{
	if (d <= 0 || t >= d)
		return b + c;
	return c * (-std::pow(2.0, -10.0 * t / d) + 1.0) + b;
}

int targetStep(int luma, const Settings &s)
{
	const double t = kBrtStepsMax
	               - remap(luma, 0, 255, 0, kBrtStepsMax)
	               + remap(s.brtOffset, 0, kBrtStepsMax, 0, s.brtMax);

	const int lo = std::min(s.brtMin, s.brtMax);
	const int hi = std::max(s.brtMin, s.brtMax);
	return std::clamp(int(t), lo, hi);
}

void tempMultipliers(int kelvin, double rgb[3])
{
	const double k   = std::clamp(kelvin, 2000, 6500);
	const double pos = (k - 2000.0) / 100.0;
	int i = int(pos);

	if (i >= 45) {
		for (int c = 0; c < 3; ++c)
			rgb[c] = kTempTable[45][c];
		return;
	}

	const double f = pos - i;
	for (int c = 0; c < 3; ++c)
		rgb[c] = lerp(kTempTable[i][c], kTempTable[i + 1][c], f);
}

void buildRamp(uint16_t ramp[3][256], double brightness01, const double rgbMult[3])
{
	const double b = std::clamp(brightness01, 0.0, 1.0);

	for (int c = 0; c < 3; ++c) {
		for (int i = 0; i < 256; ++i) {
			const double v = double(i) * 257.0 * b * rgbMult[c];
			ramp[c][i] = uint16_t(std::clamp(v + 0.5, 0.0, 65535.0));
		}
	}
}

bool inHeart(double x, double y)
{
	const double a = x * x + y * y - 1.0;
	return a * a * a - x * x * y * y * y <= 0.0;
}

double heartCoverage(int px, int py, int n)
{
	if (n <= 0)
		return 0.0;

	int hit = 0;
	for (int sy = 0; sy < 4; ++sy) {
		for (int sx = 0; sx < 4; ++sx) {
			const double fx = (px + (sx + 0.5) / 4.0) / n;
			const double fy = (py + (sy + 0.5) / 4.0) / n;
			const double x  = (fx * 2.0 - 1.0) * 1.15;
			const double y  = (1.0 - fy * 2.0) * 1.1 + 0.1;
			hit += inHeart(x, y) ? 1 : 0;
		}
	}
	return hit / 16.0;
}

} // namespace core
