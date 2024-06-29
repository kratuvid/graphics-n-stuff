#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "wayland/xdg-shell.h"
#include <wayland-client.h>
#include <wayland-egl.h>

#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

struct wl_display* display = 0;
struct wl_registry* registry = 0;
struct wl_compositor* compositor = 0;
struct xdg_wm_base* wm_base = 0;

struct wl_surface* surface = 0;
struct xdg_surface* xdg_surface = 0;
struct xdg_toplevel* xdg_toplevel = 0;
struct wl_callback* callback = 0;
bool wait_for_configure = false;

struct {
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig conf;

	EGLSurface surface;
	struct wl_egl_window* native;
} egl = {};

int width, height;
bool running = true;

void init_egl()
{
	assert((egl.dpy = eglGetDisplay((EGLNativeDisplayType)display)) != EGL_NO_DISPLAY);
}

void wm_base_handler_ping(void* data, struct xdg_wm_base* wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_handler_ping
};

void registry_handler_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
	bool using = false;

	if (strcmp(interface, "wl_compositor") == 0) {
		assert(compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4));
		using = true;
	}
	else if (strcmp(interface, "xdg_wm_base") == 0) {
		assert(wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
		xdg_wm_base_add_listener(wm_base, &wm_base_listener, nullptr);
		using = true;
	}

	if (using)
		printf("Using interface: %s, name: %d, version: %d\n", interface, name, version);
}

void registry_handler_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
}

const struct wl_registry_listener registry_listener = {
	.global = registry_handler_global,
	.global_remove = registry_handler_global_remove
};

int main()
{
	width = 480; height = 320;

	assert(display = wl_display_connect(nullptr));
	assert(registry = wl_display_get_registry(display));

	wl_registry_add_listener(registry, &registry_listener, nullptr);
	wl_display_roundtrip(display);
	assert(compositor);
	assert(wm_base);

	init_egl();

	if (wm_base)
		xdg_wm_base_destroy(wm_base);
	if (compositor)
		wl_compositor_destroy(compositor);

	wl_registry_destroy(registry);
	wl_display_flush(display);
	wl_display_disconnect(display);
}
