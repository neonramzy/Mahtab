/**
 * GPLv3, see LICENSE.
 */
#include "config.h"
#include "appinfo.h"
#include <windows.h>
#include <algorithm>
#include <string>

static std::wstring iniPath(bool createDir)
{
	wchar_t buf[MAX_PATH] {};
	const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
	std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) + L"\\" + kAppId : L".";

	if (createDir)
		CreateDirectoryW(dir.c_str(), nullptr);

	return dir + L"\\settings.ini";
}

static const wchar_t *kSection = kAppId;

AppConfig loadConfig()
{
	const std::wstring p = iniPath(false);
	AppConfig c;

	auto get = [&](const wchar_t *key, int def, int lo, int hi) {
		const int v = int(GetPrivateProfileIntW(kSection, key, def, p.c_str()));
		return std::clamp(v, lo, hi);
	};

	c.brightnessPct = get(L"brightnessPct", c.brightnessPct, 10, 100);
	c.kelvin        = get(L"kelvin",        c.kelvin,        2000, 6500);
	return c;
}

void saveConfig(const AppConfig &c)
{
	const std::wstring p = iniPath(true);

	auto put = [&](const wchar_t *key, int v) {
		WritePrivateProfileStringW(kSection, key, std::to_wstring(v).c_str(), p.c_str());
	};

	put(L"brightnessPct", c.brightnessPct);
	put(L"kelvin",        c.kelvin);
}
