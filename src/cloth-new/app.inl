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
	float elapsed_time = 0, delta_update_time = 0, delta_draw_time = 0;

	std::chrono::high_resolution_clock::time_point tp_begin, tp_very_last;
	std::chrono::nanoseconds duration_pause {0};
	bool last_window_activated = false;

	static constexpr unsigned substeps = 32;
	static constexpr std::string_view title {"Cloth New"};
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
							app->delta_draw_time * 1e3f
					).c_str());
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
			elapsed_time += sub_dt;
		}
		auto _tp_end = std::chrono::high_resolution_clock::now();
		delta_update_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		_tp_begin = std::chrono::high_resolution_clock::now();
		draw(buffer);
		_tp_end = std::chrono::high_resolution_clock::now();
		delta_draw_time = std::chrono::duration_cast<std::chrono::nanoseconds>(_tp_end - _tp_begin).count() / 1e9f;

		wl_surface_attach(window.surface, buffer->buffer, 0, 0);
		wl_surface_damage_buffer(window.surface, 0, 0, width, height);
	}

private: /* Meat: variables */
	glm::vec2 gravity = glm::vec2(0, -9.8) * 1.f;

	struct Cloth {
		App* app;
		float width, height;

		glm::vec3 color;
		float radius = 8.f;

		glm::vec2 extent[2];
		int grid[2]; // column, rows
		int grid_size;
		std::vector<int> anchors;

		glm::vec2 gravity;
		float mass, NL[2], k;
		float Cdis;

		struct Motion {
			glm::vec2 position, velocity, acceleration;
			// glm::vec2 position, position_old;
		};
		std::vector<Motion> motion;
		std::vector<glm::vec2> forces;

		Cloth(App* app, glm::vec3 const& color, glm::vec2 const& gravity, float mass, const int grid[2], std::vector<int> const& anchors, float k, float Cdis) {
			this->app = app;

			this->color = color;
			this->gravity = gravity;
			this->mass = mass;

			memcpy(this->grid, grid, sizeof(this->grid));
			this->anchors = anchors;

			this->k = k;
			this->Cdis = Cdis;

			grid_size = grid[0] * grid[1];

			motion.resize(grid_size);
			forces.resize(grid_size);
		}

		int get_index(int j, int i) {
			return i * grid[0] + j;
		}

		void setup(const glm::vec2 extent[2]) {
			width = app->width;
			height = app->height;

			memcpy(this->extent, extent, sizeof(this->extent));

			const float w = extent[1].x - extent[0].x, h = extent[0].y - extent[1].y;
			const float dx = w / grid[0], dy = h / grid[1];

			NL[0] = dx; NL[1] = dy;

			for (int j=0; j < grid[0]; j++)
			{
				for (int i=0; i < grid[1]; i++)
				{
					const int index = get_index(j, i);
					auto& m = motion[index];

					const float x = extent[0].x + dx * j, y = extent[0].y - dy * i;

					m.position.x = x; m.position.y = y;
					/*
					m.position_old = m.position;
					*/
					m.velocity = glm::vec2(0, 0);
					m.acceleration = glm::vec2(0, 0);
				}
			}
		}

		void calculate_forces(glm::vec2 const& ext_force) {
			const auto& button = app->input.pointer.button;

			memset(&forces[0], 0x00, forces.size() * sizeof(decltype(forces)::value_type));

			for (int j=0; j < grid[0]; j++)
			{
				for (int i=0; i < grid[1]; i++)
				{
					const int index = get_index(j, i);
					const bool is_anchor = std::find(anchors.begin(), anchors.end(), index) != anchors.end();

					if (is_anchor)
						continue;

					auto& m = motion[index];

					glm::vec2 force {};

					force += ext_force;
					force += gravity * mass;

					if (button[0])
						force += glm::vec2(app->input.pointer.cpos) * 0.01f;

					force += -Cdis * m.velocity;

					const int indices[4] = { // left, right, top, bottom
						j == 0 ? -1 : index - 1,
						j == grid[0] - 1 ? -1 : index + 1,
						i == 0 ? -1 : index - grid[0],
						i == grid[1] - 1 ? -1 : index + grid[0]
					};
					for (int s=0; s < 4; s++) {
						const auto neighbour = indices[s];
						const auto this_NL = s < 2 ? NL[0] : NL[1];

						if (neighbour != -1) {
							const auto& m_next = motion[neighbour];

							const auto vector = m_next.position - m.position;
							const auto magnitude_sq = glm::length2(vector);

							if (magnitude_sq != 0.f) {
								const auto magnitude = glm::sqrt(magnitude_sq);
								const auto vector_norm = vector / magnitude;

								const float change = magnitude - this_NL;
								const auto restoring = vector_norm * change * k;

								force += restoring;
							}
						}
					}
					
					forces[index] = force;
				}
			}
		}

		void update(float delta_time, glm::vec2 const& ext_force) {
			calculate_forces(ext_force);

			for (int j=0; j < grid[0]; j++)
			{
				for (int i=0; i < grid[1]; i++)
				{
					const int index = get_index(j, i);
					auto& m = motion[index];

					const auto& force = forces[index];

					/* velocity Verlet - full */
					const auto velocity_mid = m.velocity + 0.5f * m.acceleration * delta_time;
					m.position = m.position + velocity_mid * delta_time;
					const auto new_acceleration = force / mass;
					m.velocity = velocity_mid + 0.5f * new_acceleration * delta_time;
					m.acceleration = new_acceleration;

					/* velocity Verlet - short */
					/*
					m.position = m.position + m.velocity * delta_time + 0.5f * m.acceleration * delta_time * delta_time;
					const auto new_acceleration = force / mass;
					m.velocity = m.velocity + 0.5f * (m.acceleration + new_acceleration) * delta_time;
					m.acceleration = new_acceleration;
					*/

					/* standard Verlet */
					/*
					const auto acceleration = force / mass;
					const auto new_position = 2.f * m.position - m.position_old + acceleration * delta_time * delta_time;
					m.position_old = m.position;
					m.position = new_position;
					*/
				}
			}
		}

		void draw(Cairo::Context& cr, Cairo::Surface& crs) {
			cr.set_source_rgb(0.8, 0.8, 0.8);
			cr.set_line_width(radius / 3);

			for (int i=0; i < grid[1]; i++) // row
			{
				for (int j=0; j < grid[0] - 1; j++) // column
				{
					const int index = get_index(j, i);
					const auto& m = motion[index];
					const auto& m_next = motion[get_index(j+1, i)];

					cr.move_to(m.position.x, m.position.y);
					cr.line_to(m_next.position.x, m_next.position.y);
					cr.stroke();
				}
			}

			for (int i=0; i < grid[1] - 1; i++) // row
			{
				for (int j=0; j < grid[0]; j++) // column
				{
					const int index = get_index(j, i);
					const auto& m = motion[index];
					const auto& m_next = motion[get_index(j, i+1)];

					cr.move_to(m.position.x, m.position.y);
					cr.line_to(m_next.position.x, m_next.position.y);
					cr.stroke();
				}
			}

			const float radius_rect = radius * 1.1f;
			const float radius_half = radius_rect / 2;

			cr.set_source_rgb(color.r, color.g, color.b);

			for (int j=0; j < grid[0] and false; j++)
			{
				for (int i=0; i < grid[1]; i++)
				{
					const int index = get_index(j, i);
					const auto& position = motion[index].position;

					// cr.arc(position.x, position.y, radius, 0, 2 * M_PI);
					// cr.fill();

					cr.rectangle(position.x - radius_half, position.y - radius_half, radius_rect, radius_rect);
					cr.fill();
				}
			}
		}
	};

	std::vector<Cloth> cloth;

