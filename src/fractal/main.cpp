#include "fractal/app.hpp"

using oreal = long double;
using ocomplex = std::complex<oreal>;
using ovec2 = glm::tvec2<oreal>;

template<class TBase, class TInShared, class TInPer, class TOut>
class ThreadManager
{
public:
	enum class CommandType { quit, work };
	struct Command {
		CommandType type;
		const TInShared* in_shared;
		const TInPer* in_per;
		TOut* out;
	};
	static constexpr std::ptrdiff_t semaphore_least_max_value = 64;

private:
	const TBase* app;
	unsigned nthreads;

	std::vector<std::thread> workers;

	std::counting_semaphore<semaphore_least_max_value> command_semaphore {0};
	std::mutex command_mutex;
	std::queue<Command> command_queue;

	std::unique_ptr<std::mutex[]> work_state;
	std::vector<uint64_t> work_done;
	std::atomic_bool stop = false;

public:
	ThreadManager(const TBase* app, unsigned nthreads = std::thread::hardware_concurrency())
		:app(app), nthreads(nthreads)
	{
		iassert(nthreads < semaphore_least_max_value, "Too many threads you got brother. Increase the semaphore's least max value");

		work_state = std::make_unique<std::mutex[]>(nthreads);
		memset(work_state.get(), 0x00, sizeof(std::mutex) * nthreads);
		work_done.resize(nthreads, 0);

		for (unsigned i : std::views::iota(0u, nthreads)) {
			workers.emplace_back(ThreadManager::workplace, i, this);
		}
	}

	auto num_threads() const
	{
		return nthreads;
	}

	void enqueue(Command&& cmd, unsigned count = 1)
	{
		const Command& cmd_cref = cmd;
		command_mutex.lock();
		for (unsigned _ : std::views::iota(0u, count)) {
			command_queue.push(cmd_cref);
		}
		command_mutex.unlock();
	}

	template<class IterBegin, class IterEnd>
	void enqueue(IterBegin begin, IterEnd end)
	{
		command_mutex.lock();
		for (auto it = begin; it != end; it++) {
			command_queue.push(*it);
		}
		command_mutex.unlock();
	}

	template<class Func>
	void enqueue(Func setter)
	{
		command_mutex.lock();
		setter(command_queue);
		command_mutex.unlock();
	}

	void clear()
	{
		command_mutex.lock();
		while (!command_queue.empty())
			command_queue.pop();
		command_mutex.unlock();
	}

	void halt()
	{
		clear();
		stop = true;

		wait_all();

		stop = false;
	}

	void wait_all()
	{
		for (unsigned i : std::views::iota(0u, nthreads))
		{
			work_state[i].lock();
			work_state[i].unlock();
		}
	}

	// WARNING: Don't release if the semaphore counter is at its max capacity
	void release(std::ptrdiff_t update = 1)
	{
		command_semaphore.release(update);
	}

