#include "pch.hpp"
#include "app.hpp"
#include "backend-shm.hpp"

class PS11 : public App<BackendSHM>
{
public:
	PS11()
	{
		title = "PS11";
	}

private:
	void setup() override
	{
	}

	void update(float delta_time) override
	{
	}

	void draw(float delta_time) override
	{
		auto buf = backend->next_buffer();
		backend->present(buf);
	}

private:
	void on_click(uint32_t button, wl_pointer_button_state state) override
	{
	}

	void on_key(xkb_keysym_t key, wl_keyboard_key_state state) override
	{
	}

	void on_configure(bool new_dimensions, wl_array* array) override
	{
	}
};

int main(int argc, char** argv)
{
	auto stderr_logger = spdlog::stderr_color_mt("stderr_logger");
	spdlog::set_default_logger(stderr_logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    PS11 app;
    try {
        app.init();
        app.run();
    } catch (const assertion&) {
        return 1;
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal std::exception: {}", e.what());
        return 2;
    }
}
