#include <print>
#include <string_view>
#include <source_location>
#include <exception>

#include <spdlog/spdlog.h>

#include <wayland-client.h>
#include "wayland/xdg-shell.h"

#include "app.inl"

int main()
{
	spdlog::set_level(spdlog::level::debug);
	spdlog::set_pattern("[%o] [%^%l%$] %v");

	App app;
	try
	{
		app.initialize();
		app.run();
	}
	catch (const App::assertion&)
	{
		return 1;
	}
	catch (const std::exception& e)
	{
		std::println(stderr, "Fatal std::exception: {}", e.what());
		return 2;
	}
}
