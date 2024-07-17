#include "app.hpp"
#include "backend-shm.hpp"
#include "backend-egl.hpp"

class Sap : public App<BackendEGL>
{
	GLuint vs, fs, shader;
	GLuint vbo, vao;
	GLuint texture;
	GLuint pbo;
	GLsync fence = nullptr;

	std::vector<float> image;
	int iwidth, iheight;

	std::jthread renderer;
	std::atomic_bool is_uploaded = true;
	
public:
	Sap()
	{
		iwidth = width = 1910; iheight = height = 1010;
	}

private:
	void setup() override
	{
		title = "Sap";

		const GLchar* vs_source = R"(
			#version 460 core

			in vec2 in_vertex;
			in vec2 in_image_coords;
			out vec2 image_coords;

			void main() {
				gl_Position = vec4(in_vertex, 0, 1);
				image_coords = in_image_coords;
			}
		)";

		const GLchar* fs_source = R"(
			#version 460 core

			uniform sampler2D image;

			in vec2 image_coords;
			out vec4 color;

			void main() {
				color = texture(image, image_coords);
			}
		)";

		vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &vs_source, nullptr);
		glCompileShader(vs);

		fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &fs_source, nullptr);
		glCompileShader(fs);

		shader = glCreateProgram();
		glAttachShader(shader, vs);
		glAttachShader(shader, fs);
		glLinkProgram(shader);

		const float vertices[] {
			-1, -1,
			-1,  1,
			 1, -1,
			 1,  1
		};

		const float image_coords[] {
			0, 0,
			0, 1,
			1, 0,
			1, 1
		};

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices) + sizeof(image_coords), nullptr, GL_STATIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
		glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), sizeof(image_coords), image_coords);

		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, (const void*)sizeof(vertices));

		image.resize(3 * iwidth * iheight);
		glGenTextures(1, &texture);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, iwidth, iheight, 0, GL_RGB, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

		glUseProgram(shader);
		glUniform1i(glGetUniformLocation(shader, "image"), 0);

		glGenBuffers(1, &pbo);

		glClearColor(0, 0, 0, 1);
		glDisable(GL_DEPTH_TEST);

		renderer = std::jthread(render_proxy, this);
	}

	void update(float delta_time) override
	{
	}

	void draw(float delta_time) override
	{
		if (is_uploaded)
		{
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, image.size() * sizeof(float), image.data(), GL_STREAM_DRAW);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, iwidth, iheight, GL_RGB, GL_FLOAT, (const void*)0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

			if (fence) glDeleteSync(fence);
			fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

			is_uploaded = false;
		}
		else
		{
			auto ret = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1 * 1e6);

			if (ret == GL_ALREADY_SIGNALED or ret == GL_CONDITION_SATISFIED)
			{
				glUseProgram(shader);
				glBindVertexArray(vao);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

				backend->present();

				is_uploaded = true;
			}
		}

		static bool first = true;
		if (first) [[unlikely]] { backend->present(); first = false; }
	}

	void render(std::stop_token& stoken)
	{
		while (!stoken.stop_requested())
		{
			for (int i = 0; i < iwidth; i++)
			{
				for (int j = 0; j < iheight; j++)
				{
					const size_t location = 3 * (j * iwidth + i);
					float* pixel = image.data() + location;
					*pixel = i / float(iwidth);
					*(pixel+1) = j / float(iheight);
					*(pixel+2) = (glm::sin(elapsed_time) + 1) * 0.5f;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(25));
		}
	}

	static void render_proxy(std::stop_token stoken, Sap* app)
	{
		app->render(stoken);
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
		glViewport(0, 0, width, height);
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
