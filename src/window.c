#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
int width_gl, height_gl;
bool running = true;

struct {
	bool configs_all, configs_filtered, configs_selected;
} args = {
	.configs_all = false, .configs_filtered = false, .configs_selected = false
};

void _print_egl_configs(EGLConfig* configs, int count) {
#define _DEFINE_ATTRIB_MAP_ELEM(attrib) { #attrib, attrib }
	struct attrib_map {
		const char* name;
		EGLint attrib;
	};
	static struct attrib_map map[] = {
		_DEFINE_ATTRIB_MAP_ELEM(EGL_ALPHA_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_ALPHA_MASK_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_BIND_TO_TEXTURE_RGB), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_BIND_TO_TEXTURE_RGBA), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_BLUE_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_BUFFER_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_COLOR_BUFFER_TYPE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_CONFIG_CAVEAT), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_CONFIG_ID), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_CONFORMANT), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_DEPTH_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_GREEN_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_LEVEL), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_LUMINANCE_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_MAX_PBUFFER_WIDTH), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_MAX_PBUFFER_HEIGHT), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_MAX_PBUFFER_PIXELS), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_MAX_SWAP_INTERVAL), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_MIN_SWAP_INTERVAL), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_NATIVE_RENDERABLE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_NATIVE_VISUAL_ID), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_NATIVE_VISUAL_TYPE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_RED_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_RENDERABLE_TYPE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_SAMPLE_BUFFERS), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_SAMPLES), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_STENCIL_SIZE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_SURFACE_TYPE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_TRANSPARENT_TYPE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_TRANSPARENT_RED_VALUE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_TRANSPARENT_GREEN_VALUE), 
		_DEFINE_ATTRIB_MAP_ELEM(EGL_TRANSPARENT_BLUE_VALUE)
	};
	static const int map_count = sizeof(map) / sizeof(*map);

	for (int i = 0; i < count; i++) {
		if (i != 0) fprintf(stderr, "\n");
		fprintf(stderr, "EGL config #%d:\n", i);
		EGLConfig config = configs[i];
		for (int j = 0; j < map_count; j++) {
			EGLint value;
			assert(eglGetConfigAttrib(egl.dpy, config, map[j].attrib, &value));
			fprintf(stderr, "%s = %d\n", map[j].name, value);
		}
	}
}

void init_egl()
{
	int major, minor;
	assert((egl.dpy = eglGetDisplay((EGLNativeDisplayType)display)) != EGL_NO_DISPLAY);
	assert(eglInitialize(egl.dpy, &major, &minor));
	fprintf(stderr, "EGL v%d.%d\n", major, minor);
	assert(eglBindAPI(EGL_OPENGL_API));

	EGLConfig* configs;
	int count;
	assert(eglGetConfigs(egl.dpy, nullptr, 0, &count));
	fprintf(stderr, "%d configurations available\n", count);
	assert(configs = calloc(count, sizeof(EGLConfig)));

	if (args.configs_all)
	{
		assert(eglGetConfigs(egl.dpy, configs, count, &count));

		fprintf(stderr, "All configurations:\n");
		_print_egl_configs(configs, count);
	}

	static const EGLint filter_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_BUFFER_SIZE, 24,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	static const EGLint required_buffer_size = 24;
	int filtered_count;
	assert(eglChooseConfig(egl.dpy, filter_attribs, configs, count, &filtered_count));
	assert(filtered_count);
	fprintf(stderr, "Filtered down to %d configurations\n", filtered_count);

	if (args.configs_filtered) {
		fprintf(stderr, "Filtered configurations:\n");
		_print_egl_configs(configs, filtered_count);
	}

	for (int i = 0; i < filtered_count; i++) {
		EGLConfig config = configs[i];
		EGLint value;
		assert(eglGetConfigAttrib(egl.dpy, config, EGL_BUFFER_SIZE, &value));
		if (value == required_buffer_size) {
			egl.conf = config;
			break;
		}
	}
	assert(egl.conf);

	free(configs);

	if (args.configs_selected) {
		fprintf(stderr, "Selected configuration:\n");
		_print_egl_configs(&egl.conf, 1);
	}

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 4,
		EGL_CONTEXT_MINOR_VERSION, 6,
		EGL_CONTEXT_OPENGL_DEBUG, EGL_FALSE,
		EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE, EGL_TRUE,
		EGL_NONE
	};
	assert((egl.ctx = eglCreateContext(egl.dpy, egl.conf, EGL_NO_CONTEXT, context_attribs)) != EGL_NO_CONTEXT);
}

