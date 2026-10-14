#include "../src/engine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern uint16_t g_minAccepted, g_lastRamp[3][256];
extern int  g_applyCount, g_rejected;
extern bool g_rejectAll, g_unlocked;

static void reset() { g_minAccepted = 0; g_rejectAll = false; g_unlocked = false; g_applyCount = 0; g_rejected = 0; }

static double white(int c) { return g_lastRamp[c][255] / 65535.0; }

// Windows' real default limit is about 32512 / 65535
static const uint16_t kWinLimit = 32512;

int main()
{
	// 1) the probe finds the display's limit (and not much more)
	{
		reset(); g_minAccepted = kWinLimit;
		Engine e; assert(e.init());
		assert(e.gammaLimit() >= kWinLimit / 65535.0);
		assert(e.gammaLimit() <  kWinLimit / 65535.0 + 0.012);
	}

	// 2) normal brightness uses gamma only, neutral = identity ramp
	{
		reset(); g_minAccepted = kWinLimit;
		Engine e; assert(e.init());
		e.set(100, 6500);
		assert(white(0) == 1.0 && white(1) == 1.0 && white(2) == 1.0 && e.overlayAlpha() == 0.0);
		e.set(80, 6500);
		assert(std::fabs(white(0) - 0.8) < 0.002 && e.overlayAlpha() == 0.0);
	}

	// 3) below the limit the rest of the dimming goes to the overlay; the total is what was asked for
	{
		reset(); g_minAccepted = kWinLimit;
		Engine e; assert(e.init());
		for (int pct : { 49, 30, 10 }) {
			e.set(pct, 6500);
			assert(white(0) >= kWinLimit / 65535.0);
			assert(e.overlayAlpha() > 0.0);
			assert(std::fabs(white(0) * (1.0 - e.overlayAlpha()) - pct / 100.0) < 0.003);
		}
	}

	// 4) temperature: warmest allowed setting still fits the limit; one step further would not
	{
		reset(); g_minAccepted = kWinLimit;
		Engine e; assert(e.init());
		assert(e.minKelvin() > 2000 && e.minKelvin() < 4000);
		e.set(100, 1000);   // clamped to minKelvin
		assert(g_lastRamp[2][255] >= kWinLimit);
		assert(white(2) < white(1) && white(1) < white(0));
	}

	// 5) no combination of settings is ever rejected by the display
	{
		reset(); g_minAccepted = kWinLimit;
		Engine e; assert(e.init());
		const int before = g_rejected;
		for (int b = 10; b <= 100; b += 3)
			for (int k = e.minKelvin(); k <= 6500; k += 100) {
				e.set(b, k);
				const double total = white(0) * (1.0 - e.overlayAlpha());   // red is the brightest channel
				assert(std::fabs(total - b / 100.0) < 0.004);
			}
		assert(g_rejected == before);
	}

	// 6) limit already lifted (registry): pure gamma, full temperature range, no overlay
	{
		reset(); g_unlocked = true; g_minAccepted = 0;
		Engine e; assert(e.init());
		assert(e.minKelvin() == 2000 && e.gammaLimit() == 0.0);
		e.set(10, 2000);
		assert(e.overlayAlpha() == 0.0 && std::fabs(white(0) - 0.1) < 0.002);
	}

	// 7) display takes no gamma ramps at all: dimming still works through the overlay, color is fixed
	{
		reset(); g_rejectAll = true;
		Engine e; assert(e.init());
		assert(e.minKelvin() == 6500);
		e.set(40, 3000);
		assert(std::fabs(e.overlayAlpha() - 0.6) < 0.001);
	}

	// 8) reapply writes again; nothing is applied before init() or after restore()
	{
		reset(); Engine e;
		e.set(60, 6500); e.reapply();
		assert(g_applyCount == 0);
		assert(e.init()); e.set(60, 6500);
		const int n = g_applyCount;
		e.reapply();
		assert(g_applyCount == n + 1);
		e.restore(); e.reapply();
		assert(g_applyCount == n + 1);
	}

	std::puts("engine tests passed");
	return 0;
}
