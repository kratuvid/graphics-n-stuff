#include "fractal-mp/app.hpp"

using zreal = mpfr_t;
using zcomplex = mpc_t;
using zvec2 = zreal[2];
static constexpr mpfr_prec_t zprec = 53;

static constexpr unsigned work_multiplier = 4;

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
	mpfr_rnd_t def_rnd;
	mpc_rnd_t def_crnd;

	unsigned nthreads = 0;

	std::vector<std::thread> workers;
	std::vector<uint64_t> work_cumulative;

	std::counting_semaphore<semaphore_least_max_value> launch_semaphore {0};
	
	std::mutex command_queue_mtx;
	std::queue<Command> command_queue;

	std::mutex left_mtx;
	std::condition_variable left_cv;
	std::atomic_short left = 0;
	std::atomic_bool stop = false;

	zreal const_4;
	std::unique_ptr<zcomplex[]> zs, cs;
	std::vector<std::array<zreal,1>> temps_v;
	std::vector<std::array<zcomplex,1>> ctemps_v;

public:
	ThreadManager(unsigned nthreads = std::thread::hardware_concurrency())
		:nthreads(nthreads)
	{
		iassert(nthreads != 0);
		iassert(work_multiplier != 0);
		iassert(nthreads * work_multiplier < semaphore_least_max_value);
	}

	void initialize()
	{
		work_cumulative.resize(nthreads, 0);

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

	void launch(std::ptrdiff_t update = 1)
	{
		left = update;
		launch_semaphore.release(update);
	}

	void enqueue(Command&& cmd, unsigned count = 1)
	{
		const Command& cmd_cref = cmd;
		std::scoped_lock lg(command_queue_mtx);
		for (unsigned _ : std::views::iota(0u, count)) {
			command_queue.push(cmd_cref);
		}
	}

	template<class IterBegin, class IterEnd>
	void enqueue(IterBegin begin, IterEnd end)
	{
		std::scoped_lock lg(command_queue_mtx);
		for (auto it = begin; it != end; it++) {
			command_queue.push(*it);
		}
	}

	template<class Func>
	void enqueue(Func setter)
	{
		std::scoped_lock lg(command_queue_mtx);
		setter(command_queue);
	}

	void halt()
	{
		clear();
		if (is_done())
			return;
		stop = true;
		wait();
		stop = false;
	}

	void wait()
	{
		std::unique_lock lg(left_mtx);
		left_cv.wait(lg, [this](){ return left == 0; });
	}

	bool is_done() const
	{
		return left == 0;
	}

	auto num_threads() const
	{
		return nthreads;
	}

	void destroy() // DO NOT FORGET TO CALL!
	{
		if (work_cumulative.size() == 0) return;

		Command cmd_quit = {.type = CommandType::quit};

		// Filling in the command queues
		{
			std::scoped_lock lg(command_queue_mtx);
			while (!command_queue.empty())
				command_queue.pop();
			for (unsigned _ : std::views::iota(0u, nthreads))
				command_queue.push(cmd_quit);
		}

		// Signalling
		stop = true;
		launch_semaphore.release(nthreads);

		// Waiting
		std::println(stderr, "Waiting for {} threads to quit...", nthreads);
		for (auto& thread : workers)
			thread.join();

		// Statistics
		const auto total_work_cumulative = std::accumulate(work_cumulative.begin(), work_cumulative.end(), 0);
		const double ideal_dist = 1 / double(nthreads);

		std::ostringstream oss;
		oss << "Multithreading stats:\n  " << "Σ (work) = " << total_work_cumulative << "\n  Δ (distribution) = ";
		oss << std::fixed << std::setprecision(2);
		for (int i=0; i < nthreads; i++)
		{
			const auto dist = work_cumulative[i] / double(total_work_cumulative);
			const auto delta = dist - ideal_dist;
			oss << delta * 100 << "%, ";
		}
		std::println(stderr, "{}", oss.str());

		// Free MP variables
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
	void clear()
	{
		std::scoped_lock lg(command_queue_mtx);
		while (!command_queue.empty())
			command_queue.pop();
	}

private:
	static void workplace(unsigned id, ThreadManager* mgr)
	{
		mpfr_set_default_prec(zprec);
		mpfr_set_default_rounding_mode(mgr->def_rnd);

		auto left_ritual = [&]()
		{
			std::unique_lock lg(mgr->left_mtx);
			iassert(mgr->left > 0)

			mgr->left--;
			if (mgr->left == 0)
				mgr->left_cv.notify_all();
		};

		while (true)
		{
			Command cmd;

			mgr->launch_semaphore.acquire();
			{
				std::scoped_lock sl(mgr->command_queue_mtx);
				if (mgr->command_queue.empty()) {
					left_ritual();
					continue;
				}

				cmd = mgr->command_queue.front();
				mgr->command_queue.pop();
			}

			// spdlog::debug("{}: Commanded {}", id, cmd.type == CommandType::quit ? "quit" : "work");

			// Quit
			if (cmd.type == CommandType::quit)
				break;

			// Work
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

					const float iter_ratio = iter / float(cmd.in_shared->max_iterations);

					mpc_abs(temps[0], c, mgr->def_rnd);
					const float abs_c = mpfr_get_flt(temps[0], mgr->def_rnd);

					/* mpc_abs(temps[0], z, mgr->def_rnd);
					const float abs_z = mpfr_get_flt(temps[0], mgr->def_rnd); */

					color.r = 1 + glm::sin(iter_ratio * 2 * M_PIf + abs_c);
					color.r /= 2;
					color.g = 1 + glm::sin(color.r * 2 * M_PIf + M_PIf / 4);
					color.g /= 2;
					color.b = 1 + glm::cos(color.r * 2 * M_PIf);
					color.b /= 2;

					cmd.out->canvas[index] = color_u32(color);
				}

				if (mgr->stop) break;
			}

			left_ritual();

			mgr->work_cumulative[id]++;
		}

		mpfr_free_cache();
	}

	static uint32_t color_u32(glm::vec3 color)
	{
		color = glm::clamp(color, glm::vec3(0), glm::vec3(1));
		return \
			(uint32_t(255) << 24) |
			(uint32_t(color.r * 255)) << 16 |
			(uint32_t(color.g * 255)) << 8 |
			uint32_t(color.b * 255);
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

	InShared in_shared {};
	std::vector<InPer> in_per;
	Out out;

	ThreadManager<Fractal, InShared, InPer, Out> thread_manager;

	using CommandType = decltype(thread_manager)::CommandType;
	using Command = decltype(thread_manager)::Command;

	enum class ArgType {
		boolean, integer, dreal, string
	};

	struct {
		bool help = false;

		bool render = false;
		int initial_iterations = 0;
		int seconds = 0;
		int fps = 0;

		int center_sway_mode = 0;
		std::string start_center {}, start_range {};
		std::string final_center {};
		double zoom = 0;
		bool no_correct_aspect = false;
		bool silent = false;

		struct {
			std::string_view str;
			std::string_view str_desc;
			ArgType type;
			void* ptr;
		} const desc[12] {
			{"--help", "b: Self explanatory", ArgType::boolean, &help},
			{"--render", "b: Outputs raw frames to stdout once initiated", ArgType::boolean, &render},
			{"--initial-iterations", "i: Initial max iterations", ArgType::integer, &initial_iterations},
			{"--seconds", "i: Total render time", ArgType::integer, &seconds},
			{"--fps", "i: FPS of the render", ArgType::integer, &fps},
			{"--center-sway-mode", "i: How to travel from the starting to the final center. Supported is fixed(1)", ArgType::integer, &center_sway_mode},
			{"--start-center", "s: Center to begin from", ArgType::string, &start_center},
			{"--start-range", "s: Range to begin from", ArgType::string, &start_range},
			{"--final-center", "s: Center to finally reach", ArgType::string, &final_center},
			{"--zoom", "d: Zoom", ArgType::dreal, &zoom},
			{"--no-correct-range", "b: Do not correct the range by the aspect ratio", ArgType::boolean, &no_correct_aspect},
			{"--silent", "b: Don't utter anything while rendering", ArgType::boolean, &silent},
		};
		const size_t desc_size = sizeof(desc) / sizeof(*desc);

		struct {
			zvec2 start_center {}, start_range {};
			zvec2 final_center {};
		} refined;
	} args;

	mpfr_rnd_t def_rnd;
	std::array<zreal, 2> temps {};

	zvec2 center {}, range {};
	zvec2 start {}, delta {};
	double max_iterations;

	// Rendering specific
	std::atomic_bool is_rendering = false;
	unsigned total_frames;
	zvec2 delta_range {};
	std::jthread render_thread;
	
public:
	Fractal()
	{
		def_rnd = mpfr_get_default_rounding_mode();
		mpfr_set_default_prec(zprec);

		alloc_zvec(temps);

		alloc_zvec(args.refined.start_center);
		alloc_zvec(args.refined.start_range);
		alloc_zvec(args.refined.final_center);

		alloc_zvec(in_shared.center);
		alloc_zvec(in_shared.range);
		alloc_zvec(in_shared.start);
		alloc_zvec(in_shared.delta);

		alloc_zvec(center);
		alloc_zvec(range);
		alloc_zvec(start);
		alloc_zvec(delta);

		alloc_zvec(delta_range);
	}

	~Fractal()
	{
		if (is_rendering) {
			render_thread.request_stop();
			render_thread.join();
		}

		thread_manager.destroy();

		free_zvec(delta_range);

		free_zvec(start);
		free_zvec(delta);
		free_zvec(center);
		free_zvec(range);

		free_zvec(in_shared.start);
		free_zvec(in_shared.delta);
		free_zvec(in_shared.center);
		free_zvec(in_shared.range);

		free_zvec(args.refined.start_center);
		free_zvec(args.refined.start_range);
		free_zvec(args.refined.final_center);

		free_zvec(temps);

		mpfr_free_cache();
		mpfr_free_cache2(static_cast<mpfr_free_cache_t>(MPFR_FREE_LOCAL_CACHE | MPFR_FREE_GLOBAL_CACHE));
	}

private:
	void initialize_pre() override
	{
		title = "Fractal-MP";
		initialize_variables();
		thread_manager.initialize();
	}

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
		correct_by_aspect_any(range);
	}

	void correct_by_aspect_any(zvec2& vec)
	{
		const auto inv_ar = height / double(width);
		mpfr_mul_d(vec[1], vec[0], inv_ar, def_rnd);
	}

	void refresh(bool resize = false)
	{
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

		launch();
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
		const int work_size = thread_manager.num_threads() * work_multiplier;

		const int range_size = height / work_size;
		const int range_size_left = height % work_size;

		in_per.resize(range_size == 0 ? 1 : work_size, {});

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
			in_per.resize(in_per.size() + 1, {});
			auto& ip = in_per.back();
			ip.row_start = height - range_size_left;
			ip.row_end = height - 1;
		}
	}

	void launch()
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
		thread_manager.launch(in_per.size());
	}

	void update(float delta_time) override
	{
		if (!is_rendering) {
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
	}

	void draw(Buffer* buffer, float delta_time) override
	{
		memcpy(buffer->shm_data, out.canvas.data(), out.canvas.size() * sizeof(decltype(out.canvas)::value_type));
	}

private:
	static void render_workplace(std::stop_token stop, Fractal* app)
	{
		mpfr_set_default_prec(zprec);
		mpfr_set_default_rounding_mode(app->def_rnd);

		auto& canvas = app->out.canvas;
		const size_t pixel_size = sizeof canvas[0];

		for (unsigned frame=0; frame < app->total_frames; frame++)
		{
			const double frame_ratio = frame / double(app->total_frames-1);
			const double seconds_in = frame_ratio * app->args.seconds;

			if (!app->args.silent)
				std::print(stderr, "\rRendering frame {} aka {:.3f}%, {:.6f}s...  ", frame + 1, frame_ratio * 100, seconds_in);

			app->thread_manager.halt();

			mpfr_mul_d(app->range[0], app->delta_range[0], frame_ratio, app->def_rnd);
			mpfr_add(app->range[0], app->range[0], app->args.refined.start_range[0], app->def_rnd);

			mpfr_mul_d(app->range[1], app->delta_range[1], frame_ratio, app->def_rnd);
			mpfr_add(app->range[1], app->range[1], app->args.refined.start_range[1], app->def_rnd);

			app->recalculate_start();
			app->recalculate_delta();
			app->refresh();

			while (!app->thread_manager.is_done())
			{
				if (stop.stop_requested()) {
					app->thread_manager.halt();
					goto abrupt_exit;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(250));
			}

			std::cout.write(reinterpret_cast<const char*>(canvas.data()), canvas.size() * pixel_size);
			iassert(!std::cout.bad());
		}

		std::println(stderr, "Phew done!  ");

abrupt_exit:
		std::cout.flush();
		mpfr_free_cache();

		app->is_rendering = false;
	}

public:
	void process_args(int argc, char** argv)
	{
		for (int i=1; i < argc; i++)
		{
			const std::string_view arg(argv[i]);

			auto desc = std::find_if(args.desc, args.desc + args.desc_size, [arg](const auto& desc) {
				if (desc.str == arg)
					return true;
				return false;
			});

			if (desc == args.desc + args.desc_size) {
				throw std::runtime_error(std::format("Ignoring unknown argument: {}", arg));
			} else {
				switch (desc->type)
				{
				case ArgType::boolean:
					*static_cast<bool*>(desc->ptr) = true;
					break;

				case ArgType::integer:
					i++;
					if (i >= argc)
						throw std::runtime_error(std::format("Provide the integer {} is expecting", arg));
					*static_cast<int*>(desc->ptr) = std::stoi(argv[i]);
					break;

				case ArgType::dreal:
					i++;
					if (i >= argc)
						throw std::runtime_error(std::format("Provide the double {} is expecting", arg));
					*static_cast<double*>(desc->ptr) = std::stod(argv[i]);
					break;

				case ArgType::string:
					i++;
					if (i >= argc)
						throw std::runtime_error(std::format("Provide the string {} is expecting", arg));
					*static_cast<std::string*>(desc->ptr) = argv[i];
					break;

				default:
					iassert(false, "Case {} left uncovered", static_cast<int>(desc->type));
					break;
				}
			}
		}

		if (args.help)
		{
			std::println(stderr, "Options:");
			for (auto const& desc : args.desc) {
				std::println(stderr, "  {}: {}", desc.str, desc.str_desc);
			}
			throw Fractal::assertion();
		}

		if (args.render)
		{
			iassert(args.initial_iterations > 0);
			iassert(args.seconds > 0);
			iassert(args.fps > 0);
			iassert(args.center_sway_mode > 0);
			iassert(!args.start_center.empty());
			iassert(!args.start_range.empty());
			iassert(!args.final_center.empty());
			iassert(args.zoom > 0);

			set_zvec(args.refined.start_center, args.start_center);
			set_zvec(args.refined.start_range, args.start_range);
			set_zvec(args.refined.final_center, args.final_center);

			/*
			spdlog::debug(
				"Start center: {}, final center: {}, start range: {}",
				get_zvec(args.refined.start_center),
				get_zvec(args.refined.final_center),
				get_zvec(args.refined.start_range));
			*/
		}
	}

private:
	void on_create_buffer(Buffer* buffer) override
	{
		if (in_shared.width != width or in_shared.height != height)
		{
			iassert(!is_rendering, "Resizing while rendering is disallowed");
			recalculate_start();
			recalculate_delta();
			refresh(true);
		}
	}

	void on_click(uint32_t button, uint32_t state) override
	{
		if (is_rendering) return;
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
				if (is_rendering) return;

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
				launch();
			} break;
			*/

			case XKB_KEY_a: {
				if (is_rendering) return;

				correct_by_aspect();
				recalculate_start();
				recalculate_delta();
				refresh();
			} break;

			case XKB_KEY_s: {
				if (is_rendering) return;

				initialize_variables();

				correct_by_aspect();
				recalculate_start();
				recalculate_delta();
				refresh();
			} break;

			case XKB_KEY_r: {
				if (!args.render) {
					std::println(stderr, "Render mode is disabled");
					break;
				}

				if (!is_rendering)
				{
					if (isatty(1))
						throw std::runtime_error("To render standard output must be associated with a file/pipe");
					
					std::println(stderr, "Began rendering...\nParameters:", width, height);
					std::println(stderr,
						"  Dimensions: {}x{}\n"
						"  Initial iterations: {}\n"
						"  Seconds: {}\n"
						"  FPS: {}\n"
						"  Center sway mode: {}\n"
						"  Start center: {}\n"
						"  Final center: {}\n"
						"  Start range: {}\n"
						"  Zoom: {}",
						width, height,
						args.initial_iterations,
						args.seconds,
						args.fps,
						args.center_sway_mode,
						get_zvec(args.refined.start_center),
						get_zvec(args.refined.final_center),
						get_zvec(args.refined.start_range),
						args.zoom
					);

					max_iterations = args.initial_iterations;

					switch (args.center_sway_mode) {
					case 1: // fixed
						mpfr_set(center[0], args.refined.final_center[0], def_rnd);
						mpfr_set(center[1], args.refined.final_center[1], def_rnd);
						break;

					default:
						iassert(false, "Unsupported or invalid sway mode {}", args.center_sway_mode);
						break;
					}

					if (!args.no_correct_aspect) {
						correct_by_aspect_any(args.refined.start_range);
					}

					/* Calculate delta_range */
					// (temps[0], temps[1]) is the final range
					mpfr_div_d(temps[0], args.refined.start_range[0], args.zoom, def_rnd);
					mpfr_div_d(temps[1], args.refined.start_range[1], args.zoom, def_rnd);
					// final step
					mpfr_sub(delta_range[0], temps[0], args.refined.start_range[0], def_rnd);
					mpfr_sub(delta_range[1], temps[1], args.refined.start_range[1], def_rnd);

					total_frames = args.fps * args.seconds;

					std::println(stderr, "Calculated:\n"
						"  Corrected range: {}\n"
						"  Delta range: {}\n"
						"  Total frames: {}",
						get_zvec(args.refined.start_range),
						get_zvec(delta_range),
						total_frames
					);

					render_thread = std::jthread(render_workplace, this);

					is_rendering = true;
				}
			} break;

			case XKB_KEY_l: {
				auto c = get_zvec(center);
				auto r = get_zvec(range);

				std::println(stderr, "Center: {}", c);
				std::println(stderr, "Range: {}", r);
				std::println(stderr, "Max iterations: {}", max_iterations);
			} break;
			}
		}
	}

