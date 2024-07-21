project('opengl-studies', 'c', 'cpp', default_options: ['c_std=c23', 'cpp_std=c++23', 'b_lto=true'])
args_base = ['-pipe', '-march=native']
if get_option('buildtype').startswith('debug')
  args = args_base + ['-DDEBUG']
  add_project_arguments(args, language: 'cpp')
  add_project_link_arguments(args, language: 'cpp')
else
  args = args_base + ['-DNDEBUG', '-Ofast', '-fomit-frame-pointer']
  add_project_arguments(args, language: 'cpp')
  add_project_link_arguments(args, language: 'cpp')
endif

cpp = meson.get_compiler('cpp')

incs = {
  'primary': include_directories('inc'),
  'glad': include_directories('vendor/glad/include')
}

libs = {
  'wl-xdg-shell': static_library('wl_xdg_shell', 'src/wayland/xdg-shell.c', include_directories: incs['primary']),
  'glad': static_library('glad', 'vendor/glad/src/glad.c', include_directories: incs['glad'])
}

deps = {
  # internal
  'wl-xdg-shell': declare_dependency(link_with: libs['wl-xdg-shell'], include_directories: incs['primary']),
  'glad': declare_dependency(link_with: libs['glad'], include_directories: incs['glad']),

  # external
  'wl': dependency('wayland-client'),
  'wl-egl': dependency('wayland-egl'),
  'egl': dependency('egl'),
  'xkbcommon': dependency('xkbcommon'),
  'cairomm': dependency('cairomm-1.16'),
  'pangomm': dependency('pangomm-2.48'),
  'spdlog': dependency('spdlog'),
  'mpfr': dependency('mpfr'),
  'mpc': cpp.find_library('mpc')
}

deps_shm = [deps['spdlog'], deps['wl'], deps['wl-xdg-shell'], deps['xkbcommon'], deps['cairomm'], deps['pangomm']]
deps_egl = deps_shm + [deps['wl-egl'], deps['egl'], deps['glad']]

common_pch = 'inc/pch/pch.hpp'
executable('window', 'src/window/window.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('window-lamp', 'src/window-lamp/window.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('heart', 'src/heart/heart.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('bouncing-ball', 'src/bouncing-ball/bouncing-ball.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('cloth', 'src/cloth/cloth.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('pendulum', 'src/pendulum/main.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('two-pendulum', 'src/two-pendulum/main.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('cloth-new', 'src/cloth-new/main.cpp', include_directories: incs['primary'], dependencies: deps_shm)
executable('graph', 'src/graph/main.cpp', include_directories: incs['primary'], dependencies: deps_shm, cpp_pch: common_pch)	
executable('raytracer', 'src/raytracer/main.cpp', include_directories: incs['primary'], dependencies: deps_shm, cpp_pch: 'inc/raytracer/pch.hpp')
executable('pong', 'src/pong/main.cpp', include_directories: incs['primary'], dependencies: deps_shm , cpp_pch: common_pch)
executable('ttt', 'src/ttt/main.cpp', include_directories: incs['primary'], dependencies: deps_shm , cpp_pch: common_pch)
executable('raytracer-new', 'src/raytracer-new/main.cpp', include_directories: incs['primary'], dependencies: deps_shm , cpp_pch: 'inc/raytracer-new/pch.hpp')
executable('fractal', 'src/fractal/main.cpp', include_directories: incs['primary'], dependencies: deps_shm , cpp_pch: 'inc/fractal/pch.hpp')
executable('fractal-mp', 'src/fractal-mp/main.cpp', include_directories: incs['primary'], dependencies: deps_shm + deps['mpfr'] + deps['mpc'], cpp_pch: 'inc/fractal-mp/pch.hpp')

executable('sap', 'src/sap/main.cpp', include_directories: [incs['primary']] + [include_directories('inc/sap')], dependencies: deps_egl, cpp_pch: 'inc/sap/pch.hpp')
executable('ps11', 'src/ps11/main.cpp', include_directories: [incs['primary']] + [include_directories('inc/ps11')], dependencies: deps_shm, cpp_pch: 'inc/ps11/pch.hpp')
