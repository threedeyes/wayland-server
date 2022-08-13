#include "HaikuXdgSurface.h"
#include "HaikuXdgShell.h"
#include "HaikuXdgToplevel.h"
#include "HaikuXdgPopup.h"
#include "HaikuCompositor.h"
#include "HaikuServerDecoration.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xdg-shell-protocol.h>

#include "AppKitPtrs.h"
#include <View.h>
#include <Window.h>
#include <Bitmap.h>

#include <stdio.h>

static void Assert(bool cond) {if (!cond) abort();}


class XdgSurfaceHook: public HaikuSurface::Hook {
private:
	HaikuXdgSurface *fXdgSurface;
public:
	XdgSurfaceHook(HaikuXdgSurface *xdgSurface);
	void HandleCommit() final;
};

XdgSurfaceHook::XdgSurfaceHook(HaikuXdgSurface *xdgSurface):
	fXdgSurface(xdgSurface)
{}

void XdgSurfaceHook::HandleCommit()
{
	BBitmap *bitmap = Base()->Bitmap();
	if (Base()->View() != NULL) {
		auto viewLocked = AppKitPtrs::LockedPtr(Base()->View());
		if (bitmap != NULL) {
			viewLocked->ResizeTo(bitmap->Bounds().Width(), bitmap->Bounds().Height());
		} else {
		}
		Base()->Invalidate();
	}

	// TODO: move to HaikuXdgToplevel/HaikuXdgPopup
	if (!fXdgSurface->fConfigureCalled) {
		if (fXdgSurface->fToplevel != NULL) {
			fXdgSurface->fToplevel->DoSendConfigure();
		}
		if (fXdgSurface->fPopup != NULL) {
			BRect wndRect = fXdgSurface->fPopup->Window()->Frame();
			fXdgSurface->fPopup->fParent->Window()->ConvertFromScreen(&wndRect);
			fXdgSurface->fPopup->SendConfigure(wndRect.left, wndRect.top, (int32_t)wndRect.Width() + 1, (int32_t)wndRect.Height() + 1);
		}
		fXdgSurface->fConfigureCalled = true;
		fXdgSurface->SendConfigure(fXdgSurface->NextSerial());
	}

	if (fXdgSurface->Surface()->Bitmap() != NULL) {
		BSize oldSize = fXdgSurface->Window()->Size();
		BSize newSize = oldSize;
		if (fXdgSurface->Geometry().valid && fXdgSurface->Surface()->ServerDecoration() != NULL && fXdgSurface->Surface()->ServerDecoration()->Mode() == OrgKdeKwinServerDecoration::modeServer) {
			if (fXdgSurface->Surface()->View() != NULL)
				AppKitPtrs::LockedPtr(fXdgSurface->Surface()->View())->MoveTo(-fXdgSurface->Geometry().x, -fXdgSurface->Geometry().y);
			newSize.width = fXdgSurface->Geometry().width - 1;
			newSize.height = fXdgSurface->Geometry().height - 1;
		} else {
			newSize = fXdgSurface->Surface()->Bitmap()->Bounds().Size();
		}
		if (oldSize != newSize)
			fXdgSurface->Window()->ResizeTo(newSize.width, newSize.height);
	}
	if (!fXdgSurface->fSurfaceInitalized && fXdgSurface->Surface()->Bitmap() != NULL) {
		if (fXdgSurface->Surface()->ServerDecoration() != NULL) {
			fXdgSurface->Window()->SetLook(fXdgSurface->Surface()->ServerDecoration()->Look());
		}
		if (fXdgSurface->fToplevel != NULL) {
			fXdgSurface->Window()->CenterOnScreen();
		}
/*
		if (fXdgSurface->fPopup != NULL) {
			fXdgSurface->Window()->MoveBy(-fXdgSurface.Geometry().x, -fXdgSurface.Geometry().y);
		}
*/
		fXdgSurface->Window()->Show();
		fXdgSurface->fSurfaceInitalized = true;
	}
}


//#pragma mark - HaikuXdgSurface
HaikuXdgSurface::~HaikuXdgSurface()
{
	fSurface->SetHook(NULL);
}


uint32_t HaikuXdgSurface::NextSerial()
{
	return (uint32_t)atomic_add((int32*)&fSerial, 1);
}

BWindow *HaikuXdgSurface::Window()
{
	if (fToplevel != NULL) return fToplevel->Window();
	if (fPopup != NULL) return fPopup->Window();
	return NULL;
}


void HaikuXdgSurface::HandleGetToplevel(uint32_t id)
{
	fToplevel = HaikuXdgToplevel::Create(this, id);
}

void HaikuXdgSurface::HandleGetPopup(uint32_t id, struct wl_resource *parent, struct wl_resource *positioner)
{
	fPopup = HaikuXdgPopup::Create(this, id, parent, positioner);
}

void HaikuXdgSurface::HandleSetWindowGeometry(int32_t x, int32_t y, int32_t width, int32_t height)
{
	fGeometry.valid = true;
	fGeometry.x = x;
	fGeometry.y = y;
	fGeometry.width = width;
	fGeometry.height = height;
}

void HaikuXdgSurface::HandleAckConfigure(uint32_t serial)
{
	fAckSerial = serial;
	fConfigurePending = false;
}


HaikuXdgSurface *HaikuXdgSurface::Create(struct HaikuXdgWmBase *client, struct HaikuSurface *surface, uint32_t id)
{
	HaikuXdgSurface *xdgSurface = new(std::nothrow) HaikuXdgSurface();
	if (!xdgSurface) {
		wl_client_post_no_memory(client->Client());
		return NULL;
	}
	if (!xdgSurface->Init(client->Client(), wl_resource_get_version(client->ToResource()), id)) {
		return NULL;
	}
	xdgSurface->client = client;
	xdgSurface->fSurface = surface;
	surface->fXdgSurface = xdgSurface;
	surface->SetHook(new XdgSurfaceHook(xdgSurface));

	return xdgSurface;
}