private: // helpers
	void alloc_zvec(auto& vec)
	{
		for (auto& elem : vec) {
			iassert(!elem->_mpfr_d, "Contains: {}", static_cast<void*>(elem->_mpfr_d));
			mpfr_init(elem);
		}
	}

	void free_zvec(auto& vec)
	{
		for (auto& elem : vec) {
			if (elem->_mpfr_d)
				mpfr_clear(elem);
		}
	}

	void set_zvec(auto& vec, std::string_view str_v)
	{
		const size_t comps = sizeof(vec) / sizeof(*vec);
		iassert(comps == 2, "Currently only vectors of length 2 are supported");

		std::string str(str_v);

		auto comma = str.find(',');
		iassert(comma != std::string_view::npos);

		auto first = str.substr(0, comma);
		auto second = str.substr(comma+1);

		iassert(!first.empty());
		iassert(!second.empty());

		str[comma] = '\0';

		iassert(mpfr_set_str(vec[0], first.data(), 10, def_rnd) == 0); 
		iassert(mpfr_set_str(vec[1], second.data(), 10, def_rnd) == 0);
	}

	std::string get_zvec(const auto& vec)
	{
		const size_t comps = sizeof(vec) / sizeof(*vec);
		
		std::ostringstream oss;

		oss << "(";
		for (size_t i=0; i < comps; i++)
		{
			mpfr_exp_t exp;
			auto str = mpfr_get_str(nullptr, &exp, 10, 0, vec[i], def_rnd);
			oss << str << ':' << exp;
			mpfr_free_str(str);

			if (i != comps-1) oss << " ";
		}
		oss << ")";

		return oss.str();
	}
};

int main(int argc, char** argv)
{
	auto stderr_logger = spdlog::stderr_color_mt("stderr_logger");
	spdlog::set_default_logger(stderr_logger);

    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%^%l%$ +%o] %v");

    Fractal app;
    try {
		app.process_args(argc, argv);
        app.initialize();
        app.run();
    } catch (const App::assertion&) {
        return 1;
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal std::exception: {}", e.what());
        return 2;
    }
}
