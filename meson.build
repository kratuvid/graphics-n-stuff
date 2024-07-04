project('opengl-studies', 'c', 'cpp', default_options: ['c_std=c17', 'cpp_std=c++23'])
if get_option('buildtype').startswith('debug')
 add_project_arguments('-DDEBUG', language: 'cpp')
endif

inc = include_directories('inc')

lib_wl_xdg_shell = static_library('wl_xdg_shell', 'src/wayland/xdg-shell.c', include_directories: inc)
dep_wl_xdg_shell = declare_dependency(link_with: lib_wl_xdg_shell, include_directories: inc)

dep_wl = dependency('wayland-client')
dep_wl_egl = dependency('wayland-egl')
dep_egl = dependency('egl')

dep_spdlog = dependency('spdlog')

deps_shm = [dep_spdlog, dep_wl, dep_wl_xdg_shell]

executable('window', 'src/window/window.cpp', include_directories: inc, dependencies: deps_shm)
executable('window-lamp', 'src/window-lamp/window.cpp', include_directories: inc, dependencies: deps_shm)
executable('heart', 'src/heart/heart.cpp', include_directories: inc, dependencies: deps_shm)
executable('bouncing-ball', 'src/bouncing-ball/bouncing-ball.cpp', include_directories: inc, dependencies: deps_shm)
executable('cloth', 'src/cloth/cloth.cpp', include_directories: inc, dependencies: deps_shm)
