#define iassert(expr, ...) if (!(expr)) \
		App::_iassert(#expr, std::source_location::current() __VA_OPT__(,) __VA_ARGS__);

class App
{
public: /* section: public interface */
	void initialize()
	{
		initialize_wayland();
	}

	void run()
	{
	}

	~App()
	{
		safe_free(wayland.shm, wl_shm_destroy);
		safe_free(wayland.wm_base, xdg_wm_base_destroy);
		safe_free(wayland.compositor, wl_compositor_destroy);
		safe_free(wayland.registry, wl_registry_destroy);
		safe_free(wayland.display, wl_display_disconnect);
	}

private: /* section: private interface */
	void initialize_wayland()
	{
	}

public: /* section: listeners */
	static void on_registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
	{
	}
	static void on_registry_global_remove(void* data, wl_registry* registry, uint32_t name)
	{
	}
	static constexpr wl_registry_listener registry_listeners {
		.global = on_registry_global, .global_remove = on_registry_global_remove
	};

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

private: /* section: variables */
	struct
	{
		wl_display* display;
		wl_registry* registry;
		wl_compositor* compositor;
		xdg_wm_base* wm_base;
		wl_shm* shm;
	} wayland {};
};
