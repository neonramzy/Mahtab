/**
 * Copyright (C) milad ramzany, GPLv3. See LICENSE.
 *
 * Differences from the original capture code:
 *  - COM objects are reference-counted correctly (RAII wrapper below)
 *  - AcquireNextFrame uses a timeout, so quitting never hangs on a static screen
 *  - the staging texture is created once, not on every frame
 *  - the output at (0,0) of the virtual desktop (the primary one) is used, together with its own GPU
 *  - cursor-only updates are skipped
 *  - the GDI fallback downscales in StretchBlt instead of copying the full frame
 */
#include "win_capture.h"
#include "core.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>

namespace {

template <class T>
struct Com {
	T *p = nullptr;
	Com() = default;
	~Com() { reset(); }
	Com(const Com &) = delete;
	Com &operator=(const Com &) = delete;

	void reset() { if (p) { p->Release(); p = nullptr; } }
	T  **put()   { reset(); return &p; }
	void assign(T *x) { reset(); p = x; if (p) p->AddRef(); }
	T   *get() const  { return p; }
	T   *operator->() const { return p; }
	explicit operator bool() const { return p != nullptr; }
};

constexpr int kThumbW = 160;
constexpr int kThumbH = 90;

} // namespace

struct ScreenCapture::Impl
{
	// DXGI
	Com<ID3D11Device>           dev;
	Com<ID3D11DeviceContext>    ctx;
	Com<IDXGIOutputDuplication> dup;
	Com<ID3D11Texture2D>        staging;
	UINT        stW = 0, stH = 0;
	bool        dxgiPermanentlyOff = false;
	ULONGLONG   nextDxgiRetry = 0;

	// GDI fallback
	HDC     memDc = nullptr;
	HBITMAP bmp   = nullptr;
	HGDIOBJ oldBmp = nullptr;
	void   *bits  = nullptr;

	~Impl()
	{
		if (memDc) {
			if (oldBmp) SelectObject(memDc, oldBmp);
			DeleteDC(memDc);
		}
		if (bmp)
			DeleteObject(bmp);
	}

	void dropDxgi()
	{
		dup.reset();
		staging.reset();
		ctx.reset();
		dev.reset();
		stW = stH = 0;
		nextDxgiRetry = GetTickCount64() + 2000;
	}

