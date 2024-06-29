#include <print>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland/xdg-shell.h"

struct display {
	wl_display* display;
	wl_registry* registry;
	wl_compositor* compositor;
	xdg_wm_base* wm_base;
};

int main()
{
}
