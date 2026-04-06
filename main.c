#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include <math.h>

#include "nob.h"

// Dynamic Array Helper Macro
// foreach(element, dynamic_array)
// {
//     Type * item = element; // TODO: maybe add type to meta-data and it can auto convert
//     do_something(item);
// }

#define foreach(ptr, da)                                    \
    for (size_t i = 0; i < da.count; ++ i)                  \
    for (void * ptr = da.items[i]; ptr != NULL; ptr = NULL)

#define foreach_enumerate(i, ptr, da)                       \
    for (size_t i = 0; i < da.count; ++ i)                  \
    for (void * ptr = da.items[i]; ptr != NULL; ptr = NULL)

// Constant
// like resource in Bevy

#define PARTICLE_NUM 50
#define PARTICLE_RADIUS 30
#define PANEL_WIDTH 250
#define WALL_WIDTH  800
#define WALL_HEIGHT 600
#define WALL_DEPTH  800
#define WINDOW_WIDTH PANEL_WIDTH+WALL_WIDTH
#define WINDOW_HEIGHT WALL_HEIGHT
#define MAX_VELOCITY 400
#define MAX_SIZE 40
#define MIN_SIZE 20

// render far particle
#define GAMMA 2.2
#define RENDER_DEPTH_FACTOR 1/WALL_DEPTH
#define DIM_RATIO 0.7

#define BACKGROUND GetColor(0x181818FF)

