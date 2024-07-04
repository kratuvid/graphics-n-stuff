#ifdef DEBUG
#define iassert(expr, ...) if (!(expr)) \
		App::_iassert(#expr, std::source_location::current() __VA_OPT__(,) __VA_ARGS__);
#else
#define iassert(expr, ...) if (!(expr));
#endif

#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>

class App
{
private: /* section: variables */
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
		} pointer;
		struct {
			wl_keyboard* object;
		} keyboard;
	} input {};

	struct
	{
		wl_surface* surface;
		xdg_surface* xsurface;
		xdg_toplevel* xtoplevel;
		wl_callback* callback;
	} window {};

	struct buffer
	{
		wl_buffer* buffer;
		bool busy;
		union {
			void* shm_data;
			uint32_t* shm_data_u32;
			uint8_t* shm_data_u8;
		};
		size_t shm_size;
	} buffers[2] {};

	bool rebuild_buffers = false;
	bool is_initial_configured = false;
	bool running = true;
	int width = 800, height = 600;
	float delta_update_time = 0, delta_draw_time = 0;

	std::chrono::high_resolution_clock::time_point tp_begin, tp_very_last;
	std::chrono::nanoseconds duration_pause {0};
	bool last_window_activated = false;

	static constexpr std::string_view title {"Cloth"};

