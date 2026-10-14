/**
 * gammy-lite console tool (debugging / headless use).
 *
 * Adaptive screen brightness for Windows.
 * Copyright (C) Francesco Fusco, GPLv3 (see LICENSE).
 */
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core.h"
#include "win_capture.h"
#include "win_gamma.h"

static std::atomic<bool> g_quit { false };
static std::atomic<bool> g_done { false };

static BOOL WINAPI onConsoleEvent(DWORD type)
{
	g_quit = true;

	// For close/logoff/shutdown Windows kills us soon after the handler returns,
	// so give the main loop a moment to restore the original gamma first.
	if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
		for (int i = 0; i < 300 && !g_done; ++i)
			Sleep(10);
	}
	return TRUE;
}

static void usage()
{
	std::puts(
		"Mahtab (console prototype)\n"
		"\n"
		"  --min N       minimum brightness in %      (default 50)\n"
		"  --max N       maximum brightness in %      (default 100)\n"
		"  --offset N    brightness offset in %       (default 33)\n"
		"  --speed MS    transition time in ms        (default 1000)\n"
		"  --threshold N change needed to react       (default 8)\n"
		"  --poll MS     min. time between captures   (default 100)\n"
		"  --kelvin K    color temperature 2000-6500  (default 6500 = off)\n"
		"  --verbose     print measurements\n"
		"\n"
		"Press Ctrl+C to quit; the original gamma is restored.");
}

static bool parseArgs(int argc, char **argv, core::Settings &s, bool &verbose)
{
	auto pct = [](int v) { return std::max(0, std::min(100, v)) * core::kBrtStepsMax / 100; };

	for (int i = 1; i < argc; ++i) {
		const char *a = argv[i];
		const bool hasVal = i + 1 < argc;

		if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h"))
			return false;
		else if (!std::strcmp(a, "--verbose"))
			verbose = true;
		else if (!std::strcmp(a, "--min")       && hasVal) s.brtMin    = pct(std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--max")       && hasVal) s.brtMax    = pct(std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--offset")    && hasVal) s.brtOffset = pct(std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--speed")     && hasVal) s.speedMs   = std::max(0, std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--threshold") && hasVal) s.threshold = std::max(0, std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--poll")      && hasVal) s.pollMs    = std::max(10, std::atoi(argv[++i]));
		else if (!std::strcmp(a, "--kelvin")    && hasVal) s.kelvin    = std::atoi(argv[++i]);
		else {
			std::printf("Unknown or incomplete option: %s\n\n", a);
			return false;
		}
	}

	if (s.brtMin > s.brtMax)
		s.brtMin = s.brtMax;

	return true;
}

int main(int argc, char **argv)
{
	SetProcessDPIAware();   // so screen sizes are real pixels

	core::Settings s;
	bool verbose = false;

	if (!parseArgs(argc, argv, s, verbose)) {
		usage();
		return 1;
	}

	SetConsoleCtrlHandler(onConsoleEvent, TRUE);

	GammaDevice gamma;
	if (!gamma.open(primaryDisplayName())) {
		std::puts("Could not open the primary display.");
		return 2;
	}

	ScreenCapture cap;
	std::printf("Capture backend: %s. Press Ctrl+C to quit.\n", cap.backend());

	using clock = std::chrono::steady_clock;
	const auto ms = [](clock::time_point a, clock::time_point b) {
		return double(std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
	};

	double rgb[3];
	core::tempMultipliers(s.kelvin, rgb);

	uint16_t ramp[3][256];
	bool warnedRange = false;

	auto applyStep = [&](double step) {
		core::buildRamp(ramp, step / core::kBrtStepsMax, rgb);
		if (!gamma.apply(ramp) && !warnedRange) {
			warnedRange = true;
			std::puts(
				"\nWindows rejected the gamma ramp. This usually means the allowed range is limited.\n"
				"Double-click extend-gamma-range.reg (needs admin), reboot, and try again.\n");
		}
	};

	double cur = core::kBrtStepsMax, animStart = cur, animDiff = 0;
	int    target = core::kBrtStepsMax;
	bool   animating = false, haveLuma = false;
	int    prevLuma = 0, accum = 0;

	auto t0         = clock::now();
	auto animT0     = t0;
	auto nextCap    = t0;
	auto nextReapply = t0 + std::chrono::seconds(5);

	applyStep(cur);

	while (!g_quit) {
		const auto now = clock::now();

		if (now >= nextCap) {
			int luma = 0;

			if (cap.poll(16, luma)) {
				accum += std::abs(luma - prevLuma);
				prevLuma = luma;

				if (!haveLuma || accum > s.threshold) {
					accum    = 0;
					haveLuma = true;

					const int t = core::targetStep(luma, s);

					if (verbose)
						std::printf("luma %3d -> target %3d%%\n", luma, t * 100 / core::kBrtStepsMax);

					if (t != target) {
						target    = t;
						animStart = cur;
						animDiff  = target - cur;
						animT0    = now;
						animating = true;
					}
				}
			}

			nextCap = clock::now() + std::chrono::milliseconds(s.pollMs);
		} else {
			Sleep(8);
		}

		if (animating) {
			const double t = ms(animT0, clock::now()) / 1000.0;
			const double d = s.speedMs / 1000.0;

			cur = core::easeOutExpo(t, animStart, animDiff, d);

			if (t >= d) {
				cur = target;
				animating = false;
			}
			applyStep(cur);
			nextReapply = clock::now() + std::chrono::seconds(5);
		} else if (clock::now() >= nextReapply) {
			// Windows (UAC, fullscreen apps, driver) can silently reset the ramp.
			applyStep(cur);
			nextReapply = clock::now() + std::chrono::seconds(5);
		}
	}

	gamma.restore();
	g_done = true;
	std::puts("Original gamma restored. Bye.");
	return 0;
}
