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

		renderer = std::jthread([this](std::stop_token stoken) {
			this->pre_render();
			this->render(stoken);
		});

		setup_render();
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

private:
	struct Ray {
		glm::vec3 orig, dir;

		auto at(float t) const {
			return orig + t * dir;
		}
	};

	struct Shape {
		glm::vec3 color;

		Shape(glm::vec3 color)
			:color(color)
		{}
		virtual ~Shape() = default;
	};

	enum class ShapeType {Circle, Sphere};

	struct Sphere : public Shape {
		glm::vec3 pos;
		float rad;

		Sphere(glm::vec3 color, glm::vec3 pos, float rad)
			:Shape(color), pos(pos), rad(rad)
		{}
	};

	struct Circle : public Shape {
		glm::vec3 normal;
		glm::vec3 pos;
		float rad;

		Circle(glm::vec3 color, glm::vec3 normal, glm::vec3 pos, float rad)
			:Shape(color), normal(normal), pos(pos), rad(rad)
		{}
	};

	std::vector<std::unique_ptr<Circle>> circles;
	std::vector<std::unique_ptr<Sphere>> spheres;
	std::vector<std::pair<ShapeType, const Shape*>> scene;

	std::jthread renderer;

	glm::vec3 cam_pos, cam_dir, cam_up, cam_right;
	float focal_length;

	float aspect_ratio;
	glm::vec3 vp_pos, vp_topleft;
	glm::vec2 vp_size, vp_delta;

	void setup_render()
	{
 		circles.emplace_back(std::make_unique<Circle>(glm::vec3(1, 1, 1), glm::vec3(0, 1, 0), glm::vec3(0, 0, 0), 4));
		spheres.emplace_back(std::make_unique<Sphere>(glm::vec3(1, 0, 0), glm::vec3(0, 0.5, 0), 0.5));
		spheres.emplace_back(std::make_unique<Sphere>(glm::vec3(0, 1, 0), glm::vec3(1, 0.5, 0), 0.5));
		spheres.emplace_back(std::make_unique<Sphere>(glm::vec3(0, 0, 1), glm::vec3(-1, 0.5, 0), 0.5));

		for (const auto& circle : circles)
			scene.emplace_back(std::make_pair(ShapeType::Circle, circle.get()));
		for (const auto& sphere : spheres)
			scene.emplace_back(std::make_pair(ShapeType::Sphere, sphere.get()));

		cam_pos = {2, 2, -2};
		focal_length = 1.0f;
	}

	void update(float delta_time) override
	{
		// cam_pos = {4 + glm::abs(glm::sin(elapsed_time)) * 4, 1, -1};
		cam_dir = glm::normalize(glm::vec3(0, 0, 0) - cam_pos);
		auto t = glm::normalize(glm::cross(cam_dir, glm::vec3(0, 1, 0)));
		cam_up = glm::normalize(glm::cross(cam_dir, t));
		cam_right = glm::normalize(glm::cross(cam_dir, cam_up));

		aspect_ratio = float(iwidth) / iheight;
		vp_pos = cam_pos + focal_length * cam_dir;
		vp_size = glm::vec2(2 * aspect_ratio, 2);
		vp_topleft = vp_pos + (-cam_right * vp_size.x * 0.5f + cam_up * vp_size.y * 0.5f);
		vp_delta = vp_size / glm::vec2(iwidth, iheight);

		/*
		spheres[0].pos.x = glm::sin(elapsed_time);
		spheres[1].pos.y = glm::sin(elapsed_time);
		spheres[2].pos.z = glm::sin(elapsed_time);
		*/
		
		const float factor = delta_time * 2;
		if (input.keyboard.map_utf['1']) cam_pos.x -= factor;
		if (input.keyboard.map_utf['2']) cam_pos.x += factor;
		if (input.keyboard.map_utf['3']) cam_pos.y -= factor;
		if (input.keyboard.map_utf['4']) cam_pos.y += factor;
		if (input.keyboard.map_utf['5']) cam_pos.z -= factor;
		if (input.keyboard.map_utf['6']) cam_pos.z += factor;
		if (input.keyboard.map_utf['7']) focal_length -= factor;
		if (input.keyboard.map_utf['8']) focal_length += factor;
	}
	
	void render(std::stop_token stoken)
	{
		while (!stoken.stop_requested())
		{
			size_t location = 0;
			for (int j = 0; j < iheight; j++)
			{
				for (int i = 0; i < iwidth; i++, location++)
				{
					Ray prim_ray {.orig = cam_pos};
					{
						const auto pixel_pos = vp_topleft +
							(cam_right * vp_delta.x * (i + 0.5f)
							+ -cam_up * vp_delta.y * (j + 0.5f));
						prim_ray.dir = glm::normalize(pixel_pos - cam_pos);
					}

					const Shape* object = nullptr;
					glm::vec3 point, normal;

					auto min_dist = std::numeric_limits<float>::max();

					for (auto [type, obj] : scene)
					{
						glm::vec3 _point, _normal;
						if (hit(prim_ray, type, obj, _point, _normal)) {
							const float dist_sq = glm::distance2(cam_pos, _point);
							if (dist_sq < min_dist) {
								object = obj;
								point = _point;
								normal = _normal;
								min_dist = dist_sq;
							}
						}
					}

					glm::vec3* pixel = reinterpret_cast<glm::vec3*>(image.data() + location * 3);
					// if (object) *pixel = glm::clamp(normal, glm::vec3(0), glm::vec3(1));
					if (object) *pixel = normal * 0.5f + 0.5f;
					else *pixel = glm::vec3(0, 0, 0);
				}
			}

			// std::this_thread::sleep_for(std::chrono::milliseconds(25));
		}
	}

	bool hit(Ray const& r, ShapeType type, const Shape* obj, glm::vec3& point, glm::vec3& normal)
	{
		// use constexpr to improve?
		switch (type)
		{
		case ShapeType::Circle:
			return hit(r, *dynamic_cast<const Circle*>(obj), point, normal);
			break;
		case ShapeType::Sphere:
			return hit(r, *dynamic_cast<const Sphere*>(obj), point, normal);
			break;
		default:
			iassert(false, "{}", (int)type);
		}
	}

	bool hit(Ray const& r, Sphere const& s, glm::vec3& point, glm::vec3& normal)
	{
		const float A = glm::dot(r.dir, r.dir);
		const float B = 2 * (
			r.dir.x * (r.orig.x - s.pos.x)
			+ r.dir.y * (r.orig.y - s.pos.y)
			+ r.dir.z * (r.orig.z - s.pos.z));
		const float C = -(s.rad * s.rad)
			+ glm::pow(r.orig.x - s.pos.x, 2)
			+ glm::pow(r.orig.y - s.pos.y, 2)
			+ glm::pow(r.orig.z - s.pos.z, 2);

		const float det = B*B - 4*A*C;
		if (det < 0)
			return false;
		const bool one = std::fpclassify(det) == FP_ZERO;

		const float t0 = (-B + glm::sqrt(det)) / (2 * A);
		const float t1 = one ? t0 : (-B - glm::sqrt(det)) / (2 * A);
		const float tnear = t0 < t1 ? (t0 <= 0 ? t1 : t0) : t1;
		if (tnear <= 0)
			return false;

		point = r.at(tnear);
		normal = (point - s.pos) / s.rad;

		return true;
	}

	bool hit(Ray const& r, Circle const& c, glm::vec3& point, glm::vec3& normal)
	{
		const float t = glm::dot(c.pos - r.orig, c.normal) / glm::dot(r.dir, c.normal);
		if (t <= 0)
			return false;

		point = r.at(t);

		const auto center_to_point = point - c.pos;
		if (glm::dot(center_to_point, center_to_point) > c.rad * c.rad)
			return false;

		normal = glm::dot(r.dir, c.normal) >= 0 ? -c.normal : c.normal;

		return true;
	}

	void pre_render() {}

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
