#pragma once

#include "pch.hpp"

#ifndef DISABLE_IASSERT
#define iassert_base(type, expr, ...)	\
    if (!(expr)) throw type::make(#expr, std::source_location::current() __VA_OPT__(, ) __VA_ARGS__)
#else
#define iassert_base(type, expr, ...)	\
    if (!(expr))
#endif

static void safe_free(auto& pointer, auto freer)
{
	if (pointer) {
		freer(pointer);
		pointer = nullptr;
	}
}

template<class Derived>
class assertion_base : public std::exception
{
public:
    template <class... Args>
	static auto make(const char* expr, std::source_location where, Args&&... args)
	{
        std::print(stderr, "{}:{}: {}: Assertion `{}` failed",
				where.file_name(), where.line(), where.function_name(),
				expr);

        if constexpr (sizeof...(args) > 0)
            _assert_args(args...);

        std::println(stderr, "");

		return Derived();
	}

    template <class Format, class... Args>
    static void _assert_args(Format&& format, Args&&... args)
    {
        std::print(stderr, ": ");
        std::vprint_unicode(stderr, format, std::make_format_args(args...));
    }
};
