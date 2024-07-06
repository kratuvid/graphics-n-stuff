#ifndef DISABLE_IASSERT
#define iassert(expr, ...) if (!(expr)) \
		App::_iassert(#expr, std::source_location::current() __VA_OPT__(,) __VA_ARGS__);
#else
#define iassert(expr, ...) if (!(expr));
#endif

#include <linux/input-event-codes.h>

#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/norm.hpp>

#include <cairomm/cairomm.h>

class App
{
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

		struct {
			Cairo::RefPtr<Cairo::ImageSurface> surface;
			Cairo::RefPtr<Cairo::Context> context;
		} cairo;
	} buffers[2] {};

	bool rebuild_buffers = false;
	// END - wayland

	// state
	bool is_initial_configured = false;
	bool running = true;
	int width = 800, height = 600;
	float delta_update_time = 0, delta_draw_time = 0;

	std::chrono::high_resolution_clock::time_point tp_begin, tp_very_last;
	std::chrono::nanoseconds duration_pause {0};
	bool last_window_activated = false;

	static constexpr unsigned substeps = 16;
	static constexpr std::string_view title {"Two Pendulum"};
	// END - state

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

		setup_pre();
		redraw(this, nullptr, 0);
		wl_display_roundtrip(wayland.display);
		setup();

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
		const float sub_dt = delta_time / substeps;
		for (unsigned i = 0; i < substeps; i++)
			update(time + sub_dt * i, sub_dt);
		auto _tp_end = std::chrono::high_resolution_clock::now();
		delta_update_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		_tp_begin = std::chrono::high_resolution_clock::now();
		draw(buffer, time);
		_tp_end = std::chrono::high_resolution_clock::now();
		delta_draw_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		wl_surface_attach(window.surface, buffer->buffer, 0, 0);
		wl_surface_damage_buffer(window.surface, 0, 0, width, height);
	}

