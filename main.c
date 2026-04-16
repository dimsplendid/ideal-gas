#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <raylib.h>
#include <math.h>

#include "nob.h"

// Constant

#define G                   100

// like resource in Bevy
#define PARTICLE_NUM        100
#define PARTICLE_RADIUS     30
#define PARTICLE_MASS       100
#define MAX_VELOCITY        50

// initial random particle spread
#define SPREAD_RATIO        0.2

#define GRID_LEN            10
#define CELL_NUM            (GRID_LEN*GRID_LEN*GRID_LEN)
#define BIN_COUNT           20
#define TRAJECTORY_LEN      100
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

// boundary checked access element in dynamic array
#define da_at(da, i) (da)->items[(NOB_ASSERT((da)->count > i), i)]

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
Box wall = {
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
// using bitmask to track each entity components
typedef enum {
    COMPONENT_NONE       = 0u,
    COMPONENT_POSITION   = 1u << 0,
    COMPONENT_VELOCITY   = 1u << 1,
    COMPONENT_TRAJECTORY = 1u << 2,
    COMPONENT_GRAVITY    = 1u << 3,
    COMPONENT_COLLIDABLE = 1u << 4,
    COMPONENT_VISIBLE    = 1u << 5,
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
RealArr Ax     = {0};
RealArr Ay     = {0};
RealArr Az     = {0};
RealArr Vx     = {0};
RealArr Vy     = {0};
RealArr Vz     = {0};
RealArr Px     = {0};
RealArr Py     = {0};
RealArr Pz     = {0};
RealArr M      = {0};

struct {
    size_t *items;
    size_t count;
    size_t capacity;
} grid_id = {0}; // store each particle's located grid id

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
void grid_put(size_t id) {
    float cell_w = (float) WALL_WIDTH  / GRID_LEN;
    float cell_h = (float) WALL_HEIGHT / GRID_LEN;
    float cell_d = (float) WALL_DEPTH  / GRID_LEN;
    int i = (Px.items[id] - wall.min.x) / cell_w;
    int j = (Py.items[id] - wall.min.y) / cell_h;
    int k = (Pz.items[id] - wall.min.z) / cell_d;

    // out of bound modified
    if (i < 0) i = 0; else if (i >= GRID_LEN) i = GRID_LEN - 1;
    if (j < 0) j = 0; else if (j >= GRID_LEN) j = GRID_LEN - 1;
    if (k < 0) k = 0; else if (k >= GRID_LEN) k = GRID_LEN - 1;

    int gid = i * GRID_LEN * GRID_LEN + j * GRID_LEN + k;
    da_append(&grid[gid], id); // grid[gid] => particle id, reset every frame
    da_at(&grid_id, id) = gid; // grid_id[id] => grid id, update when position update
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

// particle entities initialize
// TODO: maybe change to "fix" speed at all direction(or a range of angle?) is better
void spawn_random_n_particles(size_t particle_numbers) {
    for (size_t id = 0; id < particle_numbers; ++id) {
        da_append(&Radius, PARTICLE_RADIUS);
        // da_append(&Px, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.x),(wall.max.x-PARTICLE_RADIUS)));
        // da_append(&Py, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.y),(wall.max.y-PARTICLE_RADIUS)));
        // da_append(&Pz, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.z),(wall.max.z-PARTICLE_RADIUS)));
        da_append(&Px, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.x)*SPREAD_RATIO,(wall.max.x-PARTICLE_RADIUS)*SPREAD_RATIO));
        da_append(&Py, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.y)*SPREAD_RATIO,(wall.max.y-PARTICLE_RADIUS)*SPREAD_RATIO));
        da_append(&Pz, (float)GetRandomValue((PARTICLE_RADIUS+wall.min.z)*SPREAD_RATIO,(wall.max.z-PARTICLE_RADIUS)*SPREAD_RATIO) + 10.0f);
        // da_append(&Vx, 0);
        // da_append(&Vy, 0);
        // da_append(&Vz, 0);
        da_append(&Vx, GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY));
        da_append(&Vy, GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY));
        da_append(&Vz, GetRandomValue(-MAX_VELOCITY, MAX_VELOCITY));
        da_append(&Ax, 0);
        da_append(&Ay, 0);
        da_append(&Az, 0);
        da_append(&M , PARTICLE_MASS);
        da_append(&color,((Color){
            .r = GetRandomValue(50, 255),
            .g = GetRandomValue(50, 255),
            .b = GetRandomValue(50, 255),
            .a = 255,
        }));
        da_append(&render_order, id);
        da_append(&entity, id);
        da_append(&grid_id, 0);
        grid_put(id);

        // Trajectory
        da_append(&trajectory, ((Trajectory) {0}));
        ComponentTag tag =
           COMPONENT_POSITION     |
           COMPONENT_VELOCITY     |
           COMPONENT_GRAVITY      |
           COMPONENT_COLLIDABLE   |
           COMPONENT_VISIBLE      |
           // COMPONENT_TRAJECTORY   |
           COMPONENT_NONE;

        // if (id == 0) {
        //     tag |= COMPONENT_TRAJECTORY | COMPONENT_VISIBLE;
        //     tag = tag & ~COMPONENT_VELOCITY; // TODO: change to something like view point track
        //     M.items[id] *= 10;
        //     Px.items[id] = 0;
        //     Py.items[id] = 0;
        //     Pz.items[id] = WALL_DEPTH/2;
        //     Vx.items[id] = 0;
        //     Vy.items[id] = 0;
        //     Vz.items[id] = 0;
        //     Radius.items[id] *= 3; // TODO: Modified collision
        //     color.items[id] = GetColor(0x00FF26FF);
        // }
        // if (id == 1) {
        //     tag |= COMPONENT_TRAJECTORY | COMPONENT_VISIBLE;
        //     color.items[id] = GetColor(0xFF2600FF);
        // }
        da_append(&component_tag, tag);
    }
    neighbor_cells_init();
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

