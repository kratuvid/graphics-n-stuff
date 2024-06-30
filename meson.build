project('opengl-studies', 'c', 'cpp', default_options: ['c_std=c17', 'cpp_std=c++23'])

deps = [dependency('wayland-client'), dependency('wayland-egl'), dependency('egl'), dependency('spdlog')]
inc = include_directories('inc')

executable('window', 'src/window/window.cpp', 'src/wayland/xdg-shell.c', include_directories: inc, dependencies: deps)
executable('window-lamp', 'src/window-lamp/window.cpp', 'src/wayland/xdg-shell.c', include_directories: inc, dependencies: deps)