public: /* section: public interface */
	void initialize()
	{
		srand(time(nullptr));
		initialize_wayland();
		initialize_window();
	}

	void run()
	{
		tp_begin = tp_very_last = std::chrono::high_resolution_clock::now();

		setup();
		redraw(this, nullptr, 0);
		setup_post();

		while (running && wl_display_dispatch(wayland.display) != -1);
	}

	~App()
	{
		destroy_input();
		destroy_buffers();
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
		while (!is_initial_configured) wl_display_dispatch(wayland.display);
	}

	void destroy_input()
	{
		safe_free(input.keyboard.object, wl_keyboard_destroy);
		safe_free(input.pointer.object, wl_pointer_destroy);
	}

	void destroy_buffers()
	{
		for (auto& buffer : buffers)
		{
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

		const float time = std::chrono::duration_cast<std::chrono::nanoseconds>((tp_now - app->tp_begin) - app->duration_pause).count() / 1e9f;

		static float last_title_time = -1;
		if (time - last_title_time > 0.25f) {
			last_title_time = time;
			xdg_toplevel_set_title(
				app->window.xtoplevel,
				std::format("{} - {:.3f} FPS ({:.3f}ms, {:.3f}ms, {:.3f}ms)", title,
							1.f / delta_time,
							delta_time * 1e3f,
							app->delta_update_time * 1e3f,
							app->delta_draw_time * 1e3f
					).c_str());
		}

		app->redraw(time, delta_time);

		if (callback)
			wl_callback_destroy(callback);
		iassert(app->window.callback = wl_surface_frame(app->window.surface));
		wl_callback_add_listener(app->window.callback, &redraw_listener, data);
		wl_surface_commit(app->window.surface);

		app->tp_very_last = std::chrono::high_resolution_clock::now();
	}

private: /* section: private primary */
	void redraw(float time, float delta_time)
	{
		auto buffer = next_buffer();
		iassert(buffer);

		auto _tp_begin = std::chrono::high_resolution_clock::now();
		update(time, delta_time);
		auto _tp_end = std::chrono::high_resolution_clock::now();
		delta_update_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		clear(buffer);

		_tp_begin = std::chrono::high_resolution_clock::now();
		draw(buffer, time);
		_tp_end = std::chrono::high_resolution_clock::now();
		delta_draw_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		wl_surface_attach(window.surface, buffer->buffer, 0, 0);
		wl_surface_damage_buffer(window.surface, 0, 0, width, height);
	}

private: /* Meat: variables */

private: /* Meat: functions */
	void setup()
	{
	}

	void setup_post()
	{
	}

	void update(float time, float delta_time)
	{
	}

	void draw(struct buffer* buffer, float time)
	{
		const float in = time * M_PIf;
		const float radius = 100.f + glm::abs(glm::sin(in)) * 50.f;
		line(buffer, glm::vec2(glm::cos(in), glm::sin(in)) * radius, input.pointer.cpos, 0xffffff);
		circle(buffer, radius, {0, 0}, 0xffffff);
	}

	void clear(struct buffer* buffer)
	{
		memset(buffer->shm_data, 0x00, buffer->shm_size);
		// std::fill(buffer->shm_data_u8, buffer->shm_data_u8 + buffer->shm_size, 0x00);
		// std::fill(buffer->shm_data_u32, buffer->shm_data_u32 + buffer->shm_size / 4, 0x00'00'00);
	}

private: /* helpers */
	void line(struct buffer* buffer, const glm::ivec2& start_raw, const glm::ivec2& end_raw, uint32_t color)
	{
		// Ref: https://en.wikipedia.org/wiki/Bresenham's_line_algorithm

		auto start = &start_raw, end = &end_raw;

		int dx = end->x - start->x, dy = end->y - start->y;

		if (std::abs(dy) < std::abs(dx)) {
			if (start->x > end->x) {
				std::swap(start, end);
				dx = -dx;
				dy = -dy;
			}

			int yi = 1;
			if (dy < 0) {
				yi = -1;
				dy = -dy;
			}
			int D = 2 * dy - dx;
			int y = start->y;

			for (int x = start->x; x <= end->x; x++)
			{
				pixel_at2(buffer, x, y) = color;
				if (D > 0) {
					y += yi;
					D += 2 * (dy - dx);
				} else {
					D += 2 * dy;
				}
			}
		} else {
			if (start->y > end->y) {
				std::swap(start, end);
				dx = -dx;
				dy = -dy;
			}

			int xi = 1;
			if (dx < 0) {
				xi = -1;
				dx = -dx;
			}
			int D = 2 * dx - dy;
			int x = start->x;

			for (int y = start->y; y <= end->y; y++)
			{
				pixel_at2(buffer, x, y) = color;
				if (D > 0) {
					x += xi;
					D += 2 * (dx - dy);
				} else {
					D += 2 * dx;
				}
			}
		}
	}

	void circle(struct buffer* buffer, float radius, const glm::ivec2& center, uint32_t color, bool filled = false)
	{
		const float y_max = radius * std::sin(M_PIf / 4);
		const int cx = center.x, cy = center.y;

		auto mirror = [&](int x, int y) {
			for (int i = 0; i < 2; i++) {
				if (filled) {
					pixel_range2(buffer, -x + cx, y + cy, x + cx, y + cy, color);
					pixel_range2(buffer, -x + cx, -y + cy, x + cx, -y + cy, color);
				} else {
					pixel_at2(buffer, x + cx, y + cy) = color;
					pixel_at2(buffer, -x + cx, y + cy) = color;
					pixel_at2(buffer, x + cx, -y + cy) = color;
					pixel_at2(buffer, -x + cx, -y + cy) = color;
				}
				std::swap(x, y);
			}
		};

		float x = radius, y = 0.f;
		if (filled) {
			pixel_range2(buffer, -x + cx, cy, x + cx, cy, color);
		} else {
			pixel_at2(buffer, x + cx, cy) = color;
			pixel_at2(buffer, -x + cx, cy) = color;
		}
		pixel_at2(buffer, cx, x + cy) = color;
		pixel_at2(buffer, cx, -x + cy) = color;

		for (y = 1.f; y <= y_max; y += 1.f)
		{
			x = std::sqrt(x*x - 2*y - 1);
			mirror(x, y);
		}
	}

	void pixel_range2(struct buffer* buffer, int x, int y, int ex, int ey, uint32_t color)
	{
		centered(x, y);
		centered(ex, ey);
		pixel_range(buffer, x, y, ex, ey, color);
	}

	void pixel_range(struct buffer* buffer, int x, int y, int ex, int ey, uint32_t color)
	{
		x = std::clamp(x, 0, width-1); y = std::clamp(y, 0, height-1);
		ex = std::clamp(ex + 1, 0, width-1); ey = std::clamp(ey, 0, height-1);
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

	static uint32_t pixel_brightness(uint32_t color, float factor)
	{
		auto singles = (uint8_t*)&color;
		for (uint8_t* channel = singles; channel < singles+4; channel++)
			*channel = std::clamp(int(*channel * factor), 0, 0xff);
		return color;
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

		if (!buffer->buffer || rebuild_buffers)
		{
			if (rebuild_buffers) {
				destroy_buffers();
				rebuild_buffers = false;
			}
			create_shm_buffer(buffer, width, height, WL_SHM_FORMAT_XRGB8888);
		}

		return buffer;
	}

	void create_shm_buffer(struct buffer* buffer, int width, int height, uint32_t format)
	{
		const size_t stride = width * 4;
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
	}

	int create_anonymous_file(size_t size)
	{
		int fd, ret;

		fd = memfd_create("opengl-studies", MFD_CLOEXEC | MFD_ALLOW_SEALING);
		if (fd > 0)
		{
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

public: /* section: listeners */
	static void on_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
	{
		log_event(__func__, "{} v{}", interface, version);

		auto app = static_cast<App*>(data);

		if (strcmp(interface, "wl_shm") == 0)
		{
			iassert(app->wayland.shm = (wl_shm*)wl_registry_bind(registry, name, &wl_shm_interface, 1));
			wl_shm_add_listener(app->wayland.shm, &shm_listener, data);
		}
		else if (strcmp(interface, "wl_compositor") == 0)
		{
			iassert(app->wayland.compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, 4));
		}
		else if (strcmp(interface, "xdg_wm_base") == 0)
		{
			iassert(app->wayland.wm_base = (xdg_wm_base*)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
			xdg_wm_base_add_listener(app->wayland.wm_base, &wm_base_listener, data);
		}
		else if (strcmp(interface, "wl_seat") == 0)
		{
			iassert(app->wayland.seat = (wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, 5));
			wl_seat_add_listener(app->wayland.seat, &seat_listener, data);
		}
	}
	static void on_registry_global_remove(void* data, wl_registry* registry, uint32_t name)
	{
		log_event(__func__, "{}", name);
	}

	static void on_shm_format(void* data, wl_shm* shm, uint32_t format)
	{
		log_event(__func__, "0x{:x}", format);
	}

	static void on_wm_base_ping(void* data, xdg_wm_base* wm_base, uint32_t serial)
	{
		log_event(__func__, "{}", serial);

		xdg_wm_base_pong(wm_base, serial);
	}

	static void on_xsurface_configure(void* data, xdg_surface* xsurface, uint32_t serial)
	{
		log_event(__func__, "{}", serial);

		auto app = static_cast<App*>(data);
		app->is_initial_configured = true;

		xdg_surface_ack_configure(xsurface, serial);
	}

	static void on_xtoplevel_configure(void* data, xdg_toplevel* xtoplevel, int32_t width, int32_t height, wl_array* states)
	{
		log_event(__func__, "{}x{} {}", width, height, states->size);

		auto app = static_cast<App*>(data);

		for (uint32_t* ptr = (uint32_t*)states->data;
			 states->size != 0 && (uint8_t*)ptr < ((uint8_t*)states->data + states->size);
			 ptr++)
		{
			switch (*ptr)
			{
			case XDG_TOPLEVEL_STATE_ACTIVATED:
			{
				app->last_window_activated = true;

				const auto tp_now = std::chrono::high_resolution_clock::now();
				const auto
					duration_total = tp_now - app->tp_begin,
					duration_last_pause = app->tp_very_last - app->tp_begin;

				app->duration_pause += duration_total - duration_last_pause;
			}
			break;

			default:
				break;
			}
		}

		if (!((width == 0 || height == 0) || (width == app->width && height == app->height)))
		{
			app->width = width;
			app->height = height;
			app->rebuild_buffers = true;
		}
		app->is_initial_configured = false;
	}
	static void on_xtoplevel_close(void* data, xdg_toplevel* xtoplevel)
	{
		log_event(__func__);

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
		log_event(__func__, "0x{:x}", caps);

		auto app = static_cast<App*>(data);

		if (!app->input.pointer.object)
		{
			if (caps & WL_SEAT_CAPABILITY_POINTER) {
				iassert(app->input.pointer.object = wl_seat_get_pointer(seat));
				wl_pointer_add_listener(app->input.pointer.object, &pointer_listener, data);
			}
		}
		else
		{
			if (!(caps & WL_SEAT_CAPABILITY_POINTER)) {
				safe_free(app->input.pointer.object, wl_pointer_destroy);
			}
		}

		if (!app->input.keyboard.object)
		{
			if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
				iassert(app->input.keyboard.object = wl_seat_get_keyboard(seat));
				wl_keyboard_add_listener(app->input.keyboard.object, &keyboard_listener, data);
			}
		}
		else
		{
			if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
				safe_free(app->input.keyboard.object, wl_keyboard_destroy);
			}
		}
	}
	static void on_seat_name(void* data, wl_seat* seat, const char* name)
	{
		log_event(__func__, "{}", name);
	}

	static void on_pointer_enter(void* data, wl_pointer* pointer, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t)
	{
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
	static void on_pointer_button(void* data, wl_pointer* pointer, uint32_t, uint32_t, uint32_t, uint32_t)
	{
	}
	static void on_pointer_axis(void* data, wl_pointer* pointer, uint32_t, uint32_t, wl_fixed_t)
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
		log_event(__func__, "{} {}", fd, size);
		iassert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
	}
	static void on_keyboard_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys)
	{
		log_event(__func__);
	}
	static void on_keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
	{
		log_event(__func__);
	}
	static void on_keyboard_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
	{
		log_event(__func__, "0x{:x}: {}", key, state);
	}
	static void on_keyboard_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
	{
		log_event(__func__);
	}
	static void on_keyboard_repeat_info(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay)
	{
		log_event(__func__);
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

	template<class... Args>
	static void log_event(const char* function, Args&&... args)
	{
		std::print(stderr, "event {}", function);
		if constexpr (sizeof...(args) > 0)
			_log_event_args(args...);
		std::println("");
	}
	template<class Format, class... Args>
	static void _log_event_args(Format&& format, Args&&... args)
	{
		std::print(stderr, ": ");
		std::vprint_unicode(stderr, format, std::make_format_args(args...));
	}

public: /* section: helpers */
	static void safe_free(auto& pointer, auto freer)
	{
		if (pointer)
		{
			freer(pointer);
			pointer = nullptr;
		}
	}

	template<class... Args>
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

	template<class Format, class... Args>
	static void _iassert_args(Format&& format, Args&&... args)
	{
		std::print(stderr, ": ");
		std::vprint_unicode(stderr, format, std::make_format_args(args...));
	}

public: /* section: public classes */
	class assertion : public std::exception {};
};
