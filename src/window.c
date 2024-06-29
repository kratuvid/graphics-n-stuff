#include "wayland/xdg-shell.h"
#include <wayland-client.h>
#include <wayland-egl.h>

#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

struct wl_display* display;
struct wl_registry* registry;
struct wl_compositor* compositor;
struct xdg_wm_base* wm_base;

struct wl_surface* surface;
struct xdg_surface* xdg_surface;
struct xdg_toplevel* xdg_toplevel;
struct wl_callback* callback;
bool wait_for_configure = false;

struct {
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig conf;

	EGLSurface surface;
	struct wl_egl_window* native;
} egl;

int width, height;

bool running = true;

int main()
{
}
