#pragma once

#include "pch.hpp"
#include "backend-shm.hpp"
#include "utility.hpp"

template<class TBackend>
  requires std::is_base_of_v<Backend, TBackend>
class App
{
protected: // variables
	struct Wayland wl {};

    struct
    {
        struct {
            glm::ivec2 pos;
            bool button[3];
        } pointer;

        struct {
            struct {
                xkb_context* context;
                xkb_state* state;
            } xkb;
            std::unordered_map<xkb_keysym_t, wl_keyboard_key_state> map;
            std::unordered_map<char32_t, wl_keyboard_key_state> map_utf;
        } keyboard;
    } input {};

	struct {
		bool window_last_activated = false;
		bool running = true;
	} state;

	struct {
		float delta_update_time = 0, delta_draw_time = 0;
		std::chrono::high_resolution_clock::time_point tp_begin, tp_very_last;
		std::chrono::nanoseconds duration_pause {0};
	} timekeeping;

    int width = 800, height = 600;
	float elapsed_time = 0;

	std::unique_ptr<TBackend> backend;

	// standard configuration
    std::string_view title = "App!";
    unsigned substeps = 1;

public: // public interface
	App()
		:backend(std::make_unique<TBackend>(&wl, &width, &height))
	{
        srand(time(nullptr));
	}

    void init()
    {
		init_core();
		init_window();
		backend->init();
    }

    void run()
    {
        timekeeping.tp_begin = timekeeping.tp_very_last = std::chrono::high_resolution_clock::now();

		setup();

		// Kickstart redraw
		redraw(1e-3f); // expecting a backend->present in here
		iassert(wl.window.redraw_callback = wl_surface_frame(wl.window.surface));
		wl_callback_add_listener(wl.window.redraw_callback, &redraw_listener, this);
		wl_surface_commit(wl.window.surface);

        while (state.running && wl_display_dispatch(wl.display) != -1);
    }

    ~App()
    {
        rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            spdlog::info("Peak self RSS usage: {:.3f} MiB", usage.ru_maxrss / 1024.0);
            spdlog::info("User CPU time: {:.3f} s", usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6f);
            spdlog::info("System CPU time: {:.3f} s", usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6f);
        }
        if (getrusage(RUSAGE_CHILDREN, &usage) == 0 && usage.ru_maxrss != 0)
            spdlog::info("Peak children RSS usage: {:.3f} MB", usage.ru_maxrss / 1024.0);

		backend.reset();
        destroy_input();
		destroy_window();
		destroy_core();
    }

private: // private interface
    void init_core()
    {
        iassert(wl.display = wl_display_connect(nullptr));
        iassert(wl.registry = wl_display_get_registry(wl.display));

        wl_registry_add_listener(wl.registry, &registry_listener, this);
        wl_display_roundtrip(wl.display);
        iassert(wl.global.wm_base);
        iassert(wl.global.compositor);
        iassert(wl.global.shm);
        iassert(wl.global.seat);
        wl_display_roundtrip(wl.display);
	}

	void init_window()
	{
        iassert(wl.window.surface = wl_compositor_create_surface(wl.global.compositor));
        iassert(wl.window.xsurface = xdg_wm_base_get_xdg_surface(wl.global.wm_base, wl.window.surface));
        iassert(wl.window.xtoplevel = xdg_surface_get_toplevel(wl.window.xsurface));

        xdg_surface_add_listener(wl.window.xsurface, &xsurface_listener, this);
        xdg_toplevel_add_listener(wl.window.xtoplevel, &xtoplevel_listener, this);

        wl_surface_commit(wl.window.surface);
		wl_display_roundtrip(wl.display);
    }

    void destroy_input()
    {
        safe_free(input.keyboard.xkb.state, xkb_state_unref);
        safe_free(input.keyboard.xkb.context, xkb_context_unref);
        safe_free(wl.seat.keyboard, wl_keyboard_destroy);

        safe_free(wl.seat.pointer, wl_pointer_destroy);
    }

    void destroy_window()
    {
        safe_free(wl.window.redraw_callback, wl_callback_destroy);
        safe_free(wl.window.xtoplevel, xdg_toplevel_destroy);
        safe_free(wl.window.xsurface, xdg_surface_destroy);
        safe_free(wl.window.surface, wl_surface_destroy);
	}

	void destroy_core()
	{
        safe_free(wl.global.seat, wl_seat_destroy);
        safe_free(wl.global.shm, wl_shm_destroy);
        safe_free(wl.global.wm_base, xdg_wm_base_destroy);
        safe_free(wl.global.compositor, wl_compositor_destroy);
        safe_free(wl.registry, wl_registry_destroy);
        safe_free(wl.display, wl_display_disconnect);
    }