	~ThreadManager()
	{
		Command cmd_quit = {.type = CommandType::quit};

		// Filling in the command queues
		command_mutex.lock();
		while (!command_queue.empty())
			command_queue.pop();
		for (unsigned _ : std::views::iota(0u, nthreads))
			command_queue.push(cmd_quit);
		command_mutex.unlock();

		// Signalling
		stop = true;
		release(nthreads);

		// Waiting
		std::println(stderr, "Waiting for {} threads to quit...", nthreads);
		wait_all();
		for (auto& thread : workers)
		{
			thread.join();
		}

		// Statistics
		const auto total_work_done = std::accumulate(work_done.begin(), work_done.end(), 0);

		std::ostringstream oss;
		oss << "ThreadManager: " << "Σ (work) = " << total_work_done << ": distribution = ";

		for (int i=0; i < nthreads; i++)
		{
			double dist = work_done[i] / double(total_work_done);
			oss << dist * 100 << "%, ";
		}

		spdlog::debug(oss.str());
	}

private:
	static void workplace(unsigned id, ThreadManager* mgr)
	{
		while (true)
		{
			mgr->command_semaphore.acquire();
			mgr->command_mutex.lock();
			if (mgr->command_queue.empty()) {
				mgr->command_mutex.unlock();
				continue;
			}
			Command cmd = mgr->command_queue.front();
			mgr->command_queue.pop();
			mgr->command_mutex.unlock();

			// spdlog::debug("{}: Commanded {}", id, cmd.type == CommandType::quit ? "quit" : "work");

			if (cmd.type == CommandType::quit)
				break;

			// Work
			{
			std::lock_guard<std::mutex> lg(mgr->work_state[id]);

			[[maybe_unused]] const int width = cmd.in_shared->width, height = cmd.in_shared->height;
			const auto at_begin = at(0, cmd.in_per->row_start, width);

			const ovec2 start {
				cmd.in_shared->center.x - cmd.in_shared->range.x / 2,
				cmd.in_shared->center.y - cmd.in_shared->range.y / 2
			};
			const ovec2 delta {
				cmd.in_shared->range.x / width,
				cmd.in_shared->range.y / height
			};

			ocomplex coord;

			auto index = at_begin;
			for (int row = cmd.in_per->row_start; row <= cmd.in_per->row_end; row++)
			{
				coord.imag(start.y + delta.y * (height - row - 1));
				for (int col = 0; col < width; col++, index++)
				{
					coord.real(start.x + delta.x * col);

					unsigned iter = 0; ocomplex z(0, 0);
					for (; iter < cmd.in_shared->max_iterations; iter++)
					{
						auto f_z = z*z + coord;

						if (std::norm(f_z) > 4)
							break;

						z = f_z;
					}

					glm::vec3 color {};

					color.r = 1 + glm::sin((iter / float(cmd.in_shared->max_iterations)) * 2 * M_PIf + std::abs(coord));
					color.r /= 2;
					color.g = 1 + glm::sin(color.r * 2 * M_PIf + M_PIf / 4);
					color.g /= 2;
					color.b = 1 + glm::cos(color.g * 2 * M_PIf);
					color.b /= 2;

					cmd.out->canvas[index] = color_u32(color);
				}

				if (mgr->stop) break;
			}

			mgr->work_done[id]++;
			}
		}
	}

	static uint32_t color_u32(glm::vec3 color)
	{
		color = glm::clamp(color, glm::vec3(0), glm::vec3(1));
		return (uint32_t(color.r * 255)) << 16 | (uint32_t(color.g * 255)) << 8 | uint32_t(color.b * 255);
	}

	static size_t at(int col, int row, int width)
	{
		return (row * width) + col;
	}
};

class Fractal : public App
{
	struct InShared {
		int width, height;
		ovec2 center, range;
		unsigned max_iterations;
	};
	struct InPer {
		int row_start, row_end;
	};
	struct Out {
		std::vector<uint32_t> canvas;
	};

	InShared in_shared;
	std::vector<InPer> in_per;
	Out out;

	ThreadManager<Fractal, InShared, InPer, Out> thread_manager;

	using CommandType = decltype(thread_manager)::CommandType;
	using Command = decltype(thread_manager)::Command;

	ovec2 center, range;
	float max_iterations;
	
public:
	Fractal()
		:thread_manager(this)
	{
		title = "Fractal";
		center = {0, 0};
		range = {4, 4};
		max_iterations = 40;
	}

private:
	void setup_pre() override
	{
		refresh(true);
	}

	void setup() override
	{
		correct_by_aspect();
		refresh(true);
	}

	void correct_by_aspect()
	{
		range.y = range.x * (height / float(width));
	}

	void refresh(bool resize = false)
	{
		if (resize and false) {
			correct_by_aspect();
		}

		thread_manager.halt();

		if (resize) {
			in_shared.width = width;
			in_shared.height = height;
			out.canvas.resize(width * height);
		}
		reassign_dynamic();

		if (resize) {
			distribute();
		}

		pump();
	}

	void reassign_dynamic()
	{
		in_shared.center = center;
		in_shared.range = range;
		in_shared.max_iterations = max_iterations;
	}

