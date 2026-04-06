#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include <math.h>

#include "nob.h"

// Dynamic Array Helper Macro

#define foreach(ptr, da)                   \
    for (size_t i = 0; i < da.count; ++ i) \
    for (void * ptr = da.items[i]; ptr != NULL; ptr = NULL)

#define foreach_enumerate(i, ptr, da)                   \
    for (size_t i = 0; i < da.count; ++ i)              \
    for (void * ptr = da.items[i]; ptr != NULL; ptr = NULL)
// Constant
// like resource in Bevy
#define BACKGROUND GetColor(0x181818FF)
#define GAMMA 2.2

#define PANEL_WIDTH 400
#define WALL_WIDTH  800
#define WALL_HEIGHT 600
#define WALL_DEPTH  800
#define WINDOW_WIDTH PANEL_WIDTH+WALL_WIDTH
#define WINDOW_HEIGHT WALL_HEIGHT
#define PARTICLE_NUM 100
#define PARTICLE_RADIUS 30
#define MAX_VELOCITY 200
#define MAX_SIZE 40
#define MIN_SIZE 20


// Entity
typedef struct {
    void **items;
    size_t count;
    size_t capacity;
} Entities;

Entities entities = {0};

typedef struct {
    float x, y, w, h, dx, dy;
    Color c;
} Rect;

typedef struct {
    Vector3 min;
    Vector3 max;
    // float lineThick;
} Box;

Box *wall = &(Box) {
    .min = (Vector3) {
        .x=PANEL_WIDTH,
    },
    .max = (Vector3) {
        .x=PANEL_WIDTH+WALL_WIDTH,
        .y=WALL_HEIGHT,
        .z=WALL_DEPTH ,
    }
};
    
// Component
typedef struct {
    Vector3 pos;
    Vector3 vel;
    Color color;
} Particle;


// System


int particle_collide(Particle *a, Particle *b) {
    // printf("collide detect\n"); return;
    if (CheckCollisionSpheres(
        a->pos,
        PARTICLE_RADIUS,
        b->pos,
        PARTICLE_RADIUS
    )) {
        Vector3 t = {
            .x = a->vel.x,
            .y = a->vel.y,
            .z = a->vel.z,
        };
        
        a->vel.x = b->vel.x;        
        a->vel.y = b->vel.y;        
        a->vel.z = b->vel.z;

        b->vel.x = t.x;
        b->vel.y = t.y;
        b->vel.z = t.z;        
    }
}

void box_collide(Particle *p, Box *box) {
    if (p->pos.x < box->min.x || p->pos.x > box->max.x) p->vel.x = - p->vel.x;
    if (p->pos.y < box->min.y || p->pos.y > box->max.y) p->vel.y = - p->vel.y;
    if (p->pos.z < box->min.z || p->pos.z > box->max.z) p->vel.z = - p->vel.z;
}

void statistic() {
    float v_avg = 0;
    float v_square_avg = 0;
    // for ()
}


// helper funciton

Vector3 vec_add(Vector3 a, Vector3 b) {
    return (Vector3) {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}

Vector3 vec_scale(float scalar, Vector3 v) {
    return (Vector3) {
        .x = v.x * scalar,
        .y = v.y * scalar,
        .z = v.z * scalar,
    };
}

Vector2 screen(Particle *p) {
    return (Vector2) {
        .x = (p->pos.x - WALL_WIDTH /2) / (p->pos.z/WALL_DEPTH + 1) + WALL_WIDTH /2,
        .y = (p->pos.y - WALL_HEIGHT/2) / (p->pos.z/WALL_DEPTH + 1) + WALL_HEIGHT/2,
    };
}

Color depth_color(Color origin, float z) {
    return (Color) {
        .r = (unsigned char)((double)(origin.r - 0x18) * pow((1.0 - (double)z/WALL_DEPTH), GAMMA)) + 0x18,
        .g = (unsigned char)((double)(origin.g - 0x18) * pow((1.0 - (double)z/WALL_DEPTH), GAMMA)) + 0x18,
        .b = (unsigned char)((double)(origin.b - 0x18) * pow((1.0 - (double)z/WALL_DEPTH), GAMMA)) + 0x18,
        .a = origin.a,
    };
}

// Render

int particle_depth_compare(const void *a, const void *b) {
    const Particle *p1 = *(const Particle **)a;
    const Particle *p2 = *(const Particle **)b;
    if (p1->pos.z == p2->pos.z) return 0;
    return p1->pos.z > p2->pos.z ? -1 : 1;
}
void render_sort() {
    qsort(entities.items, entities.count, sizeof(void*), particle_depth_compare);
}

bool pause = false;

void update() {
    float dt = GetFrameTime();
    foreach_enumerate(i, entity, entities) {
        if (dt > 0.1) continue; // block when render block
        Particle *p = entity;
        for (size_t j=i+1; j < entities.count; ++j) {
            Particle *p2 = entities.items[j];
            particle_collide(p, p2);            
        }
        box_collide(p, wall);
        p->pos= vec_add(
            p->pos,
            vec_scale(dt, p->vel)
        );
    }
}

void render() {
    // particle -> raylib: DrawCircle
    // box      -> raylib: DrawRectangle
    render_sort();
    foreach(entity, entities) {
        Particle *p = entity;
        DrawCircleV(
            screen(p),
            PARTICLE_RADIUS/(p->pos.z/WALL_DEPTH  + 1),
            depth_color(p->color, p->pos.z)
        );
    }
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ideal GAS in Raylib");
    // spawn entities
    for (int i = 0; i < PARTICLE_NUM; ++i) {
        Particle *p = malloc(sizeof(Particle));
        
        p->color.r = GetRandomValue(50, 255);
        p->color.g = GetRandomValue(50, 255);
        p->color.b = GetRandomValue(50, 255);
        p->color.a = 255;
        p->pos = (Vector3) {
            .x = GetRandomValue(PARTICLE_RADIUS+wall->min.x,wall->max.x-PARTICLE_RADIUS),
            .y = GetRandomValue(PARTICLE_RADIUS+wall->min.y,wall->max.y-PARTICLE_RADIUS),
            .z = GetRandomValue(PARTICLE_RADIUS+wall->min.z,wall->max.z-PARTICLE_RADIUS),
        };

        // check init collide
        
        
        p->vel = (Vector3) {
            .x = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .y = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .z = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
        };
           
        da_append(&entities, p);
    }
    SetTargetFPS(60);

    // render_sort();
    // foreach(e, entities) {
    //     Particle *p = e;
    //     printf("depth: %f\n", p->pos.z);
    // }

    while(!WindowShouldClose()) {
        BeginDrawing();
        // ClearBackground(RAYWHITE);
        ClearBackground(BACKGROUND);
        DrawText("Idal Gas Simulator", 20, 20, 20, WHITE);
        // if (IsKeyPressed(KEY_P)) pause = !pause;
        update();
        render();
        EndDrawing();
    }
    CloseWindow();
    da_free(entities);
    return 0;
}