void destroy_egl()
{
	if (egl.ctx)
		eglDestroyContext(egl.dpy, egl.ctx);
	if (egl.dpy)
		eglTerminate(egl.dpy);
}

void xdg_toplevel_handler_configure(void* data, struct xdg_toplevel* xdg_toplevel, int32_t width_new, int32_t height_new, struct wl_array* states)
{
	printf("xdg_toplevel_handler_configure: %d, %d\n", width_new, height_new);

	if (width > 0 && height > 0) {
		width = width_new;
		height = height_new;
		width_gl = width;
		height_gl = height;
	}

	if (width_gl != width_new || height_gl != height_new)
		wl_egl_window_resize(egl.native, width_gl, height_gl, 0, 0);
}

void xdg_toplevel_handler_close(void* data, struct xdg_toplevel* xdg_toplevel)
{
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handler_configure,
	.close = xdg_toplevel_handler_close
};

void xdg_surface_handler_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
	wait_for_configure = false;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handler_configure
};

void create_surface()
{
	assert(surface = wl_compositor_create_surface(compositor));
	assert(egl.native = wl_egl_window_create(surface, width, height));

	assert(egl.surface = eglCreateWindowSurface(egl.dpy, egl.conf, egl.native, nullptr));

	assert(xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface));
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, nullptr);

	assert(xdg_toplevel = xdg_surface_get_toplevel(xdg_surface));
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, nullptr);

	xdg_toplevel_set_title(xdg_toplevel, "Window");

	wait_for_configure = true;
	
	struct wl_region* region = wl_compositor_create_region(compositor);
    wl_region_add(region, 0, 0, width, height);
    wl_surface_set_opaque_region(surface, region);

	wl_surface_commit(surface);

	assert(eglMakeCurrent(egl.dpy, egl.surface, egl.surface, egl.ctx));

	wl_display_roundtrip(display);
}

void destroy_surface()
{
	eglMakeCurrent(egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	if (egl.surface)
		eglDestroySurface(egl.dpy, egl.surface);

	if (egl.native)
		wl_egl_window_destroy(egl.native);
	wl_surface_destroy(surface);

	if (callback)
		wl_callback_destroy(callback);
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
		fprintf(stderr, "Using interface: %s, name: %d, version: %d\n", interface, name, version);
}

void registry_handler_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
}

const struct wl_registry_listener registry_listener = {
	.global = registry_handler_global,
	.global_remove = registry_handler_global_remove
};

void redraw()
{
	assert(eglSwapBuffers(egl.dpy, egl.surface));
	wl_surface_commit(surface);
	printf("Redrawing\n");
}

int main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--configs-all") == 0)
			args.configs_all = true;
		else if (strcmp(argv[i], "--configs-filtered") == 0)
			args.configs_filtered = true;
		else if (strcmp(argv[i], "--configs-selected") == 0)
			args.configs_selected = true;
	}

	width = width_gl = 480; height = height_gl = 320;

	assert(display = wl_display_connect(nullptr));
	assert(registry = wl_display_get_registry(display));

	wl_registry_add_listener(registry, &registry_listener, nullptr);
	wl_display_roundtrip(display);
	assert(compositor);
	assert(wm_base);

	init_egl();
	create_surface();

	int ret = 0;
	while (running && ret != -1)
	{
		ret = wl_display_dispatch(egl.dpy);
		printf("%d\n", ret);
		redraw();
	}

	destroy_surface();
	destroy_egl();

	if (wm_base)
		xdg_wm_base_destroy(wm_base);
	if (compositor)
		wl_compositor_destroy(compositor);

	wl_registry_destroy(registry);
	wl_display_flush(display);
	wl_display_disconnect(display);
}