	bool initDxgi()
	{
		Com<IDXGIFactory1> factory;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.put()))))
			return false;

		Com<IDXGIAdapter1> adapter;
		Com<IDXGIOutput>   output;

		for (UINT a = 0; !output; ++a) {
			Com<IDXGIAdapter1> ad;
			if (factory->EnumAdapters1(a, ad.put()) == DXGI_ERROR_NOT_FOUND)
				break;

			for (UINT o = 0;; ++o) {
				Com<IDXGIOutput> out;
				if (ad->EnumOutputs(o, out.put()) == DXGI_ERROR_NOT_FOUND)
					break;

				DXGI_OUTPUT_DESC d {};
				if (SUCCEEDED(out->GetDesc(&d)) && d.AttachedToDesktop &&
				    d.DesktopCoordinates.left == 0 && d.DesktopCoordinates.top == 0) {
					adapter.assign(ad.get());
					output.assign(out.get());
					break;
				}
			}
		}

		if (!adapter || !output)
			return false;

		if (FAILED(D3D11CreateDevice(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
		                             D3D11_SDK_VERSION, dev.put(), nullptr, ctx.put())))
			return false;

		Com<IDXGIOutput1> out1;
		if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void **>(out1.put()))))
			return false;

		return SUCCEEDED(out1->DuplicateOutput(dev.get(), dup.put()));
	}

	bool ensureStaging(const D3D11_TEXTURE2D_DESC &src)
	{
		if (staging && stW == src.Width && stH == src.Height)
			return true;

		staging.reset();

		D3D11_TEXTURE2D_DESC d = src;
		d.Usage          = D3D11_USAGE_STAGING;
		d.BindFlags      = 0;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		d.MiscFlags      = 0;
		d.MipLevels      = 1;
		d.ArraySize      = 1;

		if (FAILED(dev->CreateTexture2D(&d, nullptr, staging.put())))
			return false;

		stW = src.Width;
		stH = src.Height;
		return true;
	}

	bool pollDxgi(int timeoutMs, int &luma)
	{
		DXGI_OUTDUPL_FRAME_INFO fi {};
		Com<IDXGIResource> res;

		const HRESULT hr = dup->AcquireNextFrame(UINT(std::max(timeoutMs, 0)), &fi, res.put());

		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
			return false;

		if (FAILED(hr)) {                // access lost (mode change, UAC, ...): rebuild later
			dropDxgi();
			return false;
		}

		bool copied = false;

		if (fi.LastPresentTime.QuadPart != 0) {   // 0 means the cursor moved, nothing was drawn
			Com<ID3D11Texture2D> tex;

			if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(tex.put())))) {
				D3D11_TEXTURE2D_DESC desc {};
				tex->GetDesc(&desc);

				if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
					// e.g. HDR (float16). We can't read that here: use GDI from now on.
					dxgiPermanentlyOff = true;
				} else if (ensureStaging(desc)) {
					ctx->CopyResource(staging.get(), tex.get());
					copied = true;
				}
			}
		}

		res.reset();
		dup->ReleaseFrame();

		if (dxgiPermanentlyOff)
			dropDxgi();

		if (!copied)
			return false;

		D3D11_MAPPED_SUBRESOURCE m {};
		if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &m)))
			return false;

		const int step = std::max(1, int(std::min(stW, stH)) / 64);
		luma = core::meanLuma(static_cast<const uint8_t *>(m.pData), int(stW), int(stH), int(m.RowPitch), step);

		ctx->Unmap(staging.get(), 0);
		return true;
	}

	bool initGdi()
	{
		if (memDc)
			return true;

		BITMAPINFO bi {};
		bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth       = kThumbW;
		bi.bmiHeader.biHeight      = -kThumbH;   // top-down
		bi.bmiHeader.biPlanes      = 1;
		bi.bmiHeader.biBitCount    = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		HDC screen = GetDC(nullptr);
		memDc = CreateCompatibleDC(screen);
		bmp   = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		ReleaseDC(nullptr, screen);

		if (!memDc || !bmp || !bits)
			return false;

		oldBmp = SelectObject(memDc, bmp);
		SetStretchBltMode(memDc, HALFTONE);
		SetBrushOrgEx(memDc, 0, 0, nullptr);
		return true;
	}

	bool pollGdi(int timeoutMs, int &luma)
	{
		if (!initGdi())
			return false;

		if (timeoutMs > 0)
			Sleep(DWORD(timeoutMs));

		const int sw = GetSystemMetrics(SM_CXSCREEN);   // primary screen (process is DPI aware)
		const int sh = GetSystemMetrics(SM_CYSCREEN);

		HDC screen = GetDC(nullptr);
		const BOOL ok = StretchBlt(memDc, 0, 0, kThumbW, kThumbH, screen, 0, 0, sw, sh, SRCCOPY);
		ReleaseDC(nullptr, screen);

		if (!ok)
			return false;

		GdiFlush();
		luma = core::meanLuma(static_cast<const uint8_t *>(bits), kThumbW, kThumbH, kThumbW * 4, 1);
		return true;
	}
};

ScreenCapture::ScreenCapture() : p_(new Impl)
{
	if (!p_->initDxgi())
		p_->dropDxgi();   // schedules a retry; GDI is used meanwhile
}

ScreenCapture::~ScreenCapture()
{
	delete p_;
}

const char *ScreenCapture::backend() const
{
	return p_->dup ? "DXGI" : "GDI";
}

bool ScreenCapture::poll(int timeoutMs, int &luma)
{
	if (!p_->dup && !p_->dxgiPermanentlyOff && GetTickCount64() >= p_->nextDxgiRetry) {
		if (!p_->initDxgi())
			p_->dropDxgi();
	}

	if (p_->dup)
		return p_->pollDxgi(timeoutMs, luma);

	return p_->pollGdi(timeoutMs, luma);
}
