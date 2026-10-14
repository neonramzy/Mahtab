/**
 * Copyright (C) milad ramzany, GPLv3. See LICENSE.
 */
#ifndef GL_WIN_CAPTURE_H
#define GL_WIN_CAPTURE_H

// Measures the average luminance of the primary screen.
// Uses DXGI Desktop Duplication when possible, falls back to GDI.
class ScreenCapture
{
public:
	ScreenCapture();
	~ScreenCapture();
	ScreenCapture(const ScreenCapture &) = delete;
	ScreenCapture &operator=(const ScreenCapture &) = delete;

	// Waits up to timeoutMs for new screen content (DXGI) or sleeps that long (GDI),
	// then tries to measure. Returns true and sets `luma` (0..255) only on a fresh measurement.
	bool poll(int timeoutMs, int &luma);

	const char *backend() const;

private:
	struct Impl;
	Impl *p_;
};

#endif