public: // external redraw
    static void redraw(void* data, wl_callback* callback, uint32_t _time)
    {
		using namespace std::chrono;

        auto app = static_cast<App*>(data);

        static auto tp_last = app->timekeeping.tp_begin;
        const auto tp_now = high_resolution_clock::now();
        if (app->state.window_last_activated) {
            tp_last = tp_now;
            app->state.window_last_activated = false;
        }
        const auto delta_time_raw = tp_now - tp_last;
        const float delta_time = duration_cast<nanoseconds>(delta_time_raw).count() / 1e9f;
        tp_last = tp_now;

        app->elapsed_time = duration_cast<nanoseconds>((tp_now - app->timekeeping.tp_begin) - app->timekeeping.duration_pause).count() / 1e9f;

        static float last_title_time = -1;
        if (app->elapsed_time - last_title_time > 0.25f) {
            last_title_time = app->elapsed_time;
            xdg_toplevel_set_title(app->wl.window.xtoplevel,
				std::format("{} - {:.3f} FPS ({:.3f}ms, {:.3f}ms, {:.3f}ms)",
					app->title,
					1.f / delta_time,
					delta_time * 1e3f,
					app->timekeeping.delta_update_time * 1e3f,
					app->timekeeping.delta_draw_time * 1e3f)
				.c_str());
        }

        app->redraw(delta_time);

		wl_callback_destroy(callback);
        iassert(app->wl.window.redraw_callback = wl_surface_frame(app->wl.window.surface));
        wl_callback_add_listener(app->wl.window.redraw_callback, &redraw_listener, data);
        wl_surface_commit(app->wl.window.surface);

        app->timekeeping.tp_very_last = high_resolution_clock::now();
    }

private: // internal redraw
    void redraw(float delta_time)
    {
		using namespace std::chrono;

        auto _tp_begin = high_resolution_clock::now();
        const float sub_dt = delta_time / substeps;
        for (unsigned i = 0; i < substeps; i++) {
            update(sub_dt);
            if (i != substeps - 1)
                elapsed_time += sub_dt;
        }
        auto _tp_end = high_resolution_clock::now();
        timekeeping.delta_update_time = duration_cast<nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

        _tp_begin = high_resolution_clock::now();
        draw(delta_time);
        _tp_end = high_resolution_clock::now();
        timekeeping.delta_draw_time = duration_cast<nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;
    }

protected: // user facing stuff
    virtual void setup() = 0;
    virtual void update(float delta_time) = 0;
    virtual void draw(float delta_time) = 0;

protected: // events
	virtual void on_click(uint32_t button, uint32_t state)
	{
	}

	virtual void on_key(xkb_keysym_t key, wl_keyboard_key_state state)
	{
	}

	virtual void on_configure(wl_array* states)
	{
	}

