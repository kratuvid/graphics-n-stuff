#include "app.hpp"

class Sap : public App
{
public:
	Sap()
	{
	}

	~Sap()
	{
	}

private:
	void initialize_pre() override
	{
		title = "Sap";
	}

	void setup_pre() override
	{
	}

	void setup() override
	{
	}

	void initialize_variables()
	{
	}

	void update(float delta_time) override
	{
	}

	void draw(Buffer* buffer, float delta_time) override
	{
	}

private:
	void on_create_buffer(Buffer* buffer) override
	{
	}

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
        app.initialize();
        app.run();
    } catch (const App::assertion&) {
        return 1;
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal std::exception: {}", e.what());
        return 2;
    }
}
