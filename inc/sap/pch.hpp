#pragma once

// system headers
#include <chrono>
#include <exception>
#include <iostream>
#include <print>
#include <ranges>
#include <source_location>
#include <string_view>
#include <unordered_map>
#include <type_traits>
#include <thread>
#include <typeindex>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <xkbcommon/xkbcommon.h>

// external libraries
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// glm
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/random.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/vector_angle.hpp>

// GLAD
#include <glad/glad.h>

// wayland
#include <wayland-client.h>
#include <wayland-egl.h>
#include "wayland/xdg-shell.h"

// egl
#include <EGL/egl.h>
#include <EGL/eglext.h>