private: /* Meat: variables */
	glm::vec2 gravity = glm::vec2(0, -9.8) * 1.f;

	struct Pendulum {
		App* app;
		float width, height;

		glm::vec2 anchor;
		glm::vec3 color;
		const glm::vec2 rect {32, 32};
		const float radius = 32.f;

		float theta[2], NL[2], k[2];
		float mass[2];

		glm::vec2 gravity;
		struct {
			glm::vec2 position;
			glm::vec2 velocity, velocity_old;
			glm::vec2 acceleration;
		} motion[2] {};

		glm::vec2 restoring_second {};

		struct VisualVector {
			glm::vec2 vector;
			glm::vec3 color;
		} forces[2][3] = {{
				{{}, {1, 1, 0.1}},
				{{}, {0.1, 1, 1}},
				{{}, {1, 0.1, 1}},
			}, {}};

		Pendulum(App* app, glm::vec3 const& color, glm::vec2 const& gravity, const float theta[2], const float NL[2], const float k[2], const float mass[2]) {
			this->app = app;

			this->gravity = gravity;
			this->color = color;

			memcpy(this->theta, theta, sizeof(this->theta));
			memcpy(this->NL, NL, sizeof(this->NL));
			memcpy(this->k, k, sizeof(this->k));
			memcpy(this->mass, mass, sizeof(this->mass));

			memcpy(forces[1], forces[0], sizeof(forces[1]));
		}

		void setup(glm::vec2 const& anchor) {
			this->anchor = anchor;

			width = app->width;
			height = app->height;

			motion[0].position = anchor + glm::vec2(glm::cos(theta[0]), glm::sin(theta[0])) * NL[0];
			/*
			const float velocity_range = 50;
			const auto velocity = glm::linearRand(glm::vec2(-3, -0.25) * velocity_range, glm::vec2(3, 2) * velocity_range);
			const float delta_time = 1.f / 60;

			position[1] = position[0] + velocity * delta_time + 0.5f * app->gravity * delta_time * delta_time;
			position[2] = position[1];
			*/
			motion[0].velocity_old = motion[0].velocity = glm::vec2();
			motion[0].acceleration = glm::vec2();

			motion[1].position = motion[0].position + glm::vec2(glm::cos(theta[1]), glm::sin(theta[1])) * NL[1];
			motion[1].velocity_old = motion[1].velocity = glm::vec2();
			motion[1].acceleration = glm::vec2();
		}

		glm::vec2 calculate_force(int index, glm::vec2 force) {
			force += gravity * mass[index];

			forces[index][0].vector = force;

			// Spring restoring force
			const auto& real_anchor = index == 0 ? anchor : motion[0].position;
			const auto length_vector = real_anchor - motion[index].position;
			const auto length = glm::length(length_vector);

			const float error = length - NL[index];
			const auto restoring_norm = length_vector / length;
			const auto restoring = restoring_norm * error * k[index];

			if (index == 0) {
				force += restoring - restoring_second;
			}
			if (index == 1) {
				restoring_second = restoring;
				force += restoring;
			}
			// END

			// Air drag force
			const auto drag = 0.5f * 0.47f * (1.225f / 1000) * (M_PIf * radius * radius) * motion[index].velocity;

			force -= drag;
			// END

			// Wind force
			glm::vec2 wind {};

			const auto& button = app->input.pointer.button;
			if (button[0]) {
				wind = glm::vec2(app->input.pointer.cpos) * 2.f;
			}
			// wind *= 500;

			force += wind;
			// END

			forces[index][1].vector = restoring;
			forces[index][2].vector = force;

			return force;
		}

		void integrate(int index, float delta_time, glm::vec2 force) {
			force = calculate_force(index, force);

			// velocity Verlet - doesn't support velocity dependence on acceleration
			/*
			position = position + velocity * delta_time + 0.5f * acceleration * delta_time * delta_time;
			auto new_acceleration = force / mass;
			velocity = velocity + 0.5f * (acceleration + new_acceleration) * delta_time;
			acceleration = new_acceleration;
			*/

			// velocity Verlet - full
			const auto velocity_mid = motion[index].velocity + 0.5f * motion[index].acceleration * delta_time;
			motion[index].position = motion[index].position + velocity_mid * delta_time;
			const auto new_acceleration = force / mass[index];
			motion[index].velocity = velocity_mid + 0.5f * new_acceleration * delta_time;
			motion[index].acceleration = new_acceleration;

			motion[index].velocity_old = motion[index].velocity;
		}

		void collisions(int index, std::vector<Pendulum>& list) {
			/*
			for (Pendulum& p : list) {
				if (&p != this) {
					const float distance_sq = glm::length2(p.position - position);
					const float collision_distance_sq = glm::pow(p.radius + radius, 2);
					if (distance_sq <= collision_distance_sq) {
						velocity = (mass - p.mass) * velocity + 2 * p.mass * p.velocity_old;
						velocity /= mass + p.mass;
					}
				}
			}

			if ((position.x + radius >= width / 2.f) or
				(position.x - radius <= -width / 2.f)) velocity.x *= -1;
			if ((position.y + radius >= height / 2.f) or
				(position.y - radius <= -height / 2.f)) velocity.y *= -1;
			*/
		}

		void draw(Cairo::Context& cr, Cairo::Surface& crs) {
			const glm::vec2 position[2] = {motion[0].position, motion[1].position};

			cr.set_source_rgba(0.8, 0.8, 0.8, 1);
			cr.move_to(anchor.x, anchor.y);
			cr.set_line_width(16.0);
			cr.line_to(position[0].x, position[0].y);
			cr.stroke();

			cr.set_source_rgba(0.8, 0.8, 0.8, 1);
			cr.move_to(position[0].x, position[0].y);
			cr.set_line_width(16.0);
			cr.line_to(position[1].x, position[1].y);
			cr.stroke();

			cr.set_source_rgba(0.8, 0.8, 0.8, 0.75);
			cr.rectangle(anchor.x - rect.x / 2.0, anchor.y - rect.y / 2.0, rect.x, rect.y);
			cr.fill();

			cr.set_source_rgba(color.r, color.g, color.b, 1);
			cr.arc(position[0].x, position[0].y, radius, 0, 2 * M_PI);
			cr.fill();

			cr.set_source_rgba(color.r, color.g, color.b, 1);
			cr.arc(position[1].x, position[1].y, radius, 0, 2 * M_PI);
			cr.fill();

			// for (auto& f : forces) draw_vector(cr, f, 1.5);
		}

		void draw_vector(Cairo::Context& cr, VisualVector const& vis_vector, float scale = 1.f) {
			iassert(false);
			/*
			static constexpr glm::vec2 rect_head(16, 16);

			const auto vector = vis_vector.vector * scale;
			const auto& color = vis_vector.color;
			const auto position_head = position + vector;

			cr.set_source_rgba(color.r, color.g, color.b + 0.25, 1);
			cr.move_to(position.x, position.y);
			cr.set_line_width(8.0);
			cr.line_to(position_head.x, position_head.y);
			cr.stroke();

			cr.set_source_rgba(color.r, color.g, color.b, 1);
			cr.rectangle(position_head.x - rect_head.x / 2.0, position_head.y - rect_head.y / 2.0, rect_head.x, rect_head.y);
			cr.fill();
			*/
		}
	};

	std::vector<Pendulum> pendulum;