// helper funciton
// vector arithmetic
Vector3 vec_add(Vector3 a, Vector3 b) {
    return (Vector3) {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}

Vector3 vec_sub(Vector3 a, Vector3 b) {
    return (Vector3) {
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
    };
}

Vector3 vec_scale(float scalar, Vector3 v) {
    return (Vector3) {
        .x = v.x * scalar,
        .y = v.y * scalar,
        .z = v.z * scalar,
    };
}

float vec_dot(Vector3 a, Vector3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

float vec_square_norm(Vector3 v) {
    return vec_dot(v, v);
}

float vec_norm(Vector3 v) {
    return powf(vec_dot(v, v), 0.5);
}

// Entity
struct {
    void **items;
    size_t count;
    size_t capacity;
} entities = {0};

typedef struct {
    Vector3 min;
    Vector3 max;
    // float lineThick;
} Box;

// TODO: also add the box to entities and render
Box *wall = &(Box) {
    .min = (Vector3) {
        .x=-WALL_WIDTH /2,
        .y=-WALL_HEIGHT/2,
        .z=0             ,
    },
    .max = (Vector3) {
        .x= WALL_WIDTH /2,
        .y= WALL_HEIGHT/2,
        .z= WALL_DEPTH   ,
    }
};
    
// Component
typedef struct {
    Vector3 pos;
    Vector3 vel;
    Color color;
} Particle;

void spawn_random_particles(size_t particle_numbers) {
    for (size_t i = 0; i < particle_numbers; ++i) {
        
        // Vector3 new_pos = {
        //     .x = GetRandomValue(PARTICLE_RADIUS+wall->min.x,wall->max.x-PARTICLE_RADIUS),
        //     .y = GetRandomValue(PARTICLE_RADIUS+wall->min.y,wall->max.y-PARTICLE_RADIUS),
        //     .z = GetRandomValue(PARTICLE_RADIUS+wall->min.z,wall->max.z-PARTICLE_RADIUS),
        // };
        Particle *p = malloc(sizeof(*p));
        p->pos = (Vector3){
            .x = GetRandomValue((PARTICLE_RADIUS+wall->min.x)*0.01,(wall->max.x-PARTICLE_RADIUS)*0.01),
            .y = GetRandomValue((PARTICLE_RADIUS+wall->min.y)*0.01,(wall->max.y-PARTICLE_RADIUS)*0.01),
            .z = GetRandomValue((PARTICLE_RADIUS+wall->min.z)*0.01,(wall->max.z-PARTICLE_RADIUS)*0.01),
        };
        p->color.r = GetRandomValue(50, 255);
        p->color.g = GetRandomValue(50, 255);
        p->color.b = GetRandomValue(50, 255);
        p->color.a = 255;
        p->vel = (Vector3) {
            .x = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .y = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .z = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
        };
           
        da_append(&entities, p);
    }
}


// System

// perfect elastic impact(after all, it's ideal gas...)
void particle_collide(Particle *a, Particle *b) {
    Vector3 delta    = vec_sub(a->pos, b->pos);
    float   dist     = vec_norm(delta);
    float   min_dist = 2 * PARTICLE_RADIUS;
    
    if (dist < min_dist && dist > 0) {
        Vector3 n = vec_scale(1.0 / dist, delta);
        float va_para_scalar = vec_dot(a->vel, n);
        float vb_para_scalar = vec_dot(b->vel, n);

        // only calculate collide velocity when getting close
        // v_relative < 0
        if (va_para_scalar - vb_para_scalar < 0) {
            Vector3 va_para = vec_scale(va_para_scalar, n);
            Vector3 va_perp = vec_sub  (a->vel, va_para  );
            Vector3 vb_para = vec_scale(vb_para_scalar, n);
            Vector3 vb_perp = vec_sub  (b->vel, vb_para  );

            a->vel  = vec_add(va_perp, vb_para);
            b->vel  = vec_add(vb_perp, va_para);
        }
    }
}

void box_collide(Particle *p, Box *box) {
    if ((p->pos.x-PARTICLE_RADIUS < box->min.x && p->vel.x < 0) || (p->pos.x+PARTICLE_RADIUS > box->max.x && p->vel.x > 0)) p->vel.x = -p->vel.x;
    if ((p->pos.y-PARTICLE_RADIUS < box->min.y && p->vel.y < 0) || (p->pos.y+PARTICLE_RADIUS > box->max.y && p->vel.y > 0)) p->vel.y = -p->vel.y;
    if ((p->pos.z-PARTICLE_RADIUS < box->min.z && p->vel.z < 0) || (p->pos.z+PARTICLE_RADIUS > box->max.z && p->vel.z > 0)) p->vel.z = -p->vel.z;
}

typedef struct {
    float v_square_avg;
    float v_avg;
    float temperature;
    float kinetic_energy;
} Statistic;

Statistic statistic = {0};

void analysis() {
    float v_square_sum = 0;
    float v_sum = 0;
    foreach(e, entities) {
        Particle *p = e;
        v_square_sum += vec_square_norm(p->vel);
        v_sum += vec_norm(p->vel);
    }
    float v_square_avg = v_square_sum / PARTICLE_NUM;
    float v_avg        = v_sum        / PARTICLE_NUM;
    statistic = (Statistic) {
        .v_square_avg   = v_square_avg,
        .v_avg          = v_avg,
        .temperature    = v_square_avg,
        .kinetic_energy = v_square_avg,
    };
}


// Render


Color depth_color(Color origin, float z) {
    return (Color) {
        .r = (unsigned char)((float)(origin.r - 0x18) * powf((1.0 - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .g = (unsigned char)((float)(origin.g - 0x18) * powf((1.0 - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .b = (unsigned char)((float)(origin.b - 0x18) * powf((1.0 - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .a = origin.a,
    };
}

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
    foreach_enumerate(i, e, entities) {
        if (dt > 0.1) continue; // block when render block
        Particle *p = e;
        for (size_t j=i+1; j < entities.count; ++j) {
            Particle *p2 = entities.items[j];
            particle_collide(p, p2);            
        }
        box_collide(p, wall);
        p->pos = vec_add(
            p->pos,
            vec_scale(dt, p->vel)
        );
    }
}

// particle project to screen
Vector2 screen(Particle *p) {
    return (Vector2) {
        .x = p->pos.x / (p->pos.z*RENDER_DEPTH_FACTOR + 1) + WALL_WIDTH /2 + PANEL_WIDTH,
        .y = p->pos.y / (p->pos.z*RENDER_DEPTH_FACTOR + 1) + WALL_HEIGHT/2,
    };
}

char buf[256] = {0};
void render() {
    // particle -> raylib: DrawCircle
    // box      -> raylib: DrawRectangle
    render_sort();
    foreach(entity, entities) {
        Particle *p = entity;
        DrawCircleV(
            screen(p),
            PARTICLE_RADIUS/(p->pos.z*RENDER_DEPTH_FACTOR + 1),
            depth_color(p->color, p->pos.z)
        );
    }
    // Panel
    DrawText("Ideal Gas Simulator", 20, 20, 20, WHITE);
    snprintf(buf, sizeof(buf), "<V2> = %.2f", statistic.v_square_avg);
    DrawText(buf, 20, 50, 20, WHITE);
    snprintf(buf, sizeof(buf), "<V>  = %.2f", statistic.v_avg);
    DrawText(buf, 20, 80, 20, WHITE);
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ideal GAS in Raylib");
    // spawn entities
    spawn_random_particles(PARTICLE_NUM);

    SetTargetFPS(60);
    while(!WindowShouldClose()) {
        BeginDrawing();
        // ClearBackground(RAYWHITE);
        ClearBackground(BACKGROUND);
        // if (IsKeyPressed(KEY_P)) pause = !pause;
        update();
        analysis();
        render();
        EndDrawing();
    }
    CloseWindow();
    
    da_free(entities);
    return 0;
}
