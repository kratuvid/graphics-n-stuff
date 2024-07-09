#include "raytracer/pch.hpp"

#include "raytracer/types.hpp"
#include "raytracer/ray.hpp"

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
    static constexpr std::string_view title { "Raytracer!" };
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
            // spdlog::debug("Voluntary context switches: {}", usage.ru_nvcsw);
            // spdlog::debug("Involuntary context switches: {}", usage.ru_nivcsw);
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
    struct  {
        std::thread thread;

        struct {
            float percentage = 0.f;
            std::atomic_bool finished = false;
            std::chrono::nanoseconds time;

            std::vector<uint32_t> canvas;
        } out;

        struct {
            int width, height;
			bool kill = false;
			std::binary_semaphore semaphore {0};

			ovec3 offset {};
        } in;

		struct {
			std::atomic_bool stop = false;
		} inout;

		void setup(App* app) {
			thread = std::thread(workspace, std::ref(*this));
		}

		void destroy() {
			in.kill = inout.stop = true;
			in.semaphore.release();

			if (thread.native_handle())
				thread.join();
		}
    } worker;

	bool setup_finished = false;
	int last_dimens[2] {-1, -1};
	bool render_again = false;

private: /* Meat: functions */
    void setup_pre()
    {
    }

    void setup()
    {
		worker.setup(this);
		setup_finished = true;
    }
    
    void destroy()
    {
		worker.destroy();
    }

    void update(float delta_time)
    {
		if (setup_finished and (width != last_dimens[0] or height != last_dimens[1] or render_again)) {
			worker.in.width = width; worker.in.height = height;
			worker.out.canvas.resize(width * height);

			worker.inout.stop = true;
			worker.in.semaphore.release();

			last_dimens[0] = width; last_dimens[1] = height;
			render_again = false;
		}

		/*
		const float in = elapsed_time * 2 * M_PIf * 0.1f;
		const ovec2 disp(1, 1);
		worker.in.offset.x = glm::cos(in) * disp.x;
		worker.in.offset.y = glm::sin(in) * disp.y;
		render_again = true;
		*/

		const float delta = 0.5 * delta_time;
		auto& map = input.keyboard.map_utf;

		if (map['w']) {
			worker.in.offset.y += delta;
			render_again = true;
		} if (map['s']) {
			worker.in.offset.y -= delta;
			render_again = true;
		} if (map['d']) {
			worker.in.offset.x += delta;
			render_again = true;
		} if (map['a']) {
			worker.in.offset.x -= delta;
			render_again = true;
		} if (map['e']) {
			worker.in.offset.z -= delta;
			render_again = true;
		} if (map['q']) {
			worker.in.offset.z += delta;
			render_again = true;
		}

		if (render_again)
			worker.in.offset = glm::clamp(worker.in.offset, ovec3(-1, -1, -1), ovec3(1, 1, 1));
    }

	void on_key(xkb_keysym_t sym, wl_keyboard_key_state state)
	{
		if (!state)
		{
			switch (sym)
			{
			case XKB_KEY_space:
				render_again = true;
				break;
			}
		}
	}
    
    static void workspace(decltype(worker)& state)
    {
		while (true)
		{
			state.in.semaphore.acquire();

			if (state.in.kill) break;
			
			state.inout.stop = false;
			state.out.finished = false;
			state.out.percentage = 0.f;

			const auto& offset = state.in.offset;

			auto at = [&state](size_t x, size_t y) -> auto& {
				return state.out.canvas[y * state.in.width + x];
			};

			/* setup */
			const int width = state.in.width, height = state.in.height;

			// image
			oreal aspect_ratio = state.in.width / oreal(state.in.height);

			// camera
			oreal focal_length = 1.0;
			oreal viewport_height = 2.0;
			oreal viewport_width = viewport_height * aspect_ratio;
			ovec3 camera_center(0, 0, 0);

			// viewport specific
			ovec3 viewport_u(viewport_width, 0, 0);
			ovec3 viewport_v(0, -viewport_height, 0);

			// calculate the horizontal and vertical delta vectors from pixel to pixel.
			auto pixel_delta_u = viewport_u / oreal(width);
			auto pixel_delta_v = viewport_v / oreal(height);

			// calculate the location of the upper left pixel.
			auto viewport_upper_left = camera_center - ovec3(0, 0, focal_length) - viewport_u / oreal(2) - viewport_v / oreal(2);
			auto pixel00_loc = viewport_upper_left + oreal(0.5) * (pixel_delta_u + pixel_delta_v);

			// functions
			auto color_u32 = [](ovec3 incolor) -> uint32_t {
				incolor = glm::clamp(incolor, ovec3(0, 0, 0), ovec3(1, 1, 1));
				return uint32_t(incolor.r * 255) << 16 | uint32_t(incolor.g * 255) << 8 | uint32_t(incolor.b * 255);
			};

			auto hit_sphere = [&](ovec3 const& center, oreal radius, Ray const& r) -> oreal {
				ovec3 oc = center - r.origin();
				auto a = glm::length2(r.direction());
				auto h = glm::dot(r.direction(), oc);
				auto c = glm::length2(oc) - radius * radius;
				auto discriminant = h * h - a * c;

				if (discriminant < 0) {
					return -1.0;
				} else {
					return (h - glm::sqrt(discriminant)) / a;
				}
			};

			auto ray_color = [&](Ray const& r) -> auto {
				ovec3 color;

				const ovec3 pos(offset.x, offset.y, -1 + offset.z);
				oreal t = hit_sphere(pos, oreal(0.5), r);
				if (t > oreal(0.0)) {
					ovec3 N = glm::normalize(r.at(t) - pos);
					return color_u32(oreal(0.5) * (N + oreal(1.0)));
				}

				auto unit_direction = r.direction();
				oreal a = oreal(0.5) * (unit_direction.y + oreal(1.0));
				color = (oreal(1.0) - a) * ovec3(1, 1, 1) + a * ovec3(0.5, 0.7, 1.0);

				return color_u32(color);
			};
			
			/* actual work */
			auto begin = std::chrono::high_resolution_clock::now();

			for (int j=0; j < height; j++)
			{
				for (int i=0; i < width; i++)
				{
					auto pixel_center = pixel00_loc + (oreal(i) * pixel_delta_u) + (oreal(j) * pixel_delta_v);
					auto ray_direction = pixel_center - camera_center;
					Ray r(camera_center, ray_direction);

					uint32_t color = ray_color(r);
					at(i, j) = color;
				}

				state.out.percentage = j / float(height-1);

				if (state.in.kill or state.inout.stop) break;
			}

			auto end = std::chrono::high_resolution_clock::now();
			state.out.time = end - begin;
			
			state.out.finished = true;

			if (state.in.kill) break;
		}
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
        
        cr.set_source_rgb(1, 1, 1);
        cr.move_to(50, 50);

        const size_t size = std::min(buffer->shm_size, worker.out.canvas.size() * sizeof(uint32_t));
        memcpy(buffer->shm_data_u32, worker.out.canvas.data(), size);

		static std::string str;
        if (worker.out.finished) {
			str = std::format("Took {:.3f}s", worker.out.time.count() / 1e9f);
        } else {
			static auto last_time = -100.f;
			if (elapsed_time >= last_time + 0.1f) {
				str = std::format("{:.2f}%", worker.out.percentage * 100.f);
				last_time = elapsed_time;
			}
        }
		pg.set_text(str);
		pg.show_in_cairo_context(buffer->cairo.context);

        cr.restore();
    }

	void on_create_buffer(struct buffer* buffer)
	{
		static Pango::FontDescription desc;

		auto& pg = *buffer->pango.layout;
		desc = Pango::FontDescription("Ubuntu 20");
		pg.set_font_description(desc);
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
        if (sym != XKB_KEY_NoSymbol)
            keyboard.map[sym] = keystate;
        
        auto u32 = static_cast<char32_t>(xkb_state_key_get_utf32(xkb.state, scancode));
        keyboard.map_utf[u32] = keystate;

		app->on_key(sym, keystate);
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
