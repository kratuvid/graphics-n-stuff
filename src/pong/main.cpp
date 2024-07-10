#include "pch/pch.hpp"

#ifndef DISABLE_IASSERT
#define iassert(expr, ...) \
    if (!(expr))           \
        App::_iassert(#expr, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__);
#else
#define iassert(expr, ...) \
    if (!(expr))           \
        ;
#endif

class App {
private: /* section: variables */
    // wayland
    struct
    {
        wl_display* display;
        wl_registry* registry;
        wl_compositor* compositor;
        xdg_wm_base* wm_base;
        wl_shm* shm;
        wl_seat* seat;
    } wayland {};

    struct
    {
        struct {
            wl_pointer* object;
            glm::ivec2 pos, cpos;
            bool button[3];
        } pointer;
        struct {
            wl_keyboard* object;
            struct {
                xkb_context* context;
                xkb_state* state;
            } xkb;
            std::unordered_map<xkb_keysym_t, wl_keyboard_key_state> map;
            std::unordered_map<char32_t, wl_keyboard_key_state> map_utf;
        } keyboard;
    } input {};

    struct
    {
        wl_surface* surface;
        xdg_surface* xsurface;
        xdg_toplevel* xtoplevel;
        wl_callback* callback;
    } window {};

    struct buffer {
        wl_buffer* buffer;
        bool busy;
        union {
            void* shm_data;
            uint32_t* shm_data_u32;
            uint8_t* shm_data_u8;
        };
        size_t shm_size;

        struct {
            Cairo::RefPtr<Cairo::ImageSurface> surface;
            Cairo::RefPtr<Cairo::Context> context;
        } cairo;
		struct {
			Glib::RefPtr<Pango::Layout> layout;
		} pango;
    } buffers[2] {};

    bool rebuild_buffers = false;
    // END - wayland

    // state
    bool is_initial_configured = false;
    bool running = true;
    int width = 800, height = 600;
    float elapsed_time = 0, delta_update_time = 0, delta_draw_time = 0;

    std::chrono::high_resolution_clock::time_point tp_begin, tp_very_last;
    std::chrono::nanoseconds duration_pause { 0 };
    bool last_window_activated = false;

    static constexpr unsigned substeps = 1;
    static constexpr std::string_view title { "Pong" };
    // END - state

public: /* section: public interface */
    void initialize()
    {
        srand(time(nullptr));
        initialize_wayland();
        initialize_window();
		initialize_pango();
    }

    void run()
    {
        tp_begin = tp_very_last = std::chrono::high_resolution_clock::now();

        setup_pre();
        redraw(this, nullptr, 0);
        wl_display_roundtrip(wayland.display);
        setup();

        while (running && wl_display_dispatch(wayland.display) != -1)
            {}
        
        destroy();
    }

    ~App()
    {
        rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            spdlog::debug("Peak self RSS usage: {:.3f} MB", usage.ru_maxrss / 1024.0);
            spdlog::debug("User CPU time: {:.3f} s", usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6f);
            spdlog::debug("System CPU time: {:.3f} s", usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6f);
        }
        if (getrusage(RUSAGE_CHILDREN, &usage) == 0 && usage.ru_maxrss != 0)
            spdlog::debug("Peak children RSS usage: {:.3f} MB", usage.ru_maxrss / 1024.0);

        destroy_input();
        destroy_buffers();
		destroy_pango();
        destroy_window();
        destroy_wayland();
    }

private: /* section: private interface */
    void initialize_wayland()
    {
        iassert(wayland.display = wl_display_connect(nullptr));
        iassert(wayland.registry = wl_display_get_registry(wayland.display));

        wl_registry_add_listener(wayland.registry, &registry_listener, this);
        wl_display_roundtrip(wayland.display);
        iassert(wayland.wm_base);
        iassert(wayland.compositor);
        iassert(wayland.shm);
        iassert(wayland.seat);
        wl_display_roundtrip(wayland.display);
    }

    void initialize_window()
    {
        iassert(window.surface = wl_compositor_create_surface(wayland.compositor));
        iassert(window.xsurface = xdg_wm_base_get_xdg_surface(wayland.wm_base, window.surface));
        iassert(window.xtoplevel = xdg_surface_get_toplevel(window.xsurface));

        xdg_surface_add_listener(window.xsurface, &xsurface_listener, this);
        xdg_toplevel_add_listener(window.xtoplevel, &xtoplevel_listener, this);

        wl_surface_commit(window.surface);
        while (!is_initial_configured)
            wl_display_dispatch(wayland.display);
    }

	void initialize_pango()
	{
		Pango::init();
	}

	void destroy_pango()
	{
		/* Requires linking and stuff. Live with the memory leak for now
		pango_cairo_font_map_set_default(NULL);
		cairo_debug_reset_static_data();
		FcFini();
		*/
	}

    void destroy_input()
    {
        safe_free(input.keyboard.xkb.state, xkb_state_unref);
        safe_free(input.keyboard.xkb.context, xkb_context_unref);
        safe_free(input.keyboard.object, wl_keyboard_destroy);

        safe_free(input.pointer.object, wl_pointer_destroy);
    }

    void destroy_buffers()
    {
        for (auto& buffer : buffers) {
			buffer.pango.layout.reset();
            buffer.cairo.context.reset();
            buffer.cairo.surface.reset();
            safe_free(buffer.buffer, wl_buffer_destroy);
            if (buffer.shm_data) {
                munmap(buffer.shm_data, buffer.shm_size);
                buffer.shm_data = nullptr;
            }
            buffer.shm_size = 0;
            buffer.busy = false;
        }
    }

    void destroy_window()
    {
        safe_free(window.callback, wl_callback_destroy);
        safe_free(window.xtoplevel, xdg_toplevel_destroy);
        safe_free(window.xsurface, xdg_surface_destroy);
        safe_free(window.surface, wl_surface_destroy);
    }

    void destroy_wayland()
    {
        safe_free(wayland.seat, wl_seat_destroy);
        safe_free(wayland.shm, wl_shm_destroy);
        safe_free(wayland.wm_base, xdg_wm_base_destroy);
        safe_free(wayland.compositor, wl_compositor_destroy);
        safe_free(wayland.registry, wl_registry_destroy);
        safe_free(wayland.display, wl_display_disconnect);
    }

public: /* section: primary */
    static void redraw(void* data, wl_callback* callback, uint32_t _time)
    {
        auto app = static_cast<App*>(data);

        static auto tp_last = app->tp_begin;
        const auto tp_now = std::chrono::high_resolution_clock::now();
        if (app->last_window_activated) {
            tp_last = tp_now;
            app->last_window_activated = false;
        }
        const auto delta_time_raw = tp_now - tp_last;
        const float delta_time = std::chrono::duration_cast<std::chrono::nanoseconds>(delta_time_raw).count() / 1e9f;
        tp_last = tp_now;

        app->elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>((tp_now - app->tp_begin) - app->duration_pause).count() / 1e9f;

        static float last_title_time = -1;
        if (app->elapsed_time - last_title_time > 0.25f) {
            last_title_time = app->elapsed_time;
            xdg_toplevel_set_title(
                app->window.xtoplevel,
                std::format("{} - {:.3f} FPS ({:.3f}ms, {:.3f}ms, {:.3f}ms)", title,
                    1.f / delta_time,
                    delta_time * 1e3f,
                    app->delta_update_time * 1e3f,
                    app->delta_draw_time * 1e3f)
                .c_str());
        }

        app->redraw(delta_time);

        if (callback)
            wl_callback_destroy(callback);
        iassert(app->window.callback = wl_surface_frame(app->window.surface));
        wl_callback_add_listener(app->window.callback, &redraw_listener, data);
        wl_surface_commit(app->window.surface);

        app->tp_very_last = std::chrono::high_resolution_clock::now();
    }

private: /* section: private primary */
    void redraw(float delta_time)
    {
        auto buffer = next_buffer();
        iassert(buffer);

        auto _tp_begin = std::chrono::high_resolution_clock::now();
        const float sub_dt = delta_time / substeps;
        for (unsigned i = 0; i < substeps; i++) {
            update(sub_dt);
            if (i != substeps - 1)
                elapsed_time += sub_dt;
        }
        auto _tp_end = std::chrono::high_resolution_clock::now();
        delta_update_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

        _tp_begin = std::chrono::high_resolution_clock::now();
        draw(buffer, delta_time);
        _tp_end = std::chrono::high_resolution_clock::now();
        delta_draw_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

        wl_surface_attach(window.surface, buffer->buffer, 0, 0);
        wl_surface_damage_buffer(window.surface, 0, 0, width, height);
    }

private: /* Meat: variables */
	int score[2] {};

	struct {
		struct {
			int text_size;
			int pad;
		} scoreboard;

		struct {
			glm::vec2 size;
			glm::vec2 pad;
		} pads;

		struct {
			int dash_size;
		} lines;

		struct {
			float radius, radius_factor;
		} ball;
	} dimensions {};

	struct {
		float position[2] {0.5, 0.5}; // y: 0 -> 1
		bool is_draw = false;
	} pads;

	struct {
		glm::vec2 position;
		glm::vec2 velocity_norm;
		float velocity_mag;
		bool is_draw = false;
	} ball {};

	enum class State {
		inactive, lost, active
	} state = State::inactive;

private: /* Meat: functions */
    void setup_pre()
    {
		const xkb_keysym_t keys[] {
			XKB_KEY_w, XKB_KEY_W,
			XKB_KEY_s, XKB_KEY_S,

			XKB_KEY_Up,
			XKB_KEY_Down,
		};
		for (auto key : keys) {
			input.keyboard.map[key] = WL_KEYBOARD_KEY_STATE_RELEASED;
		}
    }

    void setup()
    {
		ball.position = glm::vec2(0.5, 0.5);
		ball.velocity_norm = glm::linearRand(glm::vec2(-1, -1), glm::vec2(1, 1));
		ball.velocity_norm = glm::normalize(ball.velocity_norm);
		ball.velocity_mag = 0.75f;
    }
    
    void destroy()
    {
    }

    void update(float delta_time)
    {
		if (state == State::active)
		{
			const auto& map = input.keyboard.map;
			const float rate = 1.f * delta_time;

			if (map.at(XKB_KEY_w) or map.at(XKB_KEY_W)) pads.position[0] -= rate;
			if (map.at(XKB_KEY_s) or map.at(XKB_KEY_S)) pads.position[0] += rate;

			if (map.at(XKB_KEY_Up)) pads.position[1] -= rate;
			if (map.at(XKB_KEY_Down)) pads.position[1] += rate;

			pads.position[0] = glm::clamp(pads.position[0], 0.f, 1.f);
			pads.position[1] = glm::clamp(pads.position[1], 0.f, 1.f);

			integrate(delta_time);
		}
		else if (state == State::inactive)
		{
			bounce_off_walls();
			integrate(delta_time);
		}

		pads.is_draw = state == State::active or state == State::lost;
		ball.is_draw = state == State::active or state == State::inactive;
    }

	void integrate(float delta_time)
	{
		ball.position += ball.velocity_norm * ball.velocity_mag * delta_time;
	}

	void bounce_off_walls()
	{
		const float hf = dimensions.ball.radius_factor;
		if (ball.position.x <= hf) {
			ball.position.x = hf;
			ball.velocity_norm.x = -ball.velocity_norm.x;
		} else if (ball.position.x >= 1 - hf) {
			ball.position.x = 1 - hf;
			ball.velocity_norm.x = -ball.velocity_norm.x;
		}
		if (ball.position.y <= hf) {
			ball.position.y = hf;
			ball.velocity_norm.y = -ball.velocity_norm.y;
		} else if (ball.position.y >= 1 - hf) {
			ball.position.y = 1 - hf;
			ball.velocity_norm.y = -ball.velocity_norm.y;
		}
	}

	void on_key(xkb_keysym_t sym, wl_keyboard_key_state state)
	{
	}

    void draw(struct buffer* buffer, float delta_time)
    {
        auto& cr = *buffer->cairo.context;
		[[maybe_unused]] auto& pg = *buffer->pango.layout;
        [[maybe_unused]] auto& crs = *buffer->cairo.surface;

        cr.save();
        
        cr.translate(-width / 2.0, height / 2.0);
        cr.scale(1, -1);
        
        cr.set_source_rgb(0, 0, 0);
        cr.paint();

		{
			cr.set_source_rgb(1, 1, 1);

			const auto& dash_size = dimensions.lines.dash_size;
			const std::vector<double> dashes {(double)dash_size, (double)dash_size * 2};
			cr.set_dash(dashes, 0);

			cr.move_to(width / 2.0, 0);
			cr.line_to(width / 2.0, height);
			cr.stroke();
		}

		if (pads.is_draw)
		{
			cr.set_source_rgb(1, 1, 1);

			auto get_position = [&](int index) -> glm::vec2 {
				glm::vec2 actual_position = dimensions.pads.pad;

				actual_position.y = pads.position[index] * (height - (dimensions.pads.pad.y * 2 + dimensions.pads.size.y));
				actual_position.y += dimensions.pads.pad.y;

				if (index == 1) {
					actual_position.x = width - (dimensions.pads.pad.x + dimensions.pads.size.x);
				}

				return actual_position;
			};

			const auto& size = dimensions.pads.size;

			auto pos = get_position(0);
			cr.rectangle(pos.x, pos.y, size.x, size.y);
			cr.fill();

			pos = get_position(1);
			cr.rectangle(pos.x, pos.y, size.x, size.y);
			cr.fill();
		}

		if (ball.is_draw)
		{
			cr.set_source_rgb(1, 1, 1);

			glm::vec2 pos(ball.position.x * width, ball.position.y * height);

			cr.arc(pos.x, pos.y, dimensions.ball.radius, 0, 2 * M_PI);
			cr.fill();
		}
        
		{
			cr.set_source_rgb(1, 1, 1);

			const std::string str[2] = {std::format("{}", score[0]), std::format("{}", score[1])};
			const auto& pad = dimensions.scoreboard.pad;

			pg.set_text(str[0]);
			Pango::Rectangle ink_rect, logical_rect;
			pg.get_pixel_extents(ink_rect, logical_rect);
			cr.move_to(width / 2.0 - logical_rect.get_width() - pad, 0);
			pg.show_in_cairo_context(buffer->cairo.context);

			pg.set_text(str[1]);
			cr.move_to(width / 2.0 + pad, 0);
			pg.show_in_cairo_context(buffer->cairo.context);
		}

        cr.restore();
    }

	void on_create_buffer(struct buffer* buffer)
	{
		{
		auto& scoreboard = dimensions.scoreboard;
		scoreboard.text_size = glm::round(width * (5 / 100.f));
		scoreboard.pad = width * (5 / 100.f);

		auto& pads = dimensions.pads;
		pads.size.x = width * (2.75f / 100.f);
		pads.size.y = 3.25f * pads.size.x;
		pads.pad.x = width * (3 / 100.f);
		pads.pad.y = height * (3 / 100.f);

		auto& lines = dimensions.lines;
		lines.dash_size = height * (1.f / 100.f);

		auto& ball = dimensions.ball;
		ball.radius = width * (2.f / 100.f);
		ball.radius_factor = ball.radius / width;

		static Pango::FontDescription desc;
		auto& pg = *buffer->pango.layout;
		desc = Pango::FontDescription(std::format("Ubuntu {}", scoreboard.text_size));
		pg.set_font_description(desc);
		}
	}

private: /* Helpers */
    void pixel_range2(struct buffer* buffer, int x, int y, int ex, int ey, uint32_t color)
    {
        centered(x, y);
        centered(ex, ey);
        pixel_range(buffer, x, y, ex, ey, color);
    }

    void pixel_range(struct buffer* buffer, int x, int y, int ex, int ey, uint32_t color)
    {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        ex = std::clamp(ex + 1, 0, width - 1);
        ey = std::clamp(ey, 0, height - 1);
        const auto location = at(x, y), location_end = at(ex, ey);
        if (location <= location_end)
            std::fill(&buffer->shm_data_u32[location], &buffer->shm_data_u32[location_end], color);
    }

    uint32_t& pixel_at2(struct buffer* buffer, int x, int y)
    {
        centered(x, y);
        return pixel_at(buffer, x, y);
    }

    uint32_t& pixel_at(struct buffer* buffer, int x, int y)
    {
        static uint32_t facade;
        if ((x < 0 or x >= width) or (y < 0 or y >= height)) {
            facade = 0;
            return facade;
        }
        const ssize_t location = at(x, y);
        return buffer->shm_data_u32[location];
    }

    void uncentered(int& x, int& y)
    {
        x -= width / 2;
        y = -y;
        y += height / 2;
    }

    void centered(int& x, int& y)
    {
        x += width / 2;
        y -= height / 2;
        y = -y;
    }

    ssize_t at(int x, int y)
    {
        return (y * width) + x;
    }

    struct buffer* next_buffer()
    {
        struct buffer* buffer = nullptr;

        for (auto& one : buffers) {
            if (!one.busy) {
                buffer = &one;
                break;
            }
        }
        iassert(buffer);

        if (!buffer->buffer || rebuild_buffers) {
            if (rebuild_buffers) {
                destroy_buffers();
                rebuild_buffers = false;
            }
            create_shm_buffer(buffer, width, height, WL_SHM_FORMAT_XRGB8888);

            auto& cr = *buffer->cairo.context;
            cr.translate(width / 2.0, height / 2.0);
            cr.scale(1, -1);

			on_create_buffer(buffer);
        }

        return buffer;
    }

    void create_shm_buffer(struct buffer* buffer, int width, int height, uint32_t format)
    {
        const auto cr_format = shm_to_cairo_format(format);

        const ssize_t stride = Cairo::ImageSurface::format_stride_for_width(cr_format, width);
        iassert(stride != -1);
        const size_t size = stride * height;
        buffer->shm_size = size;

        int fd;
        iassert((fd = create_anonymous_file(size)) > 0);

        void* data;
        iassert(data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        buffer->shm_data = data;

        wl_shm_pool* pool;
        iassert(pool = wl_shm_create_pool(wayland.shm, fd, size));
        iassert(buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format));
        wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
        wl_shm_pool_destroy(pool);
        close(fd);

        iassert(buffer->cairo.surface = Cairo::ImageSurface::create((unsigned char*)data, cr_format, width, height, stride));
        iassert(buffer->cairo.context = Cairo::Context::create(buffer->cairo.surface));

		iassert(buffer->pango.layout = Pango::Layout::create(buffer->cairo.context));
    }

    int create_anonymous_file(size_t size)
    {
        int fd, ret;

        fd = memfd_create("opengl-studies", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd > 0) {
            fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
            do {
                ret = ftruncate(fd, size);
            } while (ret < 0 && errno == EINTR);
            if (ret < 0) {
                close(fd);
                return -1;
            }
        }

        return fd;
    }

    Cairo::Surface::Format shm_to_cairo_format(uint32_t shm_format)
    {
        auto cr_format = static_cast<Cairo::Surface::Format>(0);
        switch (shm_format) {
        case WL_SHM_FORMAT_XRGB8888:
            cr_format = Cairo::Surface::Format::RGB24;
            break;
        case WL_SHM_FORMAT_ARGB8888:
            cr_format = Cairo::Surface::Format::ARGB32;
            break;
        default:
            iassert(false, "Only certain wl_shm formats are supported");
            break;
        }
        return cr_format;
    }

public: /* section: listeners */
    static void on_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
    {
        // log_event(__func__, "{} v{}", interface, version);

        auto app = static_cast<App*>(data);

        if (strcmp(interface, "wl_shm") == 0) {
            iassert(app->wayland.shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1));
            wl_shm_add_listener(app->wayland.shm, &shm_listener, data);
        } else if (strcmp(interface, "wl_compositor") == 0) {
            iassert(app->wayland.compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4));
        } else if (strcmp(interface, "xdg_wm_base") == 0) {
            iassert(app->wayland.wm_base = (xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            xdg_wm_base_add_listener(app->wayland.wm_base, &wm_base_listener, data);
        } else if (strcmp(interface, "wl_seat") == 0) {
            iassert(app->wayland.seat = (wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 5));
            wl_seat_add_listener(app->wayland.seat, &seat_listener, data);
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

        auto app = static_cast<App*>(data);
        app->is_initial_configured = true;

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
                app->last_window_activated = true;

                const auto tp_now = std::chrono::high_resolution_clock::now();
                const auto
                    duration_total
                    = tp_now - app->tp_begin,
                    duration_last_pause = app->tp_very_last - app->tp_begin;

                app->duration_pause += duration_total - duration_last_pause;
            } break;

            default:
                break;
            }
        }

        if (!((width == 0 || height == 0) || (width == app->width && height == app->height))) {
            app->width = width;
            app->height = height;
            app->rebuild_buffers = true;
        }
        app->is_initial_configured = false;
    }
    static void on_xtoplevel_close(void* data, xdg_toplevel* xtoplevel)
    {
        // log_event(__func__);

        auto app = static_cast<App*>(data);
        app->running = false;
    }

    static void on_buffer_release(void* data, wl_buffer* buffer)
    {
        // log_event(__func__, "{}", data);

        auto current_buffer = static_cast<struct buffer*>(data);
        current_buffer->busy = false;
    }

    static void on_seat_capabilities(void* data, wl_seat* seat, uint32_t caps)
    {
        // log_event(__func__, "0x{:x}", caps);

        auto app = static_cast<App*>(data);

        if (!app->input.pointer.object) {
            if (caps & WL_SEAT_CAPABILITY_POINTER) {
                iassert(app->input.pointer.object = wl_seat_get_pointer(seat));
                wl_pointer_add_listener(app->input.pointer.object, &pointer_listener, data);
            }
        } else {
            if (!(caps & WL_SEAT_CAPABILITY_POINTER)) {
                safe_free(app->input.pointer.object, wl_pointer_destroy);
            }
        }

        if (!app->input.keyboard.object) {
            if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
                iassert(app->input.keyboard.object = wl_seat_get_keyboard(seat));
                wl_keyboard_add_listener(app->input.keyboard.object, &keyboard_listener, data);
            }
        } else {
            if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
                safe_free(app->input.keyboard.object, wl_keyboard_destroy);
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
        app->uncentered(ix, iy);
        app->input.pointer.cpos = glm::ivec2(ix, iy);
    }
    static void on_pointer_leave(void* data, wl_pointer* pointer, uint32_t, wl_surface*)
    {
    }
    static void on_pointer_motion(void* data, wl_pointer* pointer, uint32_t, wl_fixed_t x, wl_fixed_t y)
    {
        auto app = static_cast<App*>(data);

        int ix = wl_fixed_to_int(x), iy = wl_fixed_to_int(y);
        app->input.pointer.pos = glm::ivec2(ix, iy);
        app->uncentered(ix, iy);
        app->input.pointer.cpos = glm::ivec2(ix, iy);
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
        default:
            break;
        }
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
        
        auto u32 = static_cast<char32_t>(xkb_state_key_get_utf32(xkb.state, scancode));
        keyboard.map_utf[u32] = keystate;
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
    static constexpr wl_buffer_listener buffer_listener {
        .release = on_buffer_release
    };
    static constexpr wl_callback_listener redraw_listener {
        redraw
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

    template <class... Args>
    static void log_event(const char* function, Args&&... args)
    {
        std::print(stderr, "event {}", function);
        if constexpr (sizeof...(args) > 0)
            _log_event_args(args...);
        std::println("");
    }
    template <class Format, class... Args>
    static void _log_event_args(Format&& format, Args&&... args)
    {
        std::print(stderr, ": ");
        std::vprint_unicode(stderr, format, std::make_format_args(args...));
    }

public: /* section: helpers */
    static void safe_free(auto& pointer, auto freer)
    {
        if (pointer) {
            freer(pointer);
            pointer = nullptr;
        }
    }

    template <class... Args>
    static void _iassert(const char* expr, std::source_location where, Args&&... args)
    {
        std::print(stderr, "{}:{}: {}: Assertion `{}` failed",
            where.file_name(), where.line(), where.function_name(),
            expr);
        if constexpr (sizeof...(args) > 0)
            _iassert_args(args...);
        std::println(stderr, "");
        throw assertion();
    }

    template <class Format, class... Args>
    static void _iassert_args(Format&& format, Args&&... args)
    {
        std::print(stderr, ": ");
        std::vprint_unicode(stderr, format, std::make_format_args(args...));
    }

public: /* section: public classes */
    class assertion : public std::exception { };
};

int main()
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    App app;
    try {
        app.initialize();
        app.run();
    } catch (const App::assertion&) {
        return 1;
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal std::exception: {}", e.what());
        return 2;
    }
}
