import <print>;
import <string_view>;
import <source_location>;

#include <wayland-client.h>
#include "wayland/xdg-shell.h"

#include "app.inl"

int main()
{
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
