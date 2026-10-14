/**
 * Copyright (C) miladramzany, GPLv3. See LICENSE.
 */
#ifndef GL_WIN_GAMMA_H
#define GL_WIN_GAMMA_H

#include <windows.h>
#include <cstdint>
#include <string>

// GDI name of the primary display, e.g. L"\\\\.\\DISPLAY1". Empty on failure.
std::wstring primaryDisplayName();

// Owns the device context of one display, remembers its original gamma ramp
// and puts it back on restore() / destruction.
class GammaDevice
{
public:
	GammaDevice() = default;
	~GammaDevice();
	GammaDevice(const GammaDevice &) = delete;
	GammaDevice &operator=(const GammaDevice &) = delete;

	bool open(const std::wstring &gdiDeviceName);
	bool apply(const uint16_t ramp[3][256]);

	// True if the Windows gamma range limit was already lifted (GdiICMGammaRange >= 256 in HKLM).
	bool rangeUnlocked() const;
	void restore();

private:
	HDC      dc_ = nullptr;
	uint16_t original_[3][256] {};
	bool     haveOriginal_ = false;
};

#endif
