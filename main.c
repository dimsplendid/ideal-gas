#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include <math.h>

#include "nob.h"

// Constant
// like resource in Bevy
#define PARTICLE_NUM        500
#define PARTICLE_RADIUS     30
#define MAX_VELOCITY        400

#define GRID_LEN            20
#define CELL_NUM            (GRID_LEN*GRID_LEN*GRID_LEN)
#define BIN_COUNT           30
#define TRAJECTORY_LEN      500
// Layout
#define PANEL_WIDTH         250
#define WALL_WIDTH          800
#define WALL_HEIGHT         600
#define WALL_DEPTH          800
#define WINDOW_WIDTH        (PANEL_WIDTH+WALL_WIDTH)
#define WINDOW_HEIGHT       WALL_HEIGHT
#define BACKGROUND          GetColor(0x181818FF)

// render far particle
#define GAMMA               1.8f
#define RENDER_DEPTH_FACTOR (1.0f/WALL_DEPTH)
#define DIM_RATIO           0.7f

// States
bool pause = false; // Control is position update


// helper funciton
// vector arithmetic
Vector3 vec_add(Vector3 a, Vector3 b) {
    return (Vector3) {
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}

// meta data

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

void vec_print(Vector3 v) {
    printf("v = (%f, %f, %f)", v.x, v.y, v.z);
}

// Entity
struct {
    size_t *items;
    size_t count;
    size_t capacity;
} entity = {0};// store id only

typedef struct {
    Vector3 min;
    Vector3 max;
    // float lineThick;
} Box;

// TODO: also add the box to entities and render
Box *wall = &(Box) {
    .min = (Vector3) {
        .x=-(float)WALL_WIDTH /2,
        .y=-(float)WALL_HEIGHT/2,
        .z=0,
    },
    .max = (Vector3) {
        .x= (float)WALL_WIDTH /2,
        .y= (float)WALL_HEIGHT/2,
        .z= WALL_DEPTH,
    }
};

// Component
// separate components, all tracked by ids

typedef enum {
    COMPONENT_NONE       = 0,
    COMPONENT_POSITION   = 1 << 0,
    COMPONENT_VELOCITY   = 1 << 1,
    COMPONENT_TRAJECTORY = 1 << 2,
    COMPONENT_GRAVITY    = 1 << 3,
    COMPONENT_COLLIDABLE = 1 << 4,
    COMPONENT_VISIBLE    = 1 << 5,
} ComponentTag;

// Check component
#define ENTITY_HAS(id, tag) ((component_tag.items[id] & (tag)) == (tag))
#define ENTITY_ANY(id, tag) (component_tag.items[id] & (tag))

struct {
    ComponentTag *items;
    size_t count;
    size_t capacity;
} component_tag = {0};

typedef struct {
    float *items;
    size_t count;
    size_t capacity;
} RealArr;

RealArr Radius = {0};
RealArr Vx     = {0};
RealArr Vy     = {0};
RealArr Vz     = {0};
RealArr Px     = {0};
RealArr Py     = {0};
RealArr Pz     = {0};

struct {
    Color *items;
    size_t count;
    size_t capacity;
} color = {0};

typedef struct {
    Vector3 *items;
    size_t count;
    size_t capacity;
} Trajectory;

struct {
    Trajectory *items;
    size_t count;
    size_t capacity;
} trajectory = {0};

// tracking render order
struct {
    size_t *items;
    size_t count;
    size_t capacity;
} render_order = {0};

// tracking particle in grid
// divide space into 32 * 32 * 32 grid
// TODO: maybe it's better to calculat base on particle size and box size?
typedef struct {
    size_t *items; // particle ids
    size_t count;
    size_t capacity;
} Cell;
Cell grid[CELL_NUM] = {0};
// grid(i, j, k) = grid[i * size^2 + j * size + k] // map 3D to 1D
// i, j, k <- (int) (W, H, D) / grid_size
void grid_put(size_t id, Vector3 *pos) {
    float cell_w = (float) WALL_WIDTH  / GRID_LEN;
    float cell_h = (float) WALL_HEIGHT / GRID_LEN;
    float cell_d = (float) WALL_DEPTH  / GRID_LEN;
    int i = (pos->x - wall->min.x) / cell_w;
    int j = (pos->y - wall->min.y) / cell_h;
    int k = (pos->z - wall->min.z) / cell_d;

    // out of bound modified
    if (i < 0) i = 0; else if (i >= GRID_LEN) i = GRID_LEN - 1;
    if (j < 0) j = 0; else if (j >= GRID_LEN) j = GRID_LEN - 1;
    if (k < 0) k = 0; else if (k >= GRID_LEN) k = GRID_LEN - 1;

    int idx = i * GRID_LEN * GRID_LEN + j * GRID_LEN + k;
    // if (idx >= 0 && idx < CELL_NUM) da_append(&grid[idx], id);
    da_append(&grid[idx], id);
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
CellId neighbor_cells[GRID_LEN * GRID_LEN * GRID_LEN] = {0};

void neighbor_cells_init() {
    for (size_t idx = 0; idx < CELL_NUM; ++idx) {
        int i = idx / GRID_LEN / GRID_LEN;
        int j = idx / GRID_LEN % GRID_LEN;
        int k = idx % GRID_LEN;
        da_append(neighbor_cells+idx, idx);
        for (int ni = i-1; ni <= i+1; ++ni) { if (ni == -1 || ni == GRID_LEN) continue;
        for (int nj = j-1; nj <= j+1; ++nj) { if (nj == -1 || nj == GRID_LEN) continue;
        for (int nk = k-1; nk <= k+1; ++nk) { if (nk == -1 || nk == GRID_LEN) continue;
            size_t nidx = ni * GRID_LEN * GRID_LEN + nj * GRID_LEN + nk;
            if (nidx == idx) continue;
            da_append(neighbor_cells+idx, nidx);
        }}}
    }
}

void spawn_random_particles(size_t particle_numbers) {
    for (size_t i = 0; i < particle_numbers; ++i) {
        da_append(&Radius, PARTICLE_RADIUS);
        da_append(&Px, (float)GetRandomValue((PARTICLE_RADIUS+wall->min.x)*0.1,(wall->max.x-PARTICLE_RADIUS)*0.1));
        da_append(&Py, (float)GetRandomValue((PARTICLE_RADIUS+wall->min.y)*0.1,(wall->max.y-PARTICLE_RADIUS)*0.1));
        da_append(&Pz, (float)GetRandomValue((PARTICLE_RADIUS+wall->min.z)*0.1,(wall->max.z-PARTICLE_RADIUS)*0.1) + 10.0f);
        da_append(&Vx, 0);
        da_append(&Vy, 0);
        da_append(&Vz, (float) -MAX_VELOCITY);
        da_append(&color,((Color){
            .r = GetRandomValue(50, 255),
            .g = GetRandomValue(50, 255),
            .b = GetRandomValue(50, 255),
            .a = 255,
        }));
        da_append(&render_order, i);
        da_append(&entity, i);
        grid_put(i, & (Vector3) {Px.items[i], Py.items[i], Pz.items[i]});

        // Trajectory
        da_append(&trajectory, ((Trajectory) {0}));
        ComponentTag tag =
           COMPONENT_POSITION     |
           COMPONENT_VELOCITY     |
           COMPONENT_GRAVITY      |
           COMPONENT_COLLIDABLE   |
           COMPONENT_NONE;

        if (i == 0) {
            tag |= COMPONENT_TRAJECTORY | COMPONENT_VISIBLE;
            // Radius.items[i] *= 10; // TODO: Modified collision
        }
        da_append(&component_tag, tag);
    }
    neighbor_cells_init();
}


// System

// perfect elasstic impact(after all, it's ideal gas...)
void particle_collide(size_t a, size_t b) {
    Vector3 Pa = {Px.items[a], Py.items[a], Pz.items[a]};
    Vector3 Pb = {Px.items[b], Py.items[b], Pz.items[b]};
    Vector3 delta       = vec_sub(Pb, Pa);
    float   dist_sq     = vec_square_norm(delta);
    float   min_dist_sq = 4 * PARTICLE_RADIUS * PARTICLE_RADIUS;

    // only calculate velocity change when touching
    if (dist_sq > min_dist_sq) return;
    Vector3 Va = {Vx.items[a], Vy.items[a], Vz.items[a]};
    Vector3 Vb = {Vx.items[b], Vy.items[b], Vz.items[b]};
    Vector3 v_rel = vec_sub(Vb, Va); // relative velocity
    float v_dot_delta = vec_dot(v_rel, delta);
    
    // only calculate collide velocity when getting close
    if (v_dot_delta > 0) return;
    float scalar = v_dot_delta / dist_sq;
    Vector3 impulse = vec_scale(scalar, delta);

    Va = vec_add(Va, impulse);
    Vb = vec_sub(Vb, impulse);
    Vx.items[a] = Va.x;
    Vy.items[a] = Va.y;
    Vz.items[a] = Va.z;
    Vx.items[b] = Vb.x;
    Vy.items[b] = Vb.y;
    Vz.items[b] = Vb.z;
}

void particle_collide_sys() {
    TODO("");
}

void box_collide(size_t p, Box *box) {
    if ((Px.items[p]-PARTICLE_RADIUS < box->min.x && Vx.items[p] < 0) || (Px.items[p]+PARTICLE_RADIUS > box->max.x && Vx.items[p] > 0)) Vx.items[p] = -Vx.items[p];
    if ((Py.items[p]-PARTICLE_RADIUS < box->min.y && Vy.items[p] < 0) || (Py.items[p]+PARTICLE_RADIUS > box->max.y && Vy.items[p] > 0)) Vy.items[p] = -Vy.items[p];
    if ((Pz.items[p]-PARTICLE_RADIUS < box->min.z && Vz.items[p] < 0) || (Pz.items[p]+PARTICLE_RADIUS > box->max.z && Vz.items[p] > 0)) Vz.items[p] = -Vz.items[p];
}

void box_collide_sys() {
    TODO("");
    // TODO("Add box merge?");
}

void movement_sys() {
    TODO("");    
}

typedef struct {
    float v_square_avg;
    float v_avg;
    float temperature;
    float kinetic_energy;
    
} Statistic;

Statistic statistic = {0};


float vel_bin[BIN_COUNT]  = {0};
float vel2_bin[BIN_COUNT] = {0};

void analysis() {
    float v_square_sum = 0;
    float v_sum = 0;
    for (size_t i = 0; i < BIN_COUNT; ++i) {
        vel_bin[i] = 0;
        vel2_bin[i] = 0;
    }
    float v_max = 0;
    float v2_max = 0;
    for (size_t i = 0; i < entity.count; ++i) {
        Vector3 vi    = {Vx.items[i], Vy.items[i], Vz.items[i]};
        float v2n     = vec_square_norm(vi);
        float vn      = sqrtf(v2n);
        v_max = v_max > vn ? v_max : vn;
        v2_max = v2_max > v2n ? v2_max : v2n;
    }
    for (size_t i = 0; i < entity.count; ++i) {
        Vector3 vi    = {Vx.items[i], Vy.items[i], Vz.items[i]};
        float v2n     = vec_square_norm(vi);
        float vn      = sqrtf(v2n);
        v_sum        += vn;
        v_square_sum += v2n;
        size_t bin = vn / (v_max / BIN_COUNT);
        if (bin >= BIN_COUNT) bin = BIN_COUNT - 1;
        vel_bin[bin] += 1.0f / PARTICLE_NUM;
        bin = v2n / (v2_max / BIN_COUNT);
        if (bin >= BIN_COUNT) bin = BIN_COUNT - 1;
        vel2_bin[bin] += 1.0f / PARTICLE_NUM;
    }
    float v_square_avg = v_square_sum / PARTICLE_NUM;
    float v_avg        = v_sum        / PARTICLE_NUM;
    statistic.v_square_avg   = v_square_avg;
    statistic.v_avg          = v_avg       ;
    statistic.temperature    = v_square_avg;
    statistic.kinetic_energy = v_square_avg;
}

// TODO: Math Plot Lib
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
    const size_t Pa_z = Pz.items[*(const size_t *)a];
    const size_t Pb_z = Pz.items[*(const size_t *)b];
    if (Pa_z == Pb_z) return 0;
    return Pa_z > Pb_z ? -1 : 1;
}

void render_sort() {
    qsort(render_order.items, render_order.count, sizeof(void*), particle_depth_compare);
}

size_t count = 0;


void trajectory_update() {
    count++;
    for (size_t i = 0; i < entity.count; ++i) {
        // skip entity without component
        // if ((component_tag.items[i] & COMPONENT_TRAJECTORY) ^ COMPONENT_TRAJECTORY) continue;
        // component_gaurd(i, COMPONENT_TRAJECTORY);
        if (!ENTITY_HAS(i, COMPONENT_TRAJECTORY)) continue;
        Vector3 pos = {Px.items[i], Py.items[i], Pz.items[i]};
        Trajectory *t = &trajectory.items[i];
        // ring buffer -> trajectory start with (count % TRAJECTORY_LEN)
        if (count < TRAJECTORY_LEN) {
            da_append(t, pos);
        } else {
            t->items[count % TRAJECTORY_LEN] = pos;
        }
    }
}

void velocity_update() {
    // perfect elasstic impact
    for (size_t i = 0; i < CELL_NUM; ++i) {
        Cell * cell = &grid[i];
        CellId * neighbor_cell = &neighbor_cells[i];
        da_foreach(size_t, p1, cell) {
            size_t id1 = *p1;
            da_foreach(size_t, box_idx, neighbor_cell) {
                Cell * sur_cell = &grid[*box_idx];
                da_foreach(size_t, p2, sur_cell) {
                    size_t id2 = *p2;
                    if (id1 <= id2) continue;
                    particle_collide(id1, id2);          
                }
            }
            box_collide(id1, wall);
        }
    }
}

void position_update(float dt) {
    grid_reset();
    for (size_t i = 0; i < entity.count; ++i) {
        Px.items[i] = Px.items[i] + Vx.items[i] * dt;
        Py.items[i] = Py.items[i] + Vy.items[i] * dt;
        Pz.items[i] = Pz.items[i] + Vz.items[i] * dt;
        // position grid refill
        grid_put(i, & (Vector3) {Px.items[i], Py.items[i], Pz.items[i]});
    }
}

void update() {
    if (pause) return; // <- control by events
    float dt = GetFrameTime();
    if (dt > 0.1) return; // block when render block

    velocity_update();
    position_update(dt);
    trajectory_update();
}

// particle project to screen
Vector2 screen(Vector3 pos) {
    return (Vector2) {
        .x = pos.x / (pos.z*RENDER_DEPTH_FACTOR + 1) + (float)WALL_WIDTH /2 + PANEL_WIDTH,
        .y = pos.y / (pos.z*RENDER_DEPTH_FACTOR + 1) + (float)WALL_HEIGHT/2,
    };
}

void draw_circle_fake3d (Vector3 center, float radius, Color color) {
    // printf("start draw circle 3d fake\n");
    DrawCircleV(
        screen(center),
        radius/(center.z*RENDER_DEPTH_FACTOR + 1),
        depth_color(color, center.z)
    );
}

void trajectory_render() {
    for (size_t i = 0; i < trajectory.count; ++i) {
        Trajectory t = trajectory.items[i];
        da_foreach(Vector3, p, &t) {
            draw_circle_fake3d(*p, 2, color.items[i]);
        }
    }
}

void bin_render(
    float bin[],
    Vector2 position, // bottom-left
    int width,
    int height
) {
    // int spacing = width / 40;
    int spacing = 2;
    int bin_width = (width - spacing * (BIN_COUNT - 1)) / BIN_COUNT;
    for (size_t i = 0; i < BIN_COUNT; ++i) {
        DrawRectangle(
            position.x + i * (spacing + bin_width),
            position.y - height*bin[i],
            bin_width,
            height*bin[i],
            WHITE
        );
    }
}

void render() {
    // particle -> raylib: DrawCircle
    // box      -> raylib: DrawRectangle
    render_sort();
    ClearBackground(BACKGROUND);
    da_foreach(size_t, index, &render_order) {
        size_t i = *index;
        if(!ENTITY_HAS(i, COMPONENT_VISIBLE)) continue;
        Vector3 pos = {Px.items[i], Py.items[i], Pz.items[i]};
        draw_circle_fake3d(pos, Radius.items[i], color.items[i]);
    }
    // Panel
    DrawText("Ideal Gas Simulator", 20, 20, 20, WHITE);
    DrawText(TextFormat("<V2> = %.2f", statistic.v_square_avg), 20, 50, 20, WHITE);
    DrawText(TextFormat("<V>  = %.2f", statistic.v_avg       ), 20, 80, 20, WHITE);
    bin_render( vel_bin, (Vector2) {20, 150}, PANEL_WIDTH, 300);
    bin_render(vel2_bin, (Vector2) {20, 240}, PANEL_WIDTH, 300);
    trajectory_render();
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ideal GAS in Raylib");
    // spawn entities
    spawn_random_particles(PARTICLE_NUM);
    SetTargetFPS(60);
    // SetMouseCursor(MOUSE_CURSOR_CROSSHAIR); // test for fun
    size_t frame = 0;
    while(!WindowShouldClose()) {
        BeginDrawing();
        // TODO: maybe add to event() later?
        if (IsKeyPressed(KEY_P)) pause = !pause;
        update();
        if (frame % 60 == 0) analysis();
        render();
        EndDrawing();
    }
    CloseWindow();
    
    da_free(Px);
    da_free(Py);
    da_free(Pz);
    da_free(Vx);
    da_free(Vy);
    da_free(Vz);
    da_free(color);
    da_free(render_order);
    return 0;
}
