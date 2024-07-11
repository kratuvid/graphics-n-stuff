project('opengl-studies', 'c', 'cpp', default_options: ['c_std=c23', 'cpp_std=c++23', 'b_lto=true'])
if get_option('buildtype').startswith('debug')
  add_project_arguments('-DDEBUG', language: 'cpp')
else
  add_project_arguments('-Ofast', language: 'cpp')
  add_project_link_arguments('-Ofast', language: 'cpp')
endif

inc = include_directories('inc')

libs = {
  'wl-xdg-shell': static_library('wl_xdg_shell', 'src/wayland/xdg-shell.c', include_directories: inc)
}

deps = {
  # internal
  'wl-xdg-shell': declare_dependency(link_with: libs['wl-xdg-shell'], include_directories: inc),

  # external
  'wl': dependency('wayland-client'),
  'wl-egl': dependency('wayland-egl'),
  'egl': dependency('egl'),
  'xkbcommon': dependency('xkbcommon'),
  'cairomm': dependency('cairomm-1.16'),
  'pangomm': dependency('pangomm-2.48'),
  'spdlog': dependency('spdlog')
}

deps_shm = [deps['spdlog'], deps['wl'], deps['wl-xdg-shell'], deps['xkbcommon'], deps['cairomm'], deps['pangomm']]

common_pch = 'inc/pch/pch.hpp'

executable('window', 'src/window/window.cpp', include_directories: inc, dependencies: deps_shm)
executable('window-lamp', 'src/window-lamp/window.cpp', include_directories: inc, dependencies: deps_shm)
executable('heart', 'src/heart/heart.cpp', include_directories: inc, dependencies: deps_shm)
executable('bouncing-ball', 'src/bouncing-ball/bouncing-ball.cpp', include_directories: inc, dependencies: deps_shm)
executable('cloth', 'src/cloth/cloth.cpp', include_directories: inc, dependencies: deps_shm)
executable('pendulum', 'src/pendulum/main.cpp', include_directories: inc, dependencies: deps_shm)
executable('two-pendulum', 'src/two-pendulum/main.cpp', include_directories: inc, dependencies: deps_shm)
executable('cloth-new', 'src/cloth-new/main.cpp', include_directories: inc, dependencies: deps_shm)
executable('graph', 'src/graph/main.cpp', include_directories: inc, dependencies: deps_shm, cpp_pch: common_pch)	
executable('raytracer', 'src/raytracer/main.cpp', include_directories: inc, dependencies: deps_shm, cpp_pch: 'inc/raytracer/pch.hpp')
executable('pong', 'src/pong/main.cpp', include_directories: inc, dependencies: deps_shm , cpp_pch: common_pch)
executable('ttt', 'src/ttt/main.cpp', include_directories: inc, dependencies: deps_shm , cpp_pch: common_pch)
executable('raytracer-new', 'src/raytracer-new/main.cpp', include_directories: inc, dependencies: deps_shm , cpp_pch: 'inc/raytracer-new/pch.hpp')