private: /* Meat: functions */
	void setup_pre()
	{
		const auto base_gravity = gravity * 64.f;
		float mass = 1 / 1e3f;
		float k = 7;
		float Cdis = 0.03f;
		int grid[2] = {70, 49};

		auto get_index = [&](int j, int i) -> int {
			return i * grid[0] + j;
		};

		std::vector<int> anchors;
        // std::vector<int> anchors {0, grid[0]-1};
		// std::vector<int> anchors {grid[0] / 2};
		// std::vector<int> anchors {0, grid[0]/2, grid[0]-1};
		/*
		std::vector<int> anchors {
			0, get_index(grid[0]-1, 0),
			get_index(0, grid[1] / 2), get_index(grid[0]-1, grid[1] / 2),
			get_index(grid[0] / 2, 0), get_index(grid[0] / 2, grid[1]-1),
			get_index(0, grid[1]-1), get_index(grid[0]-1, grid[1]-1)
			};
		*/
		/*
		for (int j=0; j < grid[0]; j++) {
			anchors.push_back(get_index(j, 0));
			anchors.push_back(get_index(j, grid[1]-1));
		}
		for (int i=0; i < grid[1]; i++) {
			anchors.push_back(get_index(0, i));
			anchors.push_back(get_index(grid[0]-1, i));
		}
		*/
		// anchors.push_back(get_index(grid[0]/2, grid[1]/2));
		/*
		for (int j=0; j < grid[0]; j++) {
			anchors.push_back(get_index(j, grid[1]/4));
		}
		*/
		for (int j=0; j < grid[0]; j++) {
			if (j % 8 == 0)
				anchors.push_back(get_index(j, 0));
		}

		cloth.emplace_back(this, glm::vec3(1, 0, 0), base_gravity, mass, grid, anchors, k, Cdis);
	}

	void setup()
	{
		glm::vec2 extent[2] = {
			{-width / 3.5f, height / 2.15f},
			{width / 3.5f, -height / 2.75f}
		};
		cloth[0].setup(extent);
	}

	void update(float delta_time)
	{
		glm::vec2 force {};

		for (auto& c : cloth) c.update(delta_time, force);
	}

	void draw(struct buffer* buffer)
	{
		auto& cr = *buffer->cairo.context;
		[[maybe_unused]] auto& crs = *buffer->cairo.surface;

		cr.save();

		cr.translate(width / 2.0, height / 2.0);
		cr.scale(1, -1);

		cr.set_source_rgba(0, 0, 0, 1);
		cr.paint();

		for (auto& c : cloth) c.draw(cr, crs);

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
