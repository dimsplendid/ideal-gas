#include <stdbool.h>

#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

char buf[256];
bool run = false;

int main(int argc, char **argv) {
    GO_REBUILD_URSELF(argc, argv);

    const char *program_name = shift(argv, argc);
    while(argc > 0) {
        const char *flag = shift(argv, argc);
        if (strcmp(flag, "--run") == 0) { run = true; continue; }
        if (strcmp(flag, "-r") == 0)    { run = true; continue; }
        fprintf(stderr, "%s:%d: ERROR: unknown flag `%s`\n", __FILE__, __LINE__, flag);
        return 1;
    }
    
    const char *source_path = "main.c";
    const char *output_path = "ideal_gas.exe";
    Cmd cmd = {0};
    if (needs_rebuild1(output_path, source_path)) {

        cmd_append(&cmd, "../third-party/w64devkit/bin/gcc.exe");
        cmd_append(&cmd, "-Wall", "-Wextra", "-o", output_path, source_path);
        cmd_append(&cmd, "-I", "../third-party/raylib-5.5_win64_mingw-w64/include");
        cmd_append(&cmd, "-L", "../third-party/raylib-5.5_win64_mingw-w64/lib");
        cmd_append(&cmd, "-l", "raylib");
        cmd_append(&cmd, "-l", "gdi32");
        cmd_append(&cmd, "-l", "winmm");
    
        if (!cmd_run(&cmd)) return 1;
    } else {
        nob_log(INFO, "%s is up to date", output_path);
    }

    if (run) {
        printf("------------------------------\n");
        sprintf(buf, "./%s", output_path);
        cmd_append(&cmd, buf);
        if (!cmd_run(&cmd)) return 1;
    }

    cmd_free(cmd);
    return 0;
}
