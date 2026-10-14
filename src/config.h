/**
 * User settings, stored in %APPDATA%\Mahtab\settings.ini
 * GPLv3, see LICENSE.
 */
#ifndef GL_CONFIG_H
#define GL_CONFIG_H

struct AppConfig {
	int brightnessPct = 100;   // 10..100
	int kelvin        = 6500;  // 2000..6500 (6500 = neutral)
};

AppConfig loadConfig();
void      saveConfig(const AppConfig &c);

#endif
