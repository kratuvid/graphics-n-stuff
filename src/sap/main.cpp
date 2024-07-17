#include "app.hpp"
#include "backend-shm.hpp"
#include "backend-egl.hpp"

class Sap : public App<BackendEGL>
{
public:
	Sap()
	{
	}

	~Sap()
	{
	}

private:
	void setup() override
	{
		title = "Sap";
	}

	void update(float delta_time) override
	{
	}

	void draw(float delta_time) override
	{
		/*
		auto buffer = backend->next_buffer();
		memset(buffer->data, 0xee, buffer->size);
		*/
		glClearColor(1, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		backend->present();
	}

private:
	void on_click(uint32_t button, uint32_t state) override
	{
	}

	void on_key(xkb_keysym_t key, wl_keyboard_key_state state) override
	{
	}
};

int main(int argc, char** argv)
{
	auto stderr_logger = spdlog::stderr_color_mt("stderr_logger");
	spdlog::set_default_logger(stderr_logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    Sap app;
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
