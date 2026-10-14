/**
 * Copyright (C) miladramzany, GPLv3. See LICENSE.
 */
#include "win_gamma.h"

std::wstring primaryDisplayName()
{
	for (DWORD i = 0;; ++i) {
		DISPLAY_DEVICEW dd {};
		dd.cb = sizeof(dd);

		if (!EnumDisplayDevicesW(nullptr, i, &dd, 0))
			break;

		if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) &&
		    (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE))
			return dd.DeviceName;
	}
	return {};
}

GammaDevice::~GammaDevice()
{
	restore();
	if (dc_) {
		DeleteDC(dc_);
		dc_ = nullptr;
	}
}

bool GammaDevice::open(const std::wstring &name)
{
	if (name.empty())
		return false;

	dc_ = CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr);
	if (!dc_)
		return false;

	// Remember what the display had before we touched it (calibration profiles etc.)
	haveOriginal_ = GetDeviceGammaRamp(dc_, original_) != FALSE;
	return true;
}

bool GammaDevice::apply(const uint16_t ramp[3][256])
{
	if (!dc_)
		return false;
	return SetDeviceGammaRamp(dc_, const_cast<uint16_t(*)[256]>(ramp)) != FALSE;
}

bool GammaDevice::rangeUnlocked() const
{
	DWORD v = 0, size = sizeof(v);
	return RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ICM",
	                    L"GdiICMGammaRange", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS && v >= 256;
}

void GammaDevice::restore()
{
	if (dc_ && haveOriginal_)
		SetDeviceGammaRamp(dc_, original_);
}
