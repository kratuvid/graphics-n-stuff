#include "raytracer-new/app.hpp"

class Raytracer : public App
{
public:
	Raytracer()
	{
		title = "Raytracer";
	}

private:
	void update(float delta_time) override
	{
	}

	void draw(Buffer* buffer, float delta_time) override
	{
	}
};

int main()
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    Raytracer app;
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