public: // listeners
    static void on_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        // log_event(__func__, "{} v{}", interface, version);

        auto app = static_cast<App*>(data);

        if (strcmp(interface, "wl_shm") == 0) {
            iassert(app->wl.global.shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1));
            wl_shm_add_listener(app->wl.global.shm, &shm_listener, data);
        } else if (strcmp(interface, "wl_compositor") == 0) {
            iassert(app->wl.global.compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4));
        } else if (strcmp(interface, "xdg_wm_base") == 0) {
            iassert(app->wl.global.wm_base = (xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            xdg_wm_base_add_listener(app->wl.global.wm_base, &wm_base_listener, data);
        } else if (strcmp(interface, "wl_seat") == 0) {
            iassert(app->wl.global.seat = (wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 5));
            wl_seat_add_listener(app->wl.global.seat, &seat_listener, data);
        }
    }
    static void on_registry_global_remove(void* data, wl_registry* registry, uint32_t name)
    {
        // log_event(__func__, "{}", name);
    }

    static void on_shm_format(void* data, wl_shm* shm, uint32_t format)
    {
        // log_event(__func__, "0x{:x}", format);
    }

    static void on_wm_base_ping(void* data, xdg_wm_base* wm_base, uint32_t serial)
    {
        // log_event(__func__, "{}", serial);

        xdg_wm_base_pong(wm_base, serial);
    }

    static void on_xsurface_configure(void* data, xdg_surface* xsurface, uint32_t serial)
    {
        // log_event(__func__, "{}", serial);

        xdg_surface_ack_configure(xsurface, serial);
    }

    static void on_xtoplevel_configure(void* data, xdg_toplevel* xtoplevel, int32_t width, int32_t height, wl_array* states)
    {
        // log_event(__func__, "{}x{} {}", width, height, states->size);

        auto app = static_cast<App*>(data);

        for (uint32_t* ptr = (uint32_t*)states->data;
             states->size != 0 && (uint8_t*)ptr < ((uint8_t*)states->data + states->size);
             ptr++) {
            switch (*ptr) {
            case XDG_TOPLEVEL_STATE_ACTIVATED: {
                app->state.window_last_activated = true;

                const auto tp_now = std::chrono::high_resolution_clock::now();
                const auto duration_total
                    = tp_now - app->timekeeping.tp_begin,
                    duration_last_pause = app->timekeeping.tp_very_last - app->timekeeping.tp_begin;

                app->timekeeping.duration_pause += duration_total - duration_last_pause;
            } break;

            default:
                break;
            }
        }

		const bool choose_dimensions = width == 0 or height == 0;
		bool new_dimensions = choose_dimensions;

        if (!(choose_dimensions or (width == app->width && height == app->height))) {
            app->width = width;
            app->height = height;
			new_dimensions = true;
        }

		app->backend->on_configure(new_dimensions, states);
    }
    static void on_xtoplevel_close(void* data, xdg_toplevel* xtoplevel)
    {
        // log_event(__func__);

        auto app = static_cast<App*>(data);
        app->state.running = false;
    }

    static void on_seat_capabilities(void* data, wl_seat* seat, uint32_t caps)
    {
        // log_event(__func__, "0x{:x}", caps);

        auto app = static_cast<App*>(data);

        if (!app->wl.seat.pointer) {
            if (caps & WL_SEAT_CAPABILITY_POINTER) {
                iassert(app->wl.seat.pointer = wl_seat_get_pointer(seat));
                wl_pointer_add_listener(app->wl.seat.pointer, &pointer_listener, data);
            }
        } else {
            if (!(caps & WL_SEAT_CAPABILITY_POINTER)) {
                safe_free(app->wl.seat.pointer, wl_pointer_destroy);
            }
        }

        if (!app->wl.seat.keyboard) {
            if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
                iassert(app->wl.seat.keyboard = wl_seat_get_keyboard(seat));
                wl_keyboard_add_listener(app->wl.seat.keyboard, &keyboard_listener, data);
            }
        } else {
            if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
                safe_free(app->wl.seat.keyboard, wl_keyboard_destroy);
            }
        }
    }
    static void on_seat_name(void* data, wl_seat* seat, const char* name)
    {
        // log_event(__func__, "{}", name);
    }

    static void on_pointer_enter(void* data, wl_pointer* pointer, uint32_t, wl_surface*, wl_fixed_t x, wl_fixed_t y)
    {
        auto app = static_cast<App*>(data);

        int ix = wl_fixed_to_int(x), iy = wl_fixed_to_int(y);
        app->input.pointer.pos = glm::ivec2(ix, iy);
    }
    static void on_pointer_leave(void* data, wl_pointer* pointer, uint32_t, wl_surface*)
    {
    }
    static void on_pointer_motion(void* data, wl_pointer* pointer, uint32_t, wl_fixed_t x, wl_fixed_t y)
    {
        auto app = static_cast<App*>(data);

        int ix = wl_fixed_to_int(x), iy = wl_fixed_to_int(y);
        app->input.pointer.pos = glm::ivec2(ix, iy);
    }
    static void on_pointer_button(void* data, wl_pointer* pointer, uint32_t, uint32_t, uint32_t button, uint32_t state)
    {
        auto app = static_cast<App*>(data);

        switch (button) {
        case BTN_LEFT:
            app->input.pointer.button[0] = state;
            break;
        case BTN_MIDDLE:
            app->input.pointer.button[1] = state;
            break;
        case BTN_RIGHT:
            app->input.pointer.button[2] = state;
            break;
        }

		app->on_click(button, state);
    }
    static void on_pointer_axis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
    {
    }
    static void on_pointer_frame(void* data, wl_pointer* pointer)
    {
    }
    static void on_pointer_axis_source(void* data, wl_pointer* pointer, uint32_t)
    {
    }
    static void on_pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t, uint32_t)
    {
    }
    static void on_pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t, int32_t)
    {
    }

    static void on_keyboard_keymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size)
    {
        // log_event(__func__, "{} {}", fd, size);

        auto app = static_cast<App*>(data);
        auto& xkb = app->input.keyboard.xkb;

        iassert(xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS));

        xkb_keymap* keymap;

        char* keymap_str;
        iassert(format == XKB_KEYMAP_FORMAT_TEXT_V1);
        iassert((keymap_str = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0)) != MAP_FAILED);

        iassert(keymap = xkb_keymap_new_from_string(xkb.context, keymap_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));

        munmap((void*)keymap_str, size);
        close(fd);

        iassert(xkb.state = xkb_state_new(keymap));
        xkb_keymap_unref(keymap);
    }
    static void on_keyboard_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys)
    {
        // log_event(__func__);
    }
    static void on_keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
    {
        // log_event(__func__);
    }
    static void on_keyboard_key(void* data, wl_keyboard* _keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
    {
        // log_event(__func__, "0x{:x}: {}", key, state);

        auto app = static_cast<App*>(data);
        auto& keyboard = app->input.keyboard;
        auto& xkb = keyboard.xkb;

        const auto scancode = key + 8;
		const auto keystate = static_cast<wl_keyboard_key_state>(state);

        auto sym = xkb_state_key_get_one_sym(xkb.state, scancode);
        if (sym != XKB_KEY_NoSymbol) {
            keyboard.map[sym] = keystate;
			app->on_key(sym, keystate);
		}
        
        auto c32 = static_cast<char32_t>(xkb_state_key_get_utf32(xkb.state, scancode));
        keyboard.map_utf[c32] = keystate;
    }
    static void on_keyboard_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
    {
        // log_event(__func__);

        auto app = static_cast<App*>(data);
        auto& xkb = app->input.keyboard.xkb;

        xkb_state_update_mask(xkb.state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }
    static void on_keyboard_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay)
    {
        // log_event(__func__);
    }

    static constexpr wl_registry_listener registry_listener {
        .global = on_registry_global,
        .global_remove = on_registry_global_remove
    };
    static constexpr wl_shm_listener shm_listener {
        .format = on_shm_format
    };
    static constexpr xdg_wm_base_listener wm_base_listener {
        .ping = on_wm_base_ping
    };
    static constexpr xdg_surface_listener xsurface_listener {
        .configure = on_xsurface_configure
    };
    static constexpr xdg_toplevel_listener xtoplevel_listener {
        .configure = on_xtoplevel_configure,
        .close = on_xtoplevel_close
    };
    static constexpr wl_callback_listener redraw_listener {
        .done = redraw
    };
    static constexpr wl_seat_listener seat_listener {
        .capabilities = on_seat_capabilities,
        .name = on_seat_name
    };
    static constexpr wl_pointer_listener pointer_listener {
        .enter = on_pointer_enter,
        .leave = on_pointer_leave,
        .motion = on_pointer_motion,
        .button = on_pointer_button,
        .axis = on_pointer_axis,
        .frame = on_pointer_frame,
        .axis_source = on_pointer_axis_source,
        .axis_stop = on_pointer_axis_stop,
        .axis_discrete = on_pointer_axis_discrete,
    };
    static constexpr wl_keyboard_listener keyboard_listener {
        .keymap = on_keyboard_keymap,
        .enter = on_keyboard_enter,
        .leave = on_keyboard_leave,
        .key = on_keyboard_key,
        .modifiers = on_keyboard_modifiers,
        .repeat_info = on_keyboard_repeat_info
    };
};
