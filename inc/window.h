#pragma once

#include "wayland/xdg-shell.h"
#include <wayland-client.h>
#include <wayland-egl.h>

#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

struct display {
	struct wl_display* display;
	struct wl_registry* registry;
	struct wl_compositor* compositor;
	struct xdg_wm_base* wm_base;

	struct wl_seat* seat;
	struct wl_pointer* pointer;
	struct wl_keyboard* keyboard;

	struct wl_shm* shm;
	struct wl_surface* surface;
	struct {
		EGLDisplay* display;
		EGLContext* context;
		EGLConfig config;
	} egl;
};

struct geometry {
	int width, height;
};

struct window {
	struct display* display;
};
