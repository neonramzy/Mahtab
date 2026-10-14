// Fake display used by engine_test.cpp (replaces win_gamma.cpp).
#include "../src/win_gamma.h"
#include <cstring>

uint16_t g_minAccepted = 0;      // a ramp is rejected if any channel's white point is below this
uint16_t g_lastRamp[3][256];
int      g_applyCount = 0;
int      g_rejected = 0;
bool     g_rejectAll = false;    // simulates a display that does not take gamma ramps at all
bool     g_unlocked = false;     // simulates GdiICMGammaRange being set

std::wstring primaryDisplayName() { return L"FAKE"; }

GammaDevice::~GammaDevice() {}
bool GammaDevice::open(const std::wstring &) { return true; }
void GammaDevice::restore() {}
bool GammaDevice::rangeUnlocked() const { return g_unlocked; }
bool GammaDevice::apply(const uint16_t ramp[3][256])
{
	bool ok = !g_rejectAll;
	for (int c = 0; c < 3; ++c)
		if (ramp[c][255] < g_minAccepted)
			ok = false;

	if (!ok) {
		++g_rejected;
		return false;
	}
	std::memcpy(g_lastRamp, ramp, sizeof(g_lastRamp));
	++g_applyCount;
	return true;
}
