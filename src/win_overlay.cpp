/**
 * GPLv3, see LICENSE.
 */
#include "win_overlay.h"
#include <algorithm>

namespace {

constexpr wchar_t kOverlayClass[] = L"BrightnessToolOverlay";

LRESULT CALLBACK overlayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_NCHITTEST:    return HTTRANSPARENT;   // clicks go to the windows underneath
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	}
	return DefWindowProcW(h, msg, wp, lp);
}

RECT primaryMonitorRect()
{
	RECT r = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

	MONITORINFO mi {};
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoW(MonitorFromPoint(POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi))
		r = mi.rcMonitor;
	return r;
}

} // namespace

bool Overlay::create(HINSTANCE inst)
{
	WNDCLASSEXW wc {};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = overlayProc;
	wc.hInstance     = inst;
	wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	wc.lpszClassName = kOverlayClass;
	RegisterClassExW(&wc);

	hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
	                        kOverlayClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
	if (!hwnd_)
		return false;

	SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
	return true;
}

void Overlay::fit()
{
	const RECT r = primaryMonitorRect();
	SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
	             SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Overlay::set(double alpha)
{
	if (!hwnd_)
		return;

	alpha_ = std::clamp(alpha, 0.0, 0.9);

	if (alpha_ <= 0.001) {
		ShowWindow(hwnd_, SW_HIDE);
		return;
	}

	SetLayeredWindowAttributes(hwnd_, 0, BYTE(alpha_ * 255.0 + 0.5), LWA_ALPHA);
	fit();
}

void Overlay::refresh()
{
	if (hwnd_ && alpha_ > 0.001)
		fit();
}

void Overlay::destroy()
{
	if (hwnd_) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}
