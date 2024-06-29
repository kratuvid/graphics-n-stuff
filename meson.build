project('opengl-studies', 'c', 'cpp', default_options: ['cpp_std=c++23'])

deps_core = [dependency('wayland-client'), dependency('spdlog')]
inc_core = include_directories('inc')

executable('window', 'src/window.cpp', 'src/wayland/xdg-shell.c', include_directories: inc_core, dependencies: deps_core)
