// system headers
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <numeric>
#include <print>
#include <queue>
#include <ranges>
#include <semaphore>
#include <source_location>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <xkbcommon/xkbcommon.h>

// external libraries
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cairomm/cairomm.h>

#include <pangomm.h>
#include <pangomm/init.h>

#include <mpc.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_angle.hpp>

// wayland
#include "wayland/xdg-shell.h"
#include <wayland-client.h>