private: /* Meat: functions */
	void setup_pre()
	{
		const auto base_gravity = gravity * 20.f;
		const float base_theta[2] = {glm::linearRand(0.f, 2 * M_PIf), glm::linearRand(0.f, 2 * M_PIf)};
		const float base_NL[2] = {196, 128};
		float base_k[2] = {16, 16};
		float base_mass[2] = {1.f, 1.f};
		pendulum.emplace_back(this, glm::vec3(1, 0, 0), base_gravity, base_theta, base_NL, base_k, base_mass);

		base_mass[1] = 2.f;
		pendulum.emplace_back(this, glm::vec3(0, 1, 0), base_gravity, base_theta, base_NL, base_k, base_mass);

		base_mass[1] = 3.f;
		pendulum.emplace_back(this, glm::vec3(0, 0, 1), base_gravity, base_theta, base_NL, base_k, base_mass);
	}

	void setup()
	{
		pendulum[0].setup(glm::vec2(-width / 4.0, 0));
		pendulum[1].setup(glm::vec2(0, 0));
		pendulum[2].setup(glm::vec2(width / 4.0, 0));
	}

	void update(float time, float delta_time)
	{
		glm::vec2 force {};

		for (auto& p : pendulum) {
			for (int i=0; i < 2; i++) {
				p.collisions(i, pendulum);
			}
			for (int i=1; i >= 0; i--) {
				p.integrate(i, delta_time, force);
			}
		}
	}

	void draw(struct buffer* buffer, float time)
	{
		auto& cr = *buffer->cairo.context;
		[[maybe_unused]] auto& crs = *buffer->cairo.surface;

		cr.save();

		cr.translate(width / 2.0, height / 2.0);
		cr.scale(1, -1);

		cr.set_source_rgba(0, 0, 0, 1);
		cr.paint();

		for (auto& p : pendulum) p.draw(cr, crs);

		cr.restore();
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

		buffer->cairo.surface = Cairo::ImageSurface::create((unsigned char*)data, cr_format, width, height, stride);
		buffer->cairo.context = Cairo::Context::create(buffer->cairo.surface);
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

	Cairo::Surface::Format shm_to_cairo_format(uint32_t shm_format)
	{
		Cairo::Surface::Format cr_format;
		switch(shm_format) {
		case WL_SHM_FORMAT_XRGB8888: cr_format = Cairo::Surface::Format::RGB24; break;
		case WL_SHM_FORMAT_ARGB8888: cr_format = Cairo::Surface::Format::ARGB32; break;
		default: iassert(false, "Only certain wl_shm formats are supported");
			break;
		}
		return cr_format;
	}

public: /* section: listeners */
	static void on_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
	{
		// log_event(__func__, "{} v{}", interface, version);

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
		case BTN_LEFT: app->input.pointer.button[0] = state;
			break;
		case BTN_MIDDLE: app->input.pointer.button[1] = state;
			break;
		case BTN_RIGHT: app->input.pointer.button[2] = state;
			break;
		default:
			break;
		}
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
		// log_event(__func__, "{} {}", fd, size);
		iassert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
	}
	static void on_keyboard_enter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys)
	{
		// log_event(__func__);
	}
	static void on_keyboard_leave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
	{
		// log_event(__func__);
	}
	static void on_keyboard_key(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
	{
		// log_event(__func__, "0x{:x}: {}", key, state);
	}
	static void on_keyboard_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
	{
		// log_event(__func__);
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
