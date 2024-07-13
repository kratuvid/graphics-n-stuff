# OpenGL Studies

## Fractals-MP
Use a command like this to render and convert to a file
```
bt=build-release; meson compile -C $bt && ./$bt/fractal-mp --render --fps 60 --center-sway-mode 1 --start-center '0,0' --final-center '-0.14858386523612894e1,0.37240552882300729e-1' --start-range '4,4' --zoom 6018 --initial-iterations 73 --seconds 10 --silent | ffmpeg -y -f rawvideo -pix_fmt bgra -s 1910x1010 -r 60 -i - -c:v libx265 -crf 26 ~/Videos/cooked.mkv && mpv --hwdec=none ~/Videos/cooked.mkv
```
