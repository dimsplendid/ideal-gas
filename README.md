# Ideal Gas Simulator

Simple simulator that using raylib to render. [raylib 5.5](https://github.com/raysan5/raylib/)
The canvas is 2D, but using projection to mimic 3D.
The projection Using simple (x/z, y/z), following Tsoding tutorial: [One Formula That Demystifies 3D Graphics](https://www.youtube.com/watch?v=qjWkNZ0SXfo)

## Build

Using [nob.h](https://github.com/tsoding/nob.h)(recommend checking this amazing project) at Windows 11.
It should be way easier on linux I supposed.

Windows environment: [w64devkit](https://github.com/skeeto/w64devkit/)
> I try msvc, but it just segfault and link fail again and again...
> The compile tag and this environment follow [raylib Windows instrucment](https://github.com/raysan5/raylib/wiki/Working-on-Windows)
> 

```bash
# start up,
# gcc should in the $Path, it sould work at Windows 11
# by add /path/to/w64devkit/bin to Path
# check the raylib include path in nob.c
# 
gcc -o nob.exe nob.c

# compile and run
./nob.exe -r
```

![ScreenShot](./screen_shot/screen_shot_20260406.png)

## TODO

1. [ ] chaotic system
    1. gravitation
    > could I simulte solar system?
    2. dual pendulum
2. [ ] quantum system
    > wavepilot? only wave? density matrix?
3. [ ] try hot loading
    1. separate files
    2. dynamic link
    > maybe depend on OS?
    > I also think this would be a cool ideal for a __programming game__
4. [ ] implement more ui
5. [ ] Web application
