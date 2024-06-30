project('opengl-studies', 'c', 'cpp', default_options: ['c_std=c23', 'cpp_std=c++23'])
add_global_arguments('-fmodules-ts', language: 'cpp')

deps = [dependency('wayland-client'), dependency('wayland-egl'), dependency('egl')]
inc = include_directories('inc')

executable('offscreen', 'src/offscreen/offscreen.cpp', 'src/wayland/xdg-shell.c', include_directories: inc, dependencies: deps)
