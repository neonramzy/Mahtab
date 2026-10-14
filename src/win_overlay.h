/**
 * A click-through black layer over the primary monitor, used for the part of the dimming
 * that Windows' gamma range limit does not allow. GPLv3, see LICENSE.
 */
#ifndef GL_WIN_OVERLAY_H
#define GL_WIN_OVERLAY_H

#include <windows.h>

class Overlay
{
public:
	bool create(HINSTANCE inst);
	void set(double alpha);   // 0 = hidden, 0.9 max
	void refresh();           // re-fit to the monitor and stay on top (call now and then)
	void destroy();

private:
	void fit();

	HWND   hwnd_  = nullptr;
	double alpha_ = 0.0;
};

#endif
