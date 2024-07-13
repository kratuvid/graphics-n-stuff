# OpenGL Studies

## Fractals-MP
Use a command like this to render and convert to a file
```
meson compile -C build-release && \
    ./build-release/fractal-mp --render --fps 60 --center-sway-mode 1 \
        --start-center '0,0' --final-center '-0.56207841976963335,0.64379067107853416' \
        --start-range '4,4' --zoom 9.31322574615478 --seconds 5 --silent
    | ffmpeg -y -f rawvideo -pix_fmt bgra -s 1910x1010 -r 24 -i - -c:v ffv1 ~/Videos/cooked.mkv
    && mpv --hwdec=none ~/Videos/cooked.mkv
```
