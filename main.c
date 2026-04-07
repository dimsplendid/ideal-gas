#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include <math.h>

#include "nob.h"

// Constant
// like resource in Bevy
#define PARTICLE_NUM        200
#define PARTICLE_RADIUS     30
#define MAX_VELOCITY        400

#define GRID_SIZE           32
#define CELL_NUM            GRID_SIZE*GRID_SIZE*GRID_SIZE

// Layout
#define PANEL_WIDTH         250
#define WALL_WIDTH          800
#define WALL_HEIGHT         600
#define WALL_DEPTH          800
#define WINDOW_WIDTH        PANEL_WIDTH+WALL_WIDTH
#define WINDOW_HEIGHT       WALL_HEIGHT
#define BACKGROUND          GetColor(0x181818FF)

// render far particle
#define GAMMA               1.8f
#define RENDER_DEPTH_FACTOR 1/WALL_DEPTH
#define DIM_RATIO           0.7f


// States: TODO

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
    return sqrtf(vec_dot(v, v));
}

// Entity
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

// try more ecs method
struct {
    // index indicate an entity
    // only particle for now, maybe using Union type sys later
    // or lots of array to handle component?
    Particle *items;
    size_t count;
    size_t capacity;
} particles = {0};

// tracking render order
struct {
    size_t *items;
    size_t count;
    size_t capacity;
} render_order = {0};

// tracking particle in grid
// divide space into 32 * 32 * 32 grid
typedef struct {
    size_t *items; // particle ids
    size_t count;
    size_t capacity;
} Cell;
Cell grid[GRID_SIZE * GRID_SIZE * GRID_SIZE] = {0};
// grid(i, j, k) = grid[i * size^2 + j * size + k] // map 3D to 1D
// i, j, k <- (int) (W, H, D) / grid_size
void grid_update(size_t id, Vector3 *pos) {
    float cell_w = (float) WALL_WIDTH  / GRID_SIZE;
    float cell_h = (float) WALL_HEIGHT / GRID_SIZE;
    float cell_d = (float) WALL_DEPTH  / GRID_SIZE;
    int i = (pos->x - wall->min.x) / cell_w;
    int j = (pos->y - wall->min.y) / cell_h;
    int k = (pos->z - wall->min.z) / cell_d;
    
    if (i < 0) i = 0; else if (i >= GRID_SIZE) i = GRID_SIZE - 1;
    if (j < 0) j = 0; else if (j >= GRID_SIZE) j = GRID_SIZE - 1;
    if (k < 0) k = 0; else if (k >= GRID_SIZE) k = GRID_SIZE - 1;

    int idx = i * GRID_SIZE * GRID_SIZE + j * GRID_SIZE + k;
    if (idx >= 0 && idx < CELL_NUM) da_append(&grid[idx], id);
}
void grid_reset() {
    for (size_t idx = 0; idx < CELL_NUM; ++idx) {
        Cell * s = &grid[idx];
        s->count = 0;
    }
}

typedef struct {
    size_t *items; // cell id
    size_t count;
    size_t capacity;
} CellId; // surrounding cell index of cell
CellId neighbor_cells[GRID_SIZE * GRID_SIZE * GRID_SIZE] = {0};

void neighbor_cells_init() {
    for (size_t idx = 0; idx < CELL_NUM; ++idx) {
        int i = idx / GRID_SIZE / GRID_SIZE;
        int j = idx / GRID_SIZE % GRID_SIZE;
        int k = idx % GRID_SIZE;
        da_append(neighbor_cells+idx, idx);
        for (int ni = i-1; ni <= i+1; ++ni) { if (ni == -1 || ni == GRID_SIZE) continue;
        for (int nj = j-1; nj <= j+1; ++nj) { if (nj == -1 || nj == GRID_SIZE) continue;
        for (int nk = k-1; nk <= k+1; ++nk) { if (nk == -1 || nk == GRID_SIZE) continue;
            size_t nidx = ni * GRID_SIZE * GRID_SIZE + nj * GRID_SIZE + nk;
            if (nidx == idx) continue;
            da_append(neighbor_cells+idx, nidx);
        }}}
    }
}

void spawn_random_particles(size_t particle_numbers) {
    for (size_t i = 0; i < particle_numbers; ++i) {
        Particle p = {0};
        p.pos = (Vector3){
            .x = GetRandomValue((PARTICLE_RADIUS+wall->min.x)*0.01f,(wall->max.x-PARTICLE_RADIUS)*0.01f),
            .y = GetRandomValue((PARTICLE_RADIUS+wall->min.y)*0.01f,(wall->max.y-PARTICLE_RADIUS)*0.01f),
            .z = GetRandomValue((PARTICLE_RADIUS+wall->min.z)*0.01f,(wall->max.z-PARTICLE_RADIUS)*0.01f),
        };
        p.vel = (Vector3) {
            .x = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .y = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
            .z = GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY),
        };
        p.color = (Color) {
            .r = GetRandomValue(50, 255),
            .g = GetRandomValue(50, 255),
            .b = GetRandomValue(50, 255),
            .a = 255,
        };
        da_append(&particles, p);
        da_append(&render_order, i);
        grid_update(i, &p.pos);
    }
        
    neighbor_cells_init();
}


// System

