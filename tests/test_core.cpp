#include "../src/core.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace core;

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

int main()
{
	// --- meanLuma ---
	{
		const int w = 64, h = 32, pitch = w * 4 + 16; // padded rows
		std::vector<uint8_t> img(size_t(pitch) * h, 0xAB); // padding = garbage
		auto fill = [&](uint8_t B, uint8_t G, uint8_t R) {
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					uint8_t *p = &img[size_t(y) * pitch + size_t(x) * 4];
					p[0] = B; p[1] = G; p[2] = R; p[3] = 255;
				}
		};
		fill(255, 255, 255); assert(meanLuma(img.data(), w, h, pitch, 1) == 255);
		fill(0, 0, 0);       assert(meanLuma(img.data(), w, h, pitch, 1) == 0);
		fill(0, 255, 0);     assert(meanLuma(img.data(), w, h, pitch, 1) == 182); // 0.7152*255
		fill(128, 128, 128); assert(meanLuma(img.data(), w, h, pitch, 4) == 128);
		assert(meanLuma(nullptr, w, h, pitch, 1) == 0);
	}

	// --- targetStep (defaults: min 250, max 500, offset 166) ---
	{
		Settings s;
		assert(targetStep(255, s) == 250); // white screen -> dimmed to the minimum
		assert(targetStep(0, s)   == 500); // black screen -> full brightness
		int prev = 1000;
		for (int l = 0; l <= 255; ++l) {   // brighter content never raises brightness
			int t = targetStep(l, s);
			assert(t <= prev && t >= 250 && t <= 500);
			prev = t;
		}
		Settings bad; bad.brtMin = 400; bad.brtMax = 300; // inverted range must not crash
		int t = targetStep(100, bad); assert(t >= 300 && t <= 400);
	}

	// --- tempMultipliers ---
	{
		double c[3];
		tempMultipliers(6500, c); assert(near(c[0], 1) && near(c[1], 1) && near(c[2], 1));
		tempMultipliers(9999, c); assert(near(c[2], 1));                 // clamped
		tempMultipliers(2000, c); assert(near(c[1], 0.54360078) && near(c[2], 0.08679949));
		tempMultipliers(2050, c); assert(near(c[1], (0.54360078 + 0.56618736) / 2));
		tempMultipliers(1000, c); assert(near(c[1], 0.54360078));        // clamped
	}

	// --- buildRamp ---
	{
		uint16_t r[3][256];
		const double one[3] = {1, 1, 1};
		buildRamp(r, 1.0, one);
		for (int c = 0; c < 3; ++c) { assert(r[c][0] == 0); assert(r[c][255] == 65535); assert(r[c][1] == 257); }
		buildRamp(r, 0.5, one);
		assert(r[0][255] == 32768 || r[0][255] == 32767);
		const double warm[3] = {1, 0.5, 0.25};
		buildRamp(r, 1.0, warm);
		assert(r[0][255] == 65535 && r[1][255] > 32000 && r[1][255] < 33500 && r[2][255] > 16000 && r[2][255] < 17000);
		for (int c = 0; c < 3; ++c) for (int i = 1; i < 256; ++i) assert(r[c][i] >= r[c][i - 1]); // monotonic
	}

	// --- easing ---
	{
		assert(near(easeOutExpo(0, 100, 50, 1), 100 + 50 * (-std::pow(2.0, 0) + 1)));
		assert(near(easeOutExpo(1, 100, 50, 1), 150));
		assert(near(easeOutExpo(5, 100, 50, 1), 150));   // past the end stays at target
		assert(near(easeOutExpo(0.5, 100, 0, 1), 100));  // no change
		double prev = -1;
		for (double t = 0; t <= 1.0; t += 0.05) { double v = easeOutExpo(t, 0, 100, 1); assert(v >= prev); prev = v; }
	}

	// --- heart
	{
		assert(inHeart(0.0, 0.2) && inHeart(0.6, 0.6) && inHeart(-0.6, 0.6));
		assert(!inHeart(1.3, 1.0) && !inHeart(0.0, -1.2) && !inHeart(0.0, 1.5));
		const int n = 24;
		assert(heartCoverage(n / 2, n / 3, n) == 1.0);          // inside
		assert(heartCoverage(0, 0, n) == 0.0 && heartCoverage(n - 1, 0, n) == 0.0);
		for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x)  // mirror symmetric
			assert(heartCoverage(x, y, n) == heartCoverage(n - 1 - x, y, n));
		double area = 0;
		for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) area += heartCoverage(x, y, n);
		assert(area > 0.45 * n * n && area < 0.8 * n * n);      // fills a sensible part of the box
		if (std::getenv("SHOW_HEART"))
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) { double c = heartCoverage(x, y, n); std::putchar(c > .75 ? '#' : c > .25 ? '+' : c > 0 ? '.' : ' '); }
				std::putchar('\n');
			}
	}

	std::puts("core tests passed");
	return 0;
}