bool is_in_screen(float x, float y, float z) {
    if (x > wall.max.x || x < wall.min.x) return false;
    if (y > wall.max.y || y < wall.min.y) return false;
    if (z > wall.max.z || z < wall.min.z) return false;
    return true;
}

void particle_render(size_t id) {
    // regist allowed component tags
    ComponentTag allow_tags = COMPONENT_VISIBLE;
    if(!ENTITY_HAS(id, allow_tags)) return;
    if (!is_in_screen(Px.items[id], Py.items[id], Pz.items[id])) return;
    Vector3 pos = {Px.items[id], Py.items[id], Pz.items[id]};
    draw_circle_fake3d(pos, Radius.items[id], color.items[id]);    
}

void trajectory_render(size_t id) {
    ComponentTag allow_tags = COMPONENT_TRAJECTORY; 
    if(!ENTITY_HAS(id, allow_tags)) return;
    int trajectory_dot_size = 1;
    Trajectory t = trajectory.items[id];
    da_foreach(Vector3, p, &t) {
        if (!is_in_screen(p->x, p->y, p->z)) continue;
        draw_circle_fake3d(*p, trajectory_dot_size, color.items[id]);
    }
}

// System

void event() {
    if (IsKeyPressed(KEY_P)) pause = !pause;
}

