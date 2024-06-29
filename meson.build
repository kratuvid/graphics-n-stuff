project('opengl-studies', 'c', default_options: ['c_std=c23'])

deps_core = [dependency('wayland-client'), dependency('wayland-egl'), dependency('egl')]
inc_core = include_directories('inc')

executable('window', 'src/window.c', 'src/wayland/xdg-shell.c', include_directories: inc_core, dependencies: deps_core)
