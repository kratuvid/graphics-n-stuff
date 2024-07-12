// system headers
#include <chrono>
#include <exception>
#include <print>
#include <queue>
#include <source_location>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <ranges>
#include <semaphore>
#include <numeric>
#include <complex>
#include <iostream>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <xkbcommon/xkbcommon.h>

// external libraries
#include <spdlog/spdlog.h>

#include <cairomm/cairomm.h>

#include <pangomm.h>
#include <pangomm/init.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_angle.hpp>

// wayland
#include "wayland/xdg-shell.h"
#include <wayland-client.h>
#include <wayland-egl.h>
