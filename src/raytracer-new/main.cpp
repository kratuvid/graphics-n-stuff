#include "raytracer-new/app.hpp"

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

		for (unsigned i : std::views::iota(0u, nthreads))
		{
			work_state[i].lock();
			work_state[i].unlock();
		}

		stop = false;
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
		for (unsigned i : std::views::iota(0u, nthreads))
		{
			work_state[i].lock();
			work_state[i].unlock();
		}
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
		srand(id + time(nullptr));

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

			auto index = at_begin;
			for (int row = cmd.in_per->row_start; row <= cmd.in_per->row_end; row++)
			{
				int count = 3000;
				for (int x=0; x < count; x++)
				{
					auto prev_index = index;
					for (int col = 0; col < width; col++, index++)
					{
						uint32_t color = color_u32(cmd.in_per->color);
						cmd.out->canvas[index] = color;
					}
					if (x != count-1)
						index = prev_index;
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

class Raytracer : public App
{
	struct InShared {
		int width, height;
	};
	struct InPer {
		int row_start, row_end;
		glm::vec3 color;
	};
	struct Out {
		std::vector<uint32_t> canvas;
	};

	InShared in_shared;
	std::vector<InPer> in_per;
	Out out;

	ThreadManager<Raytracer, InShared, InPer, Out> thread_manager;

	using CommandType = decltype(thread_manager)::CommandType;
	using Command = decltype(thread_manager)::Command;
	
public:
	Raytracer()
		:thread_manager(this)
	{
		title = "Raytracer";
	}

private:
	void setup_pre() override
	{
		resize();
	}

	void setup() override
	{
		resize();
	}

	void resize()
	{
		thread_manager.halt();
		in_shared.width = width;
		in_shared.height = height;
		out.canvas.resize(width * height);
		distribute();
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
			ip.color = glm::linearRand(glm::vec3(0), glm::vec3(1));
		}

		if (range_size_left != 0)
		{
			auto& ip = in_per[in_per.size()-1];
			ip.row_end = height - 1;
			if (range_size == 0)
			{
				ip.row_start = 0;
				ip.color = glm::vec3(1, 1, 1);
			}
		}

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
	}

	void draw(Buffer* buffer, float delta_time) override
	{
		memcpy(buffer->shm_data, out.canvas.data(), out.canvas.size() * sizeof(decltype(out.canvas)::value_type));
	}

private:
	void on_create_buffer(Buffer* buffer) override
	{
		if (in_shared.width != width or in_shared.height != height)
		{
			resize();
		}
	}

	void on_key(xkb_keysym_t key, wl_keyboard_key_state state) override
	{
		if (key == XKB_KEY_space and state == WL_KEYBOARD_KEY_STATE_RELEASED)
		{
			thread_manager.halt();
			distribute();
		}
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