	void distribute()
	{
		const int range_size = height / thread_manager.num_threads();
		const int range_size_left = height % thread_manager.num_threads();

		in_per.resize(range_size == 0 ? 1 : thread_manager.num_threads());

		unsigned next_index = 0;
		for (
			int row = 0;
			next_index < in_per.size() and range_size != 0;
			row += range_size, next_index++
		) {
			auto& ip = in_per[next_index];
			ip.row_start = row;
			ip.row_end = row + range_size - 1;
		}

		if (range_size_left != 0)
		{
			auto& ip = in_per[in_per.size()-1];
			ip.row_end = height - 1;
			if (range_size == 0)
			{
				ip.row_start = 0;
			}
		}
	}

	void pump()
	{
		auto command_setter = [&](std::queue<Command>& queue) {
			Command cmd {
				.type = CommandType::work,
				.in_shared = &in_shared,
				.out = &out,
			};
			for (auto& ip : in_per) {
				cmd.in_per = &ip;
				queue.push(cmd);
			}
		};
		thread_manager.enqueue(command_setter);
		thread_manager.release(in_per.size());
	}

	void update(float delta_time) override
	{
		const float mi_rate = 100 * delta_time;

		if (input.keyboard.map[XKB_KEY_i]) {
			max_iterations += mi_rate;
			refresh();
		}
		else if (input.keyboard.map[XKB_KEY_o]) {
			if (max_iterations > mi_rate)
				max_iterations -= mi_rate;
			if (max_iterations < 1)
				max_iterations = 1;
			refresh();
		}
	}

	void draw(Buffer* buffer, float delta_time) override
	{
		memcpy(buffer->shm_data, out.canvas.data(), out.canvas.size() * sizeof(decltype(out.canvas)::value_type));
	}

private:
	void on_create_buffer(Buffer* buffer) override
	{
		if (in_shared.width != width or in_shared.height != height)
			refresh(true);
	}

	void on_click(uint32_t button, uint32_t state) override
	{
		if (state != WL_POINTER_BUTTON_STATE_RELEASED) return;

		if (button == BTN_LEFT)
		{
			const ovec2 start {
				in_shared.center.x - in_shared.range.x / 2,
				in_shared.center.y - in_shared.range.y / 2
			};
			const ovec2 delta {
				in_shared.range.x / width,
				in_shared.range.y / height
			};

			const auto& cpos = input.pointer.pos;
			const ovec2 coord(start.x + delta.x * cpos.x, start.y + delta.y * (height - cpos.y - 1));

			center = coord;
		}
		else if (button == BTN_RIGHT)
		{
			const float factor = 0.8;
			if (!input.keyboard.map[XKB_KEY_Shift_L])
				range *= factor;
			else
				range /= factor;
		}

		refresh();
	}

	void on_key(xkb_keysym_t key, wl_keyboard_key_state state) override
	{
		if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
		{
			switch (key)
			{
			case XKB_KEY_space: {
				refresh();
			} break;

			case XKB_KEY_s: {
				int what;
				std::print(
					"What do you want to change?\n"
					"1. Max iterations ({})\n"
					"2. Range ({}, {})\n"
					"3. Shrink range\n"
					"4. Center ({}, {})\n? ",

					(unsigned) max_iterations,
					range.x, range.y,
					center.x, center.y
				);
				std::cin >> what;

				switch (what)
				{
				case 1:
					std::print("New max iterations: ");
					std::cin >> max_iterations;
					break;

				case 2:
					std::print("New range:\nx: ");
					std::cin >> range.x;
					std::print("y: ");
					std::cin >> range.y;
					break;

				case 3:
					float factor;
					std::print("Shrink range by: ");
					std::cin >> factor;
					range *= factor;
					break;

				case 4:
					std::print("New center:\nx: ");
					std::cin >> center.x;
					std::print("y: ");
					std::cin >> center.y;
					break;

				default:
					break;
				}

				std::println("Set!");
				refresh();
			} break;

			case XKB_KEY_a: {
				correct_by_aspect();
				refresh();
			} break;

			case XKB_KEY_r: {
				center = {0, 0};
				range = {4, 4};
				max_iterations = 40;
				correct_by_aspect();
				refresh();
			} break;

			case XKB_KEY_l: {
				std::println("Center: ({}, {})", center.x, center.y);
				std::println("Range: ({}, {})", range.x, range.y);
				std::println("Max iterations: {}", max_iterations);
			} break;
			}
		}
	}
};

int main()
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    Fractal app;
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
