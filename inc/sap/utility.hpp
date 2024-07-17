#pragma once

#include "pch.hpp"

#ifndef DISABLE_IASSERT
#define iassert(expr, ...)	\
    if (!(expr)) throw assertion(#expr, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)
#else
#define iassert(expr, ...)	\
    if (!(expr))
#endif

struct Wayland
{
	wl_display* display;
	wl_registry* registry;

	struct {
		wl_compositor* compositor;
		xdg_wm_base* wm_base;
		wl_shm* shm;
		wl_seat* seat;
	} global;

	struct {
		wl_pointer* pointer;
		wl_keyboard* keyboard;
	} seat;

	struct
	{
		wl_surface* surface;
		xdg_surface* xsurface;
		xdg_toplevel* xtoplevel;
		wl_callback* redraw_callback;
	} window {};
};

void safe_free(auto& pointer, auto freer)
{
	if (pointer) {
		freer(pointer);
		pointer = nullptr;
	}
}

template <class Format, class... Args>
void _log_event_args(Format&& format, Args&&... args)
{
	std::print(stderr, ": ");
	std::vprint_unicode(stderr, format, std::make_format_args(args...));
}
template <class... Args>
void log_event(const char* function, Args&&... args)
{
	std::print(stderr, "event {}", function);
	if constexpr (sizeof...(args) > 0)
		_log_event_args(args...);
	std::println("");
}

class assertion : public std::exception
{
public:
    template <class... Args>
	assertion(const char* expr, std::source_location where, Args&&... args)
	{
        std::print(stderr, "{}:{}: {}: Assertion `{}` failed",
				where.file_name(), where.line(), where.function_name(),
				expr);

        if constexpr (sizeof...(args) > 0)
            _assert_args(args...);

        std::println(stderr, "");
	}

private:
    template <class Format, class... Args>
    void _assert_args(Format&& format, Args&&... args)
    {
        std::print(stderr, ": ");
        std::vprint_unicode(stderr, format, std::make_format_args(args...));
    }
};
