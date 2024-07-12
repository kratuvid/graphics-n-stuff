#include "raytracer-new/app.hpp"

#include <mpc.h>

// FEATURE: Increase the number of tasks per thread
// FIXME: Segmentation fault when quitting while the frame is still being processed

static constexpr mpfr_prec_t zprec = 53;

using zreal = mpfr_t;
using zcomplex = mpc_t;
using zvec2 = zreal[2];

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

	mpfr_rnd_t def_rnd;
	mpc_rnd_t def_crnd;

	zreal const_4;

	std::unique_ptr<zcomplex[]> zs, cs;
	std::vector<std::array<zreal,3>> temps_v;
	std::vector<std::array<zcomplex,1>> ctemps_v;

public:
	ThreadManager(const TBase* app, unsigned nthreads = std::thread::hardware_concurrency())
		:app(app), nthreads(nthreads)
	{
		iassert(nthreads < semaphore_least_max_value, "Too many threads you got brother. Increase the semaphore's least max value");

		work_state = std::make_unique<std::mutex[]>(nthreads);
		memset(work_state.get(), 0x00, sizeof(std::mutex) * nthreads);
		work_done.resize(nthreads, 0);

		def_rnd = mpfr_get_default_rounding_mode();
		def_crnd = MPC_RND(def_rnd, def_rnd);

		mpfr_init(const_4);
		mpfr_set_ui(const_4, 4, def_rnd);
		
		zs = std::make_unique<zcomplex[]>(nthreads);
		cs = std::make_unique<zcomplex[]>(nthreads);
		temps_v.resize(nthreads);
		ctemps_v.resize(nthreads);

		for (unsigned i : std::views::iota(0u, nthreads)) {
			mpc_init2(zs[i], zprec);
			mpc_init2(cs[i], zprec);

			for (auto& temp : temps_v[i])
				mpfr_init(temp);
			for (auto& ctemp : ctemps_v[i])
				mpc_init2(ctemp, zprec);

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
			thread.join();

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

		// Free mp variables
		for (unsigned i : std::views::iota(0u, nthreads)) {
			mpc_clear(zs[i]);
			mpc_clear(cs[i]);

			for (auto& temp : temps_v[i])
				mpfr_clear(temp);
			for (auto& ctemp : ctemps_v[i])
				mpc_clear(ctemp);
		}

		mpfr_clear(const_4);
	}

private:
	static void workplace(unsigned id, ThreadManager* mgr)
	{
		mpfr_set_default_prec(zprec);
		mpfr_set_default_rounding_mode(mgr->def_rnd);

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

			auto& z = mgr->zs[id];
			auto& c = mgr->cs[id];
			auto& temps = mgr->temps_v[id];
			[[maybe_unused]] auto& ctemps = mgr->ctemps_v[id];

			const int width = cmd.in_shared->width, height = cmd.in_shared->height;
			const auto at_begin = at(0, cmd.in_per->row_start, width);

			auto index = at_begin;
			for (int row = cmd.in_per->row_start; row <= cmd.in_per->row_end; row++)
			{
				mpfr_mul_ui(c->im, cmd.in_shared->delta[1], height - row - 1, mgr->def_rnd);
				mpfr_add(c->im, cmd.in_shared->start[1], c->im, mgr->def_rnd);

				for (int col = 0; col < width; col++, index++)
				{
					mpfr_mul_ui(c->re, cmd.in_shared->delta[0], col, mgr->def_rnd);
					mpfr_add(c->re, cmd.in_shared->start[0], c->re, mgr->def_rnd);

					mpfr_set_zero(z->re, 1);
					mpfr_set_zero(z->im, 1);

					unsigned iter = 0;
					for (; iter < cmd.in_shared->max_iterations; iter++)
					{
						mpc_sqr(z, z, mgr->def_crnd);
						mpc_add(z, z, c, mgr->def_crnd);

						mpc_norm(temps[0], z, mgr->def_rnd);
						if (mpfr_greater_p(temps[0], mgr->const_4) != 0)
							break;
					}

					glm::vec3 color {};

					mpc_abs(temps[0], c, mgr->def_rnd);
					const float abs = mpfr_get_flt(temps[0], mgr->def_rnd);

					color.r = 1 + glm::sin((iter / float(cmd.in_shared->max_iterations)) * 2 * M_PIf + abs);
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

		mpfr_free_cache();
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
		zvec2 center, range;
		zvec2 start, delta;
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

	mpfr_rnd_t def_rnd;

	zvec2 center, range;
	zvec2 start, delta;
	double max_iterations;

	std::array<zreal, 2> temps;
	
public:
	Fractal()
		:thread_manager(this)
	{
		title = "Fractal-MP";

		def_rnd = mpfr_get_default_rounding_mode();
		mpfr_set_default_prec(zprec);

		mpfr_inits(in_shared.center[0], in_shared.center[1], nullptr);
		mpfr_inits(in_shared.range[0], in_shared.range[1], nullptr);

		mpfr_inits(in_shared.start[0], in_shared.start[1], nullptr);
		mpfr_inits(in_shared.delta[0], in_shared.delta[1], nullptr);

		mpfr_inits(center[0], center[1], nullptr);
		mpfr_inits(range[0], range[1], nullptr);

		mpfr_inits(start[0], start[1], nullptr); // will be calculated
		mpfr_inits(delta[0], delta[1], nullptr); // no need to initialize

		for (auto& temp : temps)
			mpfr_init(temp);

		initialize_variables();
	}

	~Fractal()
	{
		for (auto& temp : temps)
			mpfr_clear(temp);

		mpfr_clears(start[0], start[1], nullptr);
		mpfr_clears(delta[0], delta[1], nullptr);

		mpfr_clears(center[0], center[1], nullptr);
		mpfr_clears(range[0], range[1], nullptr);

		mpfr_clears(in_shared.start[0], in_shared.start[1], nullptr);
		mpfr_clears(in_shared.delta[0], in_shared.delta[1], nullptr);

		mpfr_clears(in_shared.center[0], in_shared.center[1], nullptr);
		mpfr_clears(in_shared.range[0], in_shared.range[1], nullptr);

		mpfr_free_cache();
		mpfr_free_cache2(static_cast<mpfr_free_cache_t>(MPFR_FREE_LOCAL_CACHE | MPFR_FREE_GLOBAL_CACHE));
	}

private:
	void setup_pre() override
	{
		setup();
	}

	void setup() override
	{
		correct_by_aspect();
		recalculate_start();
		recalculate_delta();
		refresh(true);
	}

	void initialize_variables()
	{
		mpfr_set_d(center[0], 0.0, def_rnd);
		mpfr_set_d(center[1], 0.0, def_rnd);

		mpfr_set_d(range[0], 4.0, def_rnd);
		mpfr_set_d(range[1], 4.0, def_rnd);

		max_iterations = 40;
	}

	void correct_by_aspect()
	{
		const auto inv_ar = height / double(width);
		mpfr_mul_d(range[1], range[0], inv_ar, def_rnd);
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
		mpfr_set(in_shared.start[0], start[0], def_rnd);
		mpfr_set(in_shared.start[1], start[1], def_rnd);
		mpfr_set(in_shared.delta[0], delta[0], def_rnd);
		mpfr_set(in_shared.delta[1], delta[1], def_rnd);

		mpfr_set(in_shared.center[0], center[0], def_rnd);
		mpfr_set(in_shared.center[1], center[1], def_rnd);
		mpfr_set(in_shared.range[0], range[0], def_rnd);
		mpfr_set(in_shared.range[1], range[1], def_rnd);
		in_shared.max_iterations = max_iterations;
	}

	// depends on range and center
	void recalculate_start()
	{
		mpfr_div_ui(start[0], range[0], 2, def_rnd);
		mpfr_sub(start[0], center[0], start[0], def_rnd);
		mpfr_div_ui(start[1], range[1], 2, def_rnd);
		mpfr_sub(start[1], center[1], start[1], def_rnd);
	}

	// depends on range, width and height
	void recalculate_delta()
	{
		mpfr_div_ui(delta[0], range[0], width, def_rnd);
		mpfr_div_ui(delta[1], range[1], height, def_rnd);
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
		thread_manager.enqueue([&](std::queue<Command>& queue) {
			Command cmd {
				.type = CommandType::work,
				.in_shared = &in_shared,
				.out = &out,
			};
			for (auto& ip : in_per) {
				cmd.in_per = &ip;
				queue.push(cmd);
			}
		});
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
		{
			recalculate_start();
			recalculate_delta();
			refresh(true);
		}
	}

	void on_click(uint32_t button, uint32_t state) override
	{
		if (state != WL_POINTER_BUTTON_STATE_RELEASED) return;

		if (button == BTN_LEFT)
		{
			const auto& pos = input.pointer.pos;

			mpfr_mul_ui(center[0], delta[0], pos.x, def_rnd);
			mpfr_add(center[0], start[0], center[0], def_rnd);

			mpfr_mul_ui(center[1], delta[1], height - pos.y - 1, def_rnd);
			mpfr_add(center[1], start[1], center[1], def_rnd);

			recalculate_start();
		}
		else if (button == BTN_RIGHT)
		{
			const double factor = 0.8;
			if (!input.keyboard.map[XKB_KEY_Shift_L]) {
				mpfr_mul_d(range[0], range[0], factor, def_rnd);
				mpfr_mul_d(range[1], range[1], factor, def_rnd);
			} else {
				mpfr_div_d(range[0], range[0], factor, def_rnd);
				mpfr_div_d(range[1], range[1], factor, def_rnd);
			}

			recalculate_start();
			recalculate_delta();
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

			/*
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
			*/

			case XKB_KEY_a: {
				correct_by_aspect();
				recalculate_start();
				recalculate_delta();
				refresh();
			} break;

			case XKB_KEY_r: {
				initialize_variables();

				correct_by_aspect();
				recalculate_start();
				recalculate_delta();
				refresh();
			} break;

			case XKB_KEY_l: {
				mpfr_exp_t exp[4];

				auto c_x = mpfr_get_str(nullptr, &exp[0], 10, 0, center[0], def_rnd);
				auto c_y = mpfr_get_str(nullptr, &exp[1], 10, 0, center[1], def_rnd);

				auto r_x = mpfr_get_str(nullptr, &exp[2], 10, 0, range[0], def_rnd);
				auto r_y = mpfr_get_str(nullptr, &exp[3], 10, 0, range[1], def_rnd);

				std::println("Center: ({}:{}, {}:{})", c_x, exp[0], c_y, exp[1]);
				std::println("Range: ({}:{}, {}:{})", r_x, exp[2], r_y, exp[3]);
				std::println("Max iterations: {}", max_iterations);

				mpfr_free_str(r_x);
				mpfr_free_str(r_y);

				mpfr_free_str(c_x);
				mpfr_free_str(c_y);
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