// perfestic_reset impact(after all, it's ideal gas...)
void particle_collide(Particle *a, Particle *b) {
    Vector3 delta       = vec_sub(b->pos, a->pos);
    float   dist_sq     = vec_square_norm(delta);
    float   min_dist_sq = 4 * PARTICLE_RADIUS * PARTICLE_RADIUS;
    
    if (dist_sq < min_dist_sq) {
        Vector3 v_rel = vec_sub(b->vel, a->vel); // relative velocity
        float v_dot_delta = vec_dot(v_rel, delta);
        // only calculate collide velocity when getting close
        if (v_dot_delta < 0) {
            float scalar = v_dot_delta / dist_sq;
            Vector3 impulse = vec_scale(scalar, delta);

            a->vel  = vec_add(a->vel, impulse);
            b->vel  = vec_sub(b->vel, impulse);
            // overlap modified
            float dist = sqrtf(dist_sq);
            float overlap = 2 * PARTICLE_RADIUS - dist;
            Vector3 separation = vec_scale(overlap * 0.5f / dist, delta);
            a->pos = vec_sub(a->pos, separation);
            b->pos = vec_add(b->pos, separation);
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
    da_foreach(Particle, p, &particles) {
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

// Math Plot Lib
typedef struct {
    Color color;
    float thick;
} Style;

typedef struct {
    Vector2 begin;
    Vector2 end;
    Style style;
} Arrow;

typedef struct {
    float *val;
    size_t count;
    size_t capacity;    
} Array;

typedef struct {
} Figure;

void draw_arrow(Arrow a) {
    UNUSED(a);
    TODO("");
}

// Render

// 3D-like effect
// make far particle dimmer
Color depth_color(Color origin, float z) {
    return (Color) {
        .r = (unsigned char)((float)(origin.r - 0x18) * powf((1.0f - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .g = (unsigned char)((float)(origin.g - 0x18) * powf((1.0f - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .b = (unsigned char)((float)(origin.b - 0x18) * powf((1.0f - z/WALL_DEPTH*DIM_RATIO), GAMMA)) + 0x18,
        .a = origin.a,
    };
}
// raylib render the later object at upper "layer", which make
// sense, just cover on it :)
// sorted by depth to mimic the depth cover effect
int particle_depth_compare(const void *a, const void *b) {
    const Particle p1 = particles.items[*(const size_t *)a];
    const Particle p2 = particles.items[*(const size_t *)b];
    if (p1.pos.z == p2.pos.z) return 0;
    return p1.pos.z > p2.pos.z ? -1 : 1;
}

// may cause tracking problem if track particle by id;
void render_sort() {
    qsort(render_order.items, render_order.count, sizeof(void*), particle_depth_compare);
}

bool pause = false;

void update() {
    if (pause) return; // <- control by events
    float dt = GetFrameTime();
    for (size_t i = 0; i < CELL_NUM; ++i) {
        if (dt > 0.1) continue; // block when render block
        Cell * cell = &grid[i];
        // TODO: neighbor_cells generate at the beginning
        CellId * neighbor_cell = &neighbor_cells[i];
        da_foreach(size_t, p1, cell) {
            size_t id1 = *p1;
            Particle * particle1 = particles.items+id1;
            da_foreach(size_t, box_idx, neighbor_cell) {
                Cell * sur_cell = &grid[*box_idx];
                da_foreach(size_t, p2, sur_cell) {
                    size_t id2 = *p2;
                    if (id1 <= id2) continue;
                    particle_collide(particle1, particles.items+id2);          
                }
            }
            box_collide(particle1, wall);
            particle1->pos = vec_add(
                particle1->pos,
                vec_scale(dt, particle1->vel)
            );
        }
    }
    grid_reset();
    da_foreach(Particle, p, &particles) {
        grid_update(p-particles.items, &p->pos);
    }
}

// particle project to screen
Vector2 screen(Particle *p) {
    return (Vector2) {
        .x = p->pos.x / (p->pos.z*RENDER_DEPTH_FACTOR + 1) + WALL_WIDTH /2 + PANEL_WIDTH,
        .y = p->pos.y / (p->pos.z*RENDER_DEPTH_FACTOR + 1) + WALL_HEIGHT/2,
    };
}

void render() {
    // particle -> raylib: DrawCircle
    // box      -> raylib: DrawRectangle
    render_sort();
    ClearBackground(BACKGROUND);
    da_foreach(size_t, index, &render_order) {
        Particle *p = &particles.items[*index];
        DrawCircleV(
            screen(p),
            PARTICLE_RADIUS/(p->pos.z*RENDER_DEPTH_FACTOR + 1),
            depth_color(p->color, p->pos.z)
        );
    }
    // Panel
    DrawText("Ideal Gas Simulator", 20, 20, 20, WHITE);
    DrawText(TextFormat("<V2> = %.2f", statistic.v_square_avg), 20, 50, 20, WHITE);
    DrawText(TextFormat("<V>  = %.2f", statistic.v_avg       ), 20, 80, 20, WHITE);
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ideal GAS in Raylib");
    // spawn entities
    spawn_random_particles(PARTICLE_NUM);
    SetTargetFPS(60);
    // SetMouseCursor(MOUSE_CURSOR_CROSSHAIR); // test for fun
    // test for print grid
    // for (size_t i = 0; i < CELL_NUM; ++i) {
    //     Cell * cell = &neighbor_cells[i];
    //     printf("(");
    //     da_foreach(size_t, ptr, cell) {
    //         printf("%zu, ", *ptr);
    //     }
    //     printf(")\n");
    // }
    // printf("%d\n", CELL_NUM);
    while(!WindowShouldClose()) {
        BeginDrawing();
        // TODO: maybe add to event() ?
        if (IsKeyPressed(KEY_P)) pause = !pause;
        update();
        analysis();
        render();
        EndDrawing();
    }
    CloseWindow();
    
    da_free(particles);
    da_free(render_order);
    return 0;
}