void perfect_elasstic_impact(size_t a, size_t b) {
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
void particle_collide_sys(size_t id1, char* option) {
    ComponentTag allow_tags =
        COMPONENT_POSITION   |
        COMPONENT_VELOCITY   |
        COMPONENT_COLLIDABLE |
        COMPONENT_NONE;
    if(!ENTITY_HAS(id1, allow_tags)) return;
    UNUSED(option);
    size_t self_cell_id = da_at(&grid_id,id1);
    CellId neighbor_cell = neighbor_cells[self_cell_id];
    for(size_t i = 0; i < neighbor_cell.count; ++i) {
        size_t other_cell_id = da_at(&neighbor_cell, i);
        Cell other_cell = grid[other_cell_id];
        for(size_t j = 0; j < other_cell.count; ++j) {
            size_t id2 = da_at(&other_cell, j);
            if (id1 <= id2) continue;
            perfect_elasstic_impact(id1, id2);
        }
    }   
}
void box_collide_sys(size_t id, Box *box) {
    ComponentTag allow_tags =
        COMPONENT_POSITION   |
        COMPONENT_VELOCITY   |
        COMPONENT_COLLIDABLE |
        COMPONENT_NONE;
    if(!ENTITY_HAS(id, allow_tags)) return;

    if ((Px.items[id]-PARTICLE_RADIUS < box->min.x && Vx.items[id] < 0) || (Px.items[id]+PARTICLE_RADIUS > box->max.x && Vx.items[id] > 0)) Vx.items[id] = -Vx.items[id];
    if ((Py.items[id]-PARTICLE_RADIUS < box->min.y && Vy.items[id] < 0) || (Py.items[id]+PARTICLE_RADIUS > box->max.y && Vy.items[id] > 0)) Vy.items[id] = -Vy.items[id];
    if ((Pz.items[id]-PARTICLE_RADIUS < box->min.z && Vz.items[id] < 0) || (Pz.items[id]+PARTICLE_RADIUS > box->max.z && Vz.items[id] > 0)) Vz.items[id] = -Vz.items[id];
}

void box_collide_sys_sys() {
    TODO("");
    // TODO("Add box merge?");
}

void movement_sys() {
    TODO("");    
}

void position_sys(size_t id, float dt) {
    ComponentTag allow_tags =
        COMPONENT_POSITION   |
        COMPONENT_VELOCITY   |
        // COMPONENT_GRAVITY    |
        // COMPONENT_COLLIDABLE |
        // COMPONENT_VISIBLE    |
        COMPONENT_NONE;
    if(!ENTITY_HAS(id, allow_tags)) return;
    Px.items[id] = Px.items[id] + Vx.items[id] * dt;
    Py.items[id] = Py.items[id] + Vy.items[id] * dt;
    Pz.items[id] = Pz.items[id] + Vz.items[id] * dt;
}
void velocity_sys(size_t id, float dt) {

    box_collide_sys(id, &wall);
    particle_collide_sys(id, NULL);
    
    // maybe add acceration
    Vx.items[id] = Vx.items[id] + Ax.items[id] * dt;
    Vy.items[id] = Vy.items[id] + Ay.items[id] * dt;
    Vz.items[id] = Vz.items[id] + Az.items[id] * dt;
}

void gravity_sys(size_t id1) {
    ComponentTag allow_tags =
        COMPONENT_POSITION   |
        COMPONENT_VELOCITY   |
        COMPONENT_GRAVITY    |
        COMPONENT_NONE;
    if(!ENTITY_HAS(id1, allow_tags)) return;
    for (size_t id2 = 0; id2 < entity.count; ++id2) {
        if (id1 == id2) continue;
        float P_21_x = Px.items[id2] - Px.items[id1];
        float P_21_y = Py.items[id2] - Py.items[id1];
        float P_21_z = Pz.items[id2] - Pz.items[id1];
        float r = sqrtf(P_21_x*P_21_x + P_21_y*P_21_y+P_21_z*P_21_z);
        float r3 = r * r * r;
        Ax.items[id1] +=  G * P_21_x / r3 * M.items[id2];
        Ay.items[id1] +=  G * P_21_y / r3 * M.items[id2];
        Az.items[id1] +=  G * P_21_z / r3 * M.items[id2];
     }
 }

void acceleration_sys(size_t id, float dt) {
    UNUSED(dt);
    Ax.items[id] = 0; 
    Ay.items[id] = 0; 
    Az.items[id] = 0; 
    gravity_sys(id);
}

size_t trajectory_count = 0;
size_t trajectory_len = TRAJECTORY_LEN;
size_t trajectory_objects = PARTICLE_NUM;
void trajectory_sys(size_t id, float dt) {
    return;
    // the logic is wrong, need think and rewrite
    UNUSED(dt); // maybe setting longer duration?
    ComponentTag allow_tags = COMPONENT_TRAJECTORY; 
    if (ENTITY_HAS(id, allow_tags)) {
        Trajectory *t = &trajectory.items[id];
        if (!is_in_screen(Px.items[id], Py.items[id], Pz.items[id])){
            component_tag.items[id] &= ~COMPONENT_TRAJECTORY;
            t->count = 0;
            trajectory_objects--;
            trajectory_len = (size_t)30000/trajectory_objects; // 30000 is just arbitrary pick for test
            // printf("trajectory_len: %zu\n", trajectory_len);
        }
        Vector3 pos = {Px.items[id], Py.items[id], Pz.items[id]};
        // ring buffer -> trajectory start with ((trajectory_count+1) % TRAJECTORY_LEN)
        if (trajectory_count < trajectory_len) {
            da_append(t, pos);
        } else {
            t->items[trajectory_count % TRAJECTORY_LEN] = pos;
        }
    } else {
        if (is_in_screen(Px.items[id], Py.items[id], Pz.items[id])){
            component_tag.items[id] |= COMPONENT_TRAJECTORY;
            trajectory_objects++;
            trajectory_len = (size_t)30000/trajectory_objects; // 30000 is just arbitrary pick for test
            // printf("trajectory_len: %zu\n", trajectory_len);
        }        
    }
}
// the entry point, all frame update
void GameFrame() {
    float dt = GetFrameTime();
    BeginDrawing();
    event();
    ClearBackground(BACKGROUND);
    
    // update through all entity
    if (dt < 0.1 && !pause) { // also pause when frame block
        for (size_t id = 0; id < entity.count; ++id) {
            position_sys  (id, dt);
            velocity_sys  (id, dt);
            acceleration_sys(id, dt); 
            trajectory_sys(id, dt);
        }
        trajectory_count++;

        // put particle in each grid again;
        grid_reset();
        for (size_t id = 0; id < entity.count; ++id)  grid_put(id);
        
        render_sort(); // maybe change to insertion sort after update?
    }
    
    // render through render order 
    for (size_t i = 0; i < render_order.count; ++i) {
        size_t id = da_at(&render_order, i);
        particle_render  (id);
        trajectory_render(id);
    }
    // Panel
    analysis(); // add some counter state later 
    DrawText("Ideal Gas Simulator", 20, 20, 20, WHITE);
    DrawText(TextFormat("<V2> = %.2f", statistic.v_square_avg), 20, 50, 20, WHITE);
    DrawText(TextFormat("<V>  = %.2f", statistic.v_avg       ), 20, 80, 20, WHITE);
    bin_render( vel_bin, (Vector2) {20, 150}, PANEL_WIDTH, 300);
    bin_render(vel2_bin, (Vector2) {20, 240}, PANEL_WIDTH, 300);
    
    // DrawText(TextFormat("<A0>  = %.2f", sqrtf(Ax.items[0]*Ax.items[0] + Ay.items[0]*Ay.items[0]+Az.items[0]*Az.items[0])), 20, 50, 20, WHITE);
    // DrawText(TextFormat("<A1>  = %.2f", sqrtf(Ax.items[1]*Ax.items[1] + Ay.items[1]*Ay.items[1]+Az.items[1]*Az.items[1])), 20, 80, 20, WHITE);
    EndDrawing();
}

// web cross platform using zozlib.js
void raylib_js_set_entry(void (*entry)(void));

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Ideal GAS in Raylib");
    // spawn entities
    spawn_random_n_particles(PARTICLE_NUM);
    SetTargetFPS(60);
    // SetMouseCursor(MOUSE_CURSOR_CROSSHAIR); // test for fun
#ifdef PLATFORM_WEB
    raylib_js_set_entry(GameFrame);
#else
    while(!WindowShouldClose()) {
        GameFrame();
    }
    CloseWindow();
#endif
    return 0;
}
