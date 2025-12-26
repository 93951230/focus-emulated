// ===preprocessor===
#pragma region 
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <allegro5/allegro_font.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_ttf.h>

#define true 1
#define false 0
#define KEY_SEEN 1
#define KEY_DOWN 2
#define MAX_MAP_W 500
#define MAX_MAP_H 500
#define WINDOW_W 1600
#define WINDOW_H 1200
#define FPS 60
#define TILE_SIZE 32
#define PLAYER_SIZE 20
#define FTHRES 0.01f
#define COYOTE_TIME 0.1f
#define FOCUS_MOVE_SPEED 150 // unit is pixel per second
#define MIN_PERC_FOCUS_REQ 0.3f
#define TELE_MOMENTUM 4
#define MAX_LEVEL 100
#define MAX_TARGET_CNT 5000
#define MAX_ANIM 50

#pragma endregion 

// ===global variables===
#pragma region 

ALLEGRO_TIMER* timer = NULL;
ALLEGRO_EVENT_QUEUE* queue = NULL;
ALLEGRO_DISPLAY* disp = NULL;
int info_font_height;
ALLEGRO_FONT* info_font = NULL;
int large_info_font_height;
ALLEGRO_FONT* large_info_font = NULL;
int title_font_height;
ALLEGRO_FONT* title_font = NULL;

unsigned char key[ALLEGRO_KEY_MAX];
float gravity_pull = 60*TILE_SIZE;
float drag_coefficient_y = 0.005f; // linear drag
float drag_coefficient_x = 0.05f;
bool has_gravity = true;
float player_walk_speed = 7*TILE_SIZE;
float player_jump_force = 40*TILE_SIZE;
int curr_level = 0;
bool show_tutorial = false;
char tutorial_text[100];
bool debug = false;
bool play_audio = true;
bool stage_clear = false;
int death_cnt = 0;

ALLEGRO_BITMAP* brick_wall_0;
ALLEGRO_BITMAP* brick_wall_1;
ALLEGRO_BITMAP* slab;
ALLEGRO_BITMAP* player_idle_0;
ALLEGRO_BITMAP* t_background;
ALLEGRO_BITMAP* spike;
ALLEGRO_BITMAP* target_block_on;
ALLEGRO_BITMAP* target_block_off;
ALLEGRO_BITMAP* sign;
ALLEGRO_BITMAP* ice;
ALLEGRO_BITMAP* crystal;

ALLEGRO_SAMPLE* charging;
ALLEGRO_SAMPLE_ID charging_id;
bool charging_playing = false;
ALLEGRO_SAMPLE* ding;
ALLEGRO_SAMPLE* teleport_pew;
ALLEGRO_SAMPLE* select_ding;
ALLEGRO_SAMPLE* death_buzz;

char title_screen_options[4][30] = {
    "START","Game Play Tutorial","Level Editing Tutorial","Exit"
};
int option_count;

char standable_tile[] = {
    '1','2','3','7','8','_','a'
};
char collidable_tile[] = {
    '1','2','7','8','_','a'
};
#pragma endregion

// ===helpers (vectors/rigid body/etc, filep. methods)===
#pragma region

// ---misc---
#pragma region
void must_init(bool test, const char *description) {
    if(test) return;

    fprintf(stderr,"[Error] Couldn't initialize %s\n", description);
    exit(1);
}
int min(int a,int b) {
    return (a>b)?b:a;
}
int max(int a,int b) {
    return (a<b)?b:a;
}

typedef enum {
    NONE,
    UP,
    DOWN,
    LEFT,
    RIGHT
} DIRE;

int between(int lo, int hi) {
    return lo + (rand() % (hi - lo));
}

float between_f(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

void al_draw_circle_dashed(
    float cx, float cy, float r, ALLEGRO_COLOR color,
    float thickness, float l_fill, float l_vacant, float offset
) {
    float l_total = l_fill + l_vacant;
    for (float angle = 0; angle < 2 * M_PI; angle += l_total) {
        al_draw_arc(cx, cy, r, angle + offset, l_fill, color, thickness);
    }
}

float positive_mod(float x, float y) {
    return fmod(fmod(x, y) + y, y);
}

#pragma endregion

// ---vector---
    #pragma region
typedef struct {
    float x;
    float y;
} Vector;

static inline Vector vec_new(float x, float y) {
    Vector v;
    v.x = x;
    v.y = y;
    return v;
}

static inline Vector vec_add(Vector a, Vector b) {
    return vec_new(a.x + b.x, a.y + b.y);
}

static inline Vector vec_sub(Vector a, Vector b) {
    return vec_new(a.x - b.x, a.y - b.y);
}

static inline Vector vec_mul(Vector v, float s) {
    return vec_new(v.x * s, v.y * s);
}

static inline Vector vec_div(Vector v, float s) {
    return vec_new(v.x / s, v.y / s);
}

static inline float vec_dot(Vector a, Vector b) {
    return a.x * b.x + a.y * b.y;
}

static inline float vec_len_sq(Vector v) {
    return v.x * v.x + v.y * v.y;
}

static inline float vec_len(Vector v) {
    return sqrt(vec_len_sq(v));
}

static inline Vector vec_normalize(Vector v) {
    float length = vec_len(v);
    if (length > 0.0) {
        return vec_div(v, length);
    }
    return vec_new(0.0, 0.0);
}

static inline Vector vec_rotate(Vector v, float radians) {
    float c = cos(radians);
    float s = sin(radians);
    return vec_new(
        v.x * c - v.y * s,
        v.x * s + v.y * c
    );
}
#pragma endregion

// ---static rect---
    #pragma region
typedef struct {
    Vector pos;
    Vector size;
} StaticRect; // used for tile

#pragma endregion

// ---rect---
    #pragma region
typedef struct {
    Vector pos;
    Vector size;
    Vector velocity;
} Rect; // used for player, missilez

#pragma endregion

bool overlap(const Vector posA, const Vector sizeA, const Vector posB, const Vector sizeB) {
    return !(posA.x + sizeA.x <= posB.x || 
             posB.x + sizeB.x <= posA.x || 
             posA.y + sizeA.y <= posB.y || 
             posB.y + sizeB.y <= posA.y);
}

#pragma endregion

// ===level handler===
#pragma region

typedef struct {
    Vector* target_pos;
    Vector player_pos;
    float scale;
    int width;
    int height;
    int target_cnt;
    char* data;
    char** map;
    float focus;
    float focus_rate_inc;
    float focus_rate_dec;
    bool can_tele_outside;
} Level;

void level_load(Level* lvl, char dire[500], bool tell_info) {
    FILE* file = fopen(dire,"r");

    if (!file) {
        fprintf(stderr, "[Error] Unable to open %s\n",dire);
        exit(1);
    }

    int w, h;
    if (fscanf(file, "%d %d", &w, &h) != 2) {
        fprintf(stderr, "[Error] Can't read dimensions.\n");
        exit(1);
    }
    if (h <= 0 || w <= 0 || h > MAX_MAP_H || w > MAX_MAP_W) {
        fprintf(stderr, "[Error] Invalid map dimensions: %d, %d\n", w, h);
        exit(1);
    }
    lvl->width = w;
    lvl->height = h;
    if (fscanf(file,"%f",&lvl->focus) != 1) {
        fprintf(stderr, "[Error] Can't read focus.\n");
        exit(1);
    }
    if (fscanf(file,"%f %f",&lvl->focus_rate_dec,&lvl->focus_rate_inc) != 2) {
        fprintf(stderr, "[Error] Can't read focus_rate_dec/focus_rate_inc\n");
        exit(1);
    }
    if (lvl->focus < 0 || lvl->focus_rate_inc < 0 || lvl->focus_rate_dec < 0) {
        fprintf(stderr, "[Error] Invalid focus setting: %f, %f\n", lvl->focus, lvl->focus_rate_inc);
        exit(1);
    }
    if (fscanf(file,"%f",&lvl->scale) != 1) {
        fprintf(stderr, "[Error] Can't read scale.\n");
        exit(1);
    }
    if (lvl->scale <= 0) {
        fprintf(stderr, "[Error] Invalid scale setting: %f\n", lvl->scale);
        exit(1);
    }
    int temp = 0;
    if (fscanf(file,"%d",&temp) != 1) {
        fprintf(stderr, "[Error] Can't read scale.\n");
        exit(1);
    }
    lvl->can_tele_outside = temp != 0;
    fgetc(file);
    
    lvl->data = (char*)malloc(sizeof(char)*(w*h));
    lvl->map = (char**)malloc(sizeof(char*)*h);
    for (int i=0;i<h;i++) {
        lvl->map[i] = &(lvl->data[i*w]);
    }

    char* buf = malloc((size_t)w + 4);  // +1 for newline, +1 for '\0'
    if (!buf) {
        perror("malloc");
        free(lvl->map);
        free(lvl->data);
        fclose(file);
        exit(1);
    }

    for (int i = 0; i < h; i++) {
        if (!fgets(buf, w + 4, file)) {
            fprintf(stderr, "[Error] Can't read map row %d (expected %d chars).\n", i, w);
            free(buf);
            exit(1);
        }
        size_t len = strcspn(buf, "\r\n");  // actual data length before newline
        if (len < (size_t)w) {
            fprintf(stderr, "[Error] Row %d is too short (%zu < %d).\n", i, len, w);
            free(buf);
            exit(1);
        }
        /* copy exactly w characters */
        memcpy(lvl->map[i], buf, (size_t)w);
    }
    free(buf);

    lvl->player_pos.x = -1;
    lvl->player_pos.y = -1;
    lvl->target_cnt = 0;
    lvl->target_pos = (Vector*)malloc(sizeof(Vector)*MAX_TARGET_CNT);
    for (int i=0;i<h;i++) {
        for (int j=0;j<w;j++) {
            if (lvl->map[i][j] == 'p') {
                Vector pos = {j,i};
                lvl->player_pos = pos;
                break;
            }
            if (lvl->map[i][j] == '7') {
                lvl->target_pos[lvl->target_cnt++] = vec_new(j,i);
            }
        }
    }
    if (lvl->player_pos.x == -1 || lvl->player_pos.y == -1) {
        if (tell_info) printf("Can't find initial player position (character p), set it to the mid.\n");
        Vector pos = {lvl->width/2,lvl->height/2};
        lvl->player_pos = pos;
    }
    
    fclose(file);

    if (tell_info) {
        printf("================================================\n");
        printf("Successfully loaded %s, the info being\n",dire);
        printf("width = %d, height = %d\n",lvl->width,lvl->height);
        printf("focus = %f\n",lvl->focus);
        printf("focus inc = %f, focus dec = %f\n",lvl->focus_rate_inc, lvl->focus_rate_dec);
        printf("player position = (%f,%f)\n",lvl->player_pos.x,lvl->player_pos.y);
        for (int i=0;i<lvl->height;i++) {
            for (int j=0;j<lvl->width;j++) {
                printf("%c",lvl->map[i][j]);
            }
            printf("\n");
        }
        printf("================================================\n");
    }
}

void level_free(Level* lvl) {
    free(lvl->target_pos);
    free(lvl->map);
    free(lvl->data);
}

void level_draw(Level* lvl,Vector deviation,float now) {
    for (int i=0;i<lvl->height;i++) {
        for (int j=0;j<lvl->width;j++) {
            float x = j*TILE_SIZE*lvl->scale + deviation.x;
            float y = i*TILE_SIZE*lvl->scale + deviation.y;
            float wid = TILE_SIZE*lvl->scale;
            float hei = TILE_SIZE*lvl->scale;

            switch (lvl->map[i][j]) {
                case '0':
                    // air
                    break;
                case '1':
                    // brick_wall_0
                    al_draw_scaled_bitmap(brick_wall_0, 0, 0,1200,1200,x,y,wid,hei,0);
                    break;
                case '2':
                    // brick_wall_1
                    al_draw_scaled_bitmap(brick_wall_1, 0, 0,610,610,x,y,wid,hei,0);
                    break;
                case '3':
                    // slab
                    al_draw_scaled_bitmap(slab, 0, 0,16,8,x,y,wid,hei/2,0);
                    break;
                case '4':
                    // basic turret
                    break;
                case '5':
                    // homing turret
                    break;
                case '6':
                    // fast turret
                    break;
                case '_':
                    // target_block_off
                    al_draw_scaled_bitmap(target_block_off,0,0,16,16,x,y,wid,hei,0);
                    break;
                case '8':
                    // ice
                    al_draw_scaled_bitmap(ice,0,0,16,16,x,y,wid,hei,0);
                    break;
                case '9':
                    // crystal
                    al_draw_scaled_bitmap(crystal,0,0,160,160,x,y,wid,hei,0);
                    break;
                case 'a':
                    // spike
                    al_draw_scaled_bitmap(spike,0,0,160,160,x,y,wid,hei,0);
                    break;
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                case 'E':
                case 'F':
                case 'G':
                    // sign
                    al_draw_scaled_bitmap(sign,0,0,16,16,x,y,wid,hei,0);
                    break;
            }
        }
    }
    // spcifically draw targets, avoid light overlap w/ ordinary tile
    for (int n = 0;n<lvl->target_cnt;n++) {
        int i = lvl->target_pos[n].y;
        int j = lvl->target_pos[n].x;
        if (lvl->map[i][j] == '_') continue; // is off, no draw
        // avoid light overlap w/ other targets
        bool left_target = (i < 0 || lvl->map[i][j-1] == '7'); // is left neighbor a target?
        bool up_target = (i < 0 || lvl->map[i-1][j] == '7'); // same

        float diff = 5;
        float ano_x = (j*TILE_SIZE-(left_target?0:diff))*lvl->scale + deviation.x;
        float ano_y = (i*TILE_SIZE-(up_target?0:diff))*lvl->scale + deviation.y;
        float ano_wid = (TILE_SIZE+(left_target?diff:2*diff))*lvl->scale;
        float ano_hei = (TILE_SIZE+(up_target?diff:2*diff))*lvl->scale;
        al_draw_filled_rectangle(ano_x,ano_y,ano_x+ano_wid,ano_y+ano_hei,al_map_rgba(207, 167, 73,100)); // rgb(207, 167, 73)
        
        float x = j*TILE_SIZE*lvl->scale + deviation.x;
        float y = i*TILE_SIZE*lvl->scale + deviation.y;
        float wid = TILE_SIZE*lvl->scale;
        float hei = TILE_SIZE*lvl->scale;
        al_draw_scaled_bitmap(target_block_on,0,0,16,16,x,y,wid,hei,0);
    }
}

Vector calculate_deviation(Level* lvl) {
    Vector ans;
    ans.x = (WINDOW_W-(lvl->scale*lvl->width*TILE_SIZE))/2;
    ans.y = (WINDOW_H-(lvl->scale*lvl->height*TILE_SIZE))/2;
    return ans;
}

void sequential_level_load(Level levels[MAX_LEVEL], int* total_level) {
    FILE* file = fopen("lvl_config.txt", "r");
    if (!file) {
        fprintf(stderr, "[Error] Can't find lvl_config.txt\n");
        exit(0);
    }

    int total = 0;
    if (fscanf(file, "%d", &total) != 1) {
        fprintf(stderr, "[Error] Can't read total amount of levels\n");
        fclose(file);
        exit(0);
    }
    *total_level = total;

    char buf[500];
    for (int i = 0; i < total; i++) {
        if (fscanf(file, "%499s", buf) != 1) {
            fprintf(stderr, "[Error] Can't read %d th level\n", i);
            fclose(file);
            exit(0);
        }

        char dire[500];
        strcpy(dire, "level/");

        int base = (int)strlen(dire); // 6
        int j = 0;
        for (; buf[j] != '\0' && buf[j] != '\n'; j++) {
            if (base + j >= (int)sizeof(dire) - 1) break; // avoid overflow
            dire[base + j] = buf[j];
        }
        dire[base + j] = '\0'; // IMPORTANT

        printf("Reading %s\n", dire);
        level_load(&levels[i], dire, false);
    }

    fclose(file);
}

bool is_standable(char c) {
    for (int i=0;(size_t)i<sizeof(standable_tile);i++) {
        if (standable_tile[i] == c) return true;
    }
    return false;
}

bool is_collidable(char c) {
    for (int i=0;(size_t)i<sizeof(collidable_tile);i++) {
        if (collidable_tile[i] == c) return true;
    }
    return false;
}

#pragma endregion

// ===game objects===
#pragma region

// ---player---
#pragma region
typedef enum { // for animation
    PLAYER_IDLE,     
    PLAYER_MID_AIR,  
    PLAYER_FOCUSING,
    PLAYER_WALKING
} PlayerStatus;

typedef struct {
    Vector focus_target;
    Vector crystal_provider;
    Vector standing_on;
    PlayerStatus status;
    Rect rect;
    int walk_dir;
    int target_hit;
    float focus;
    float walk_speed;
    float coyote_timer;
    bool grounded;
    bool focusing;
    bool can_refocus;
    bool near_crystal;
    char under;   
} Player;

Player player_new(Level* lvl) {
    Player player;
    player.focus_target = vec_new(0,0);
    player.crystal_provider = vec_new(0,0);
    player.standing_on = vec_new(0,0);
    player.status = PLAYER_IDLE;
    player.rect.pos = vec_new(lvl->player_pos.x*TILE_SIZE,lvl->player_pos.y*TILE_SIZE);
    player.rect.size = vec_new(PLAYER_SIZE,PLAYER_SIZE);
    player.rect.velocity = vec_new(0,0);
    player.walk_dir = 0;
    player.target_hit = 0;
    player.focus = lvl->focus;
    player.walk_speed = player_walk_speed;
    player.grounded = false;
    player.focusing = false;
    player.can_refocus = true;
    player.near_crystal = false;
    player.under = '0';
    return player;
}

DIRE player_resolve_collision(Player* player, Vector pos, Vector size) {
    Vector delta = {
        (player->rect.pos.x + PLAYER_SIZE / 2) - (pos.x + size.x / 2),
        (player->rect.pos.y + PLAYER_SIZE / 2) - (pos.y + size.y / 2)
    };
    Vector overlap_part = {
        (PLAYER_SIZE / 2 + size.x / 2) - fabs(delta.x),
        (PLAYER_SIZE / 2 + size.y / 2) - fabs(delta.y)
    };

    DIRE direction = NONE;
    if (overlap_part.x <= FTHRES || overlap_part.y <= FTHRES)
        return NONE;
    
    if (overlap_part.x < overlap_part.y) {
        // Push horizontally
        if (delta.x > FTHRES) {
            player->rect.pos.x += overlap_part.x;
            direction = LEFT;
        }
        else {
            player->rect.pos.x -= overlap_part.x;
            direction = RIGHT;
        }
        player->rect.velocity.x = 0;
    } 
    else {
        // Push vertically
        if (delta.y > FTHRES) {
            
            player->rect.pos.y += overlap_part.y;
            direction = UP;
        }
        else {
            player->rect.pos.y -= overlap_part.y;
            direction = DOWN;
        }
        player->rect.velocity.y = 0;
    }
    return direction;
}

void player_check_and_resolve_collision(Player* player, Level* lvl, Vector deviation ,float delta_time) {
    float x = player->rect.pos.x, y = player->rect.pos.y;
    int min_tx = floor(x / TILE_SIZE);
    int max_tx = floor((x + PLAYER_SIZE - 1) / TILE_SIZE);
    int min_ty = floor(y / TILE_SIZE);
    int max_ty = floor((y + PLAYER_SIZE - 1) / TILE_SIZE);

    for (int i = max(0, min_ty); i <= min(lvl->height - 1, max_ty); i++) {
        for (int j = max(0, min_tx); j <= min(lvl->width - 1, max_tx); j++) {
            if (!is_collidable(lvl->map[i][j])) continue;

            float tx = j * TILE_SIZE;
            float ty = i * TILE_SIZE;
            Rect tile = {
                .pos = {tx, ty},
                .size = {TILE_SIZE, TILE_SIZE}
            };

            if (overlap(player->rect.pos, player->rect.size, tile.pos, tile.size)) {
                if (player_resolve_collision(player, tile.pos, tile.size) == DOWN) {
                    player->status = PLAYER_IDLE;
                }
            }

            if (debug) {
            float draw_x = j * TILE_SIZE * lvl->scale + deviation.x;
            float draw_y = i * TILE_SIZE * lvl->scale + deviation.y;
            float draw_w = TILE_SIZE * lvl->scale;
            float draw_h = TILE_SIZE * lvl->scale;
            al_draw_rectangle(draw_x, draw_y, draw_x + draw_w, draw_y + draw_h, al_map_rgb(255, 0, 0), lvl->scale);
            }
        }
    }
}

char player_block_underneath(Player* player, Level* lvl) {
    float probeY = player->rect.pos.y + PLAYER_SIZE + FTHRES;
    int ty = floor(probeY / TILE_SIZE);

    int tx0 = floor(player->rect.pos.x / TILE_SIZE);
    int tx1 = floor((player->rect.pos.x + PLAYER_SIZE - 1) / TILE_SIZE);

    if (ty < 0 || ty >= lvl->height) return '0';

    for (int tx = tx0; tx <= tx1; tx++) {
        if (tx < 0 || tx >= lvl->width) continue;
        char c = lvl->map[ty][tx];
        if (is_standable(c)) {
            player->standing_on = vec_new(tx,ty);
            return c;
        }
    }
    return '0';
}

Vector player_initial_pos(Player* player); // forward

bool player_has_crystal_nearby(Player* player, Level* lvl, Vector deviation) {
    int lower_x = max(floor(player->rect.pos.x-player->focus*TILE_SIZE)/TILE_SIZE,0);
    int upper_x = min(floor(player->rect.pos.x+player->focus*TILE_SIZE)/TILE_SIZE,lvl->width-1);
    int lower_y = max(floor(player->rect.pos.y-player->focus*TILE_SIZE)/TILE_SIZE,0);
    int upper_y = min(floor(player->rect.pos.y+player->focus*TILE_SIZE)/TILE_SIZE,lvl->height-1);

    for (int i=lower_y;i<=upper_y;i++) {
        for (int j=lower_x;j<=upper_x;j++) {
            Vector dif = vec_sub(vec_mul(vec_new(j + 0.5f,i + 0.5f),TILE_SIZE),player_initial_pos(player));
            if (vec_len_sq(dif) > player->focus*player->focus*TILE_SIZE*TILE_SIZE) continue;
            if (lvl->map[i][j] == '9') {// is crystal
                player->crystal_provider = vec_new(j,i);
                return true;
            }
            
            if (debug) {
                float draw_x = TILE_SIZE*lvl->scale*j + deviation.x;
                float draw_y = TILE_SIZE*lvl->scale*i + deviation.y;
                float draw_wid = TILE_SIZE*lvl->scale;
                float draw_hei = TILE_SIZE*lvl->scale;
                al_draw_rectangle(draw_x,draw_y,draw_x+draw_wid,draw_y+draw_hei,al_map_rgb(255, 0, 0),lvl->scale);
            }
        }
    }

    return false;

}

void player_attempt_teleport(Player *player, Level *lvl);

void player_kill(Player *player, Level *lvl) {
    *player = player_new(lvl);

    for (int i=0;i<lvl->target_cnt;i++) {
        int x = lvl->target_pos[i].x;
        int y = lvl->target_pos[i].y;
        lvl->map[y][x] = '7';
    }

    if (play_audio) al_play_sample(death_buzz,0.6,0,1.0,ALLEGRO_PLAYMODE_ONCE,NULL);
    death_cnt++;
}

void player_update(Player* player, Level* lvl, Vector deviation,float delta_time) {
    // # detect target hit
    if (player->under == '7') {
        player->target_hit++;
        int sx = floor(player->standing_on.x);
        int sy = floor(player->standing_on.y);
        lvl->map[sy][sx] = '_';

        if (play_audio) al_play_sample(ding, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE,0);

        if (player->target_hit >= lvl->target_cnt) {
            stage_clear = true;
        }
    }
    // # detect spike
    if (player->under == 'a') {
        player_kill(player,lvl);
    }
    // # move 
    if (player->walk_dir) {
        player->rect.velocity.x = (player->walk_dir)*(player->under == '8'?2.0f:1.f)*player->walk_speed;
    }
    // # update focus
    if (lvl->focus == 0.f) player->can_refocus = false;
    else {
        if (player->focus < lvl->focus*MIN_PERC_FOCUS_REQ) {
            if (player->focusing) player_attempt_teleport(player,lvl);
            player->focusing = false;
            player->can_refocus = false;
        }
        else {
            player->can_refocus = true;
        }
        player->focus = fmax(0.5,player->focus);
        player->near_crystal = player_has_crystal_nearby(player,lvl,deviation);
        if (player->near_crystal) {
            player->focus = fmin(lvl->focus, player->focus + delta_time * 5);
        }
        else {
            if (player->focusing) {
                player->focus -= delta_time * lvl->focus_rate_dec;
            }
            else {
                player->focus = fmin(lvl->focus, player->focus + delta_time * lvl->focus_rate_inc);
            }
        }
    }
    // # handle jump avability
    if (player->grounded) {
        player->coyote_timer = COYOTE_TIME;
    } 
    else if (player->coyote_timer > 0.0f) {
        player->coyote_timer -= delta_time;  // delta_time = time since last frame
    }
    // # clip player back
    float margin = 3;
    float draw_x = lvl->scale*player->rect.pos.x + deviation.x;
    float draw_y = lvl->scale*player->rect.pos.y + deviation.y;
    float draw_wid = PLAYER_SIZE*lvl->scale;
    float draw_hei = PLAYER_SIZE*lvl->scale;
    if (draw_x < -margin*draw_wid) {
        player->rect.pos.x = (WINDOW_W-deviation.x)/lvl->scale;
    }
    if (draw_x > WINDOW_W + margin*draw_wid) {
        player->rect.pos.x = -deviation.x/lvl->scale;
    }
    if (draw_y < -margin*draw_hei) {
        player->rect.pos.y = (WINDOW_H-deviation.y)/lvl->scale;
    }
    if (draw_y > WINDOW_H + margin*draw_hei) {
        player->rect.pos.y = -deviation.y/lvl->scale;
    }
    // # move
    if (player->focusing) delta_time *= 0.1;
    Vector v = player->rect.velocity;
    float speed_sq = vec_dot(v, v);
    Vector inv = vec_mul(vec_normalize(v),-1);
    Vector drag = {
        inv.x * drag_coefficient_x * speed_sq,
        inv.y * drag_coefficient_y * speed_sq
    };

    if (player->status == PLAYER_IDLE) {
        if (player->under == '8' || !player->grounded) drag.x *= 0.1;
        player->rect.velocity.x += drag.x * delta_time;
        
    }
    player->rect.velocity.y += drag.y * delta_time;

    if (player->grounded && player->rect.velocity.y > 0)
        player->rect.velocity.y = 0;
    else
        player->rect.velocity.y += (has_gravity?gravity_pull:0.) * delta_time;


    player->under = player_block_underneath(player, lvl);
    player->grounded = player->under != '0';

    // detect tutorial block
    int px = (int)floor((player->rect.pos.x + PLAYER_SIZE/2) / TILE_SIZE);
    int py =(int)floor((player->rect.pos.y + PLAYER_SIZE/2) / TILE_SIZE);
    show_tutorial = false;
    if (px >= 0 && px < lvl->width && py >= 0 && py < lvl->height) {
        char block = lvl->map[py][px];
        switch (block) {
            case 'A':
                strcpy(tutorial_text, "Use [Left/Right Arrow] to move.");
                show_tutorial = true;
                break;
            case 'B':
                strcpy(tutorial_text, "Try jumping with [Up Arrow].");
                show_tutorial = true;
                break;
            case 'C':
                strcpy(tutorial_text, "Step on *all the* glowing lamp to clear stage.");
                show_tutorial = true;
                break;
            case 'D':
                strcpy(tutorial_text, "Watch out for the spikes down below!!");
                show_tutorial = true;
                break;
            case 'E':
                strcpy(tutorial_text, "Press [A] to focus and teleport!");
                show_tutorial = true;
                break;
            case 'F':
                strcpy(tutorial_text, "Use focus wisely.");
                show_tutorial = true;
                break;
            case 'G':
                strcpy(tutorial_text, "You are free to go. The challenge awaits.");
                show_tutorial = true;
                break;
            // Add more cases as needed
            default:
                break;
        }
    }

    const int STEPS = 30;
    Vector stepX = vec_new(delta_time * player->rect.velocity.x / STEPS, 0);
    Vector stepY = vec_new(0, delta_time * player->rect.velocity.y / STEPS);


    // # slide
    for (int s = 0; s < STEPS; s++) {
        player->rect.pos = vec_add(player->rect.pos, stepX);
        player_check_and_resolve_collision(player, lvl, deviation, delta_time);
    }

    for (int s = 0; s < STEPS; s++) {
        player->rect.pos = vec_add(player->rect.pos, stepY);
        player_check_and_resolve_collision(player, lvl, deviation, delta_time);
        if (player->grounded) {
            stepY.y = 0;

            int tile_row = floor((player->rect.pos.y + PLAYER_SIZE) / TILE_SIZE); // damn jitter
            player->rect.pos.y = tile_row * TILE_SIZE - PLAYER_SIZE;
            break; 
        }
    }
    player->under = player_block_underneath(player, lvl);
    player->grounded = player->under != '0';
}

Vector player_initial_pos(Player* player) {
    return vec_new(player->rect.pos.x + PLAYER_SIZE/2, player->rect.pos.y + PLAYER_SIZE/2);
}

float player_calculate_focus_decay(Player* player, Level* lvl) {
    float ans = player->focus;
    // initial cut
    ans -= lvl->focus*MIN_PERC_FOCUS_REQ;

    // linear drop according how much you use
    Vector orig = player_initial_pos(player);
    Vector dif = vec_sub(player->focus_target,orig);
    float exhaust_ratio = vec_len(dif)/player->focus/TILE_SIZE;
    ans *= (1+MIN_PERC_FOCUS_REQ-exhaust_ratio);

    return ans;
}

void player_cap_focus_target(Player* player) {
    float r = TILE_SIZE * player->focus;
    Vector orig = player_initial_pos(player);
    Vector dif = vec_sub(player->focus_target,orig);
    float len = vec_len_sq(dif);
    if (len <= r*r) return;
    
    player->focus_target = vec_add(orig,vec_mul(vec_normalize(dif),r));
}

bool player_can_teleport(Player* player, Level* lvl) {
    if (!player->can_refocus) return false;
    int tx = floor(player->focus_target.x / TILE_SIZE);
    int ty = floor(player->focus_target.y / TILE_SIZE);

    if (tx >= 0 && tx < lvl->width && ty >= 0 && ty < lvl->height) {
        return !is_standable(lvl->map[ty][tx]);
    }
    return lvl->can_tele_outside;
}

void player_draw(Player* player,Level* lvl,Vector deviation,float now) {
    float x = lvl->scale*player->rect.pos.x + deviation.x;
    float y = lvl->scale*player->rect.pos.y + deviation.y;
    float wid = PLAYER_SIZE*lvl->scale;
    float hei = PLAYER_SIZE*lvl->scale;

    float circ_x = x+lvl->scale*PLAYER_SIZE/2;
    float circ_y = y+lvl->scale*PLAYER_SIZE/2;
    float r = player->focus*lvl->scale*TILE_SIZE;
    float circ_line_width = 5;

    al_draw_scaled_bitmap(player_idle_0,0,0,1200,1200,x,y,wid,hei,0);

    if (lvl->focus == 0.f) return;

    float offset = 0.8*fmod(al_get_time(), 2 * M_PI);
    if (player->focusing) {
        offset *= 2;
    }

    al_draw_circle_dashed(circ_x,circ_y,r*1.2,al_map_rgba(100,120,170,100),circ_line_width*lvl->scale,M_PI/16,M_PI/32,offset);

    ALLEGRO_COLOR focus_color = al_map_rgba(40,150,200,220); // "rgb(40,150,200)"
    ALLEGRO_COLOR focus_color_dark = al_map_rgba(40,150,200,220); // "rgb(5, 102, 146)"
    if (player->near_crystal) {
        focus_color =  al_map_rgba(210, 79, 250,220); // "rgb(210,79,250)"
        focus_color_dark =  al_map_rgba(210, 79, 250,220); // "rgb(134, 41, 162)"
        circ_line_width *= 1.5;
        if (play_audio && !charging_playing) {
            al_play_sample(charging, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_LOOP, &charging_id);
            charging_playing = true;
        }
    }
    else {
        if (charging_playing) {
            al_stop_sample(&charging_id);
            charging_playing = false;
        }
    }
    if (!player->can_refocus) {
        focus_color = focus_color_dark = al_map_rgba(160,70,100,100);
    }
    al_draw_circle(circ_x,circ_y,r,focus_color_dark,circ_line_width);
    al_draw_circle(circ_x,circ_y,r,focus_color,circ_line_width*3);
    if (player->focusing) {
        // draw target
        player_cap_focus_target(player);
        float draw_t_x = lvl->scale*player->focus_target.x + deviation.x;
        float draw_t_y = lvl->scale*player->focus_target.y + deviation.y;
        al_draw_circle(draw_t_x,draw_t_y,10,focus_color,circ_line_width);
        float gap = 10;
        float len = 20;
        ALLEGRO_COLOR cross_color = al_map_rgba(20,100,240,220);
        al_draw_line(draw_t_x, draw_t_y - gap, draw_t_x, draw_t_y - gap - len, cross_color, circ_line_width);
        al_draw_line(draw_t_x, draw_t_y + gap, draw_t_x, draw_t_y + gap + len, cross_color, circ_line_width);
        al_draw_line(draw_t_x - gap, draw_t_y, draw_t_x - gap - len, draw_t_y, cross_color, circ_line_width);
        al_draw_line(draw_t_x + gap, draw_t_y, draw_t_x + gap + len, draw_t_y, cross_color, circ_line_width);
        

        // draw estimation
        ALLEGRO_COLOR esti_color = focus_color;
        if (!player_can_teleport(player,lvl)) {
            esti_color = al_map_rgba(160,70,100,100);
        }
        
        int tx = floor(player->focus_target.x / TILE_SIZE);
        int ty = floor(player->focus_target.y / TILE_SIZE);
        int draw_target_tile_x = tx*TILE_SIZE*lvl->scale + deviation.x;
        int draw_target_tile_y = ty*TILE_SIZE*lvl->scale + deviation.y;
        if (debug) {    
            if (tx >= 0 && tx < lvl->width && ty >= 0 && ty < lvl->height) {
                al_draw_rectangle(draw_target_tile_x,draw_target_tile_y,
                draw_target_tile_x+TILE_SIZE*lvl->scale,draw_target_tile_y+TILE_SIZE*lvl->scale,
                al_map_rgb(255, 0, 0), lvl->scale); // draw hitbox
            }
        }

        float decay_r = player_calculate_focus_decay(player,lvl) * TILE_SIZE * lvl->scale;
        al_draw_circle(draw_t_x,draw_t_y,decay_r,esti_color,circ_line_width);
    }

    if (player->near_crystal) {
        al_draw_circle_dashed(
            circ_x,circ_y,
            PLAYER_SIZE*lvl->scale*2,focus_color,circ_line_width*lvl->scale,M_PI/16,M_PI/32,offset);
        al_draw_circle_dashed(
            (player->crystal_provider.x+0.5f)*TILE_SIZE*lvl->scale + deviation.x,
            (player->crystal_provider.y+0.5f)*TILE_SIZE*lvl->scale + deviation.y,
            PLAYER_SIZE*lvl->scale*2,focus_color,circ_line_width*lvl->scale,M_PI/16,M_PI/32,-offset);
    }

    if (debug) { 
        al_draw_rectangle(x,y,x+wid,y+hei, al_map_rgb(255, 0, 0), lvl->scale); // draw hitbox
    } 
}

void player_attempt_teleport(Player* player,Level* lvl) {
    if (!player_can_teleport(player,lvl)) return;
    Vector orig = player_initial_pos(player);
    player->focus = player_calculate_focus_decay(player,lvl);
    player->rect.pos = vec_sub(player->focus_target,vec_new(PLAYER_SIZE/2,PLAYER_SIZE/2));

    // play audio
    if (play_audio) al_play_sample(teleport_pew, 2.0, 0.0, 0.5, ALLEGRO_PLAYMODE_ONCE, 0);

    // add momentum
    Vector dif = vec_sub(player->focus_target,orig);
    player->rect.velocity = vec_add(player->rect.velocity,vec_mul(vec_normalize(dif),TELE_MOMENTUM*vec_len(dif)));
}

#pragma endregion

// ---missile---
#pragma region
#pragma endregion

#pragma endregion

// ===game helper===
#pragma region
typedef enum {
    TITLE_SCREEN,
    LEVEL_SELECTION,
    IN_GAME,
    PASSED
} GameState;

void draw_info(Player* player,Level* lvl, bool light_mode) {
    float x = player->rect.pos.x, y = player->rect.pos.y;
    ALLEGRO_COLOR color = al_map_rgb(0, 0, 0);
    if (!light_mode) color = al_map_rgb(210,210,210);
    int lines = 0;

    if (debug) {
        al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Position: %f %f",x,y);
        lines++;
        al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Velocity: %f %f",player->rect.velocity.x,player->rect.velocity.y);
        lines++;
        al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "walk_dir: %d",player->walk_dir);
        lines++;
        al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Grounded: %s",(player->grounded)?"1":"0");
        lines++;
        al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Type: %c",player->under);
        lines++;
    }
    al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Focus: %f, Rate: %f",player->focus, lvl->focus_rate_inc);
    lines++;
    al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Audio: %s",play_audio?"ON":"OFF");
    lines++;
    al_draw_textf(info_font, color, 10, 10+info_font_height*lines, 0, "Deaths: %i",death_cnt);
    
    al_draw_textf(info_font, color, 10, WINDOW_H-10-info_font_height*2, 0, "Playing level %i",curr_level);

    // write tutorial
    if (show_tutorial) {
        float box_w = WINDOW_W, box_h = 60;
        float box_x = (WINDOW_W - box_w) / 2;
        float box_y = WINDOW_H - box_h - 40;
        al_draw_filled_rectangle(box_x, box_y, box_x + box_w, box_y + box_h, al_map_rgba(0,0,0,180));
        al_draw_text(info_font, al_map_rgb(255,255,255), WINDOW_W/2, box_y + 15, ALLEGRO_ALIGN_CENTRE, tutorial_text);
    }
}

void ingame_input_handle(Player* player) {
    if (player->focusing) {
        if (key[ALLEGRO_KEY_LEFT] || key[ALLEGRO_KEY_RIGHT]) {
            if (!key[ALLEGRO_KEY_LEFT] && key[ALLEGRO_KEY_RIGHT]) {
                player->focus_target.x += FOCUS_MOVE_SPEED/FPS;
            }
            else if (key[ALLEGRO_KEY_LEFT] && !key[ALLEGRO_KEY_RIGHT]) {
                player->focus_target.x -= FOCUS_MOVE_SPEED/FPS;
            }
        }
        if (key[ALLEGRO_KEY_UP] || key[ALLEGRO_KEY_DOWN]) {
            if (key[ALLEGRO_KEY_UP] && !key[ALLEGRO_KEY_DOWN]) {
                player->focus_target.y -= FOCUS_MOVE_SPEED/FPS;
            }
            else if (!key[ALLEGRO_KEY_UP] && key[ALLEGRO_KEY_DOWN]) {
                player->focus_target.y += FOCUS_MOVE_SPEED/FPS;
            }
        }
    }
    else {
        if (key[ALLEGRO_KEY_LEFT] || key[ALLEGRO_KEY_RIGHT]) {
            player->status = PLAYER_WALKING;
            if (!key[ALLEGRO_KEY_LEFT] && key[ALLEGRO_KEY_RIGHT]) {
                player->walk_dir = 1;
            }
            else if (key[ALLEGRO_KEY_LEFT] && !key[ALLEGRO_KEY_RIGHT]) {
                player->walk_dir = -1;
            }
        }
        else {
            player->status = PLAYER_IDLE;
            player->walk_dir = 0;
        }
    }

    if (debug) { // allow for antigravity movement only 
    if (!has_gravity) {
        if (key[ALLEGRO_KEY_LSHIFT] || key[ALLEGRO_KEY_RSHIFT]) {
            if (key[ALLEGRO_KEY_UP] || key[ALLEGRO_KEY_DOWN]) {
                if (key[ALLEGRO_KEY_UP] && !key[ALLEGRO_KEY_DOWN]) {
                    player->rect.velocity.y = -player->walk_speed;
                }
                else if (!key[ALLEGRO_KEY_UP] && key[ALLEGRO_KEY_DOWN]) {
                    player->rect.velocity.y = player->walk_speed;
                }
            }
            else player->rect.velocity.y = 0;
        }
        else player->rect.velocity.y = 0;
    }
    }
}

void draw_title_screen(int selected_option,bool light_mode) {
    al_draw_scaled_bitmap(t_background,0,0,1024,768,0,0,WINDOW_W,WINDOW_H,0);
    ALLEGRO_COLOR color = al_map_rgb(0, 0, 0);
    if (!light_mode) color = al_map_rgb(210,210,210);
    al_draw_text(info_font, color,0,0,0,"ver 1.0.1");
    al_draw_text(info_font, color,0,info_font_height,0,"AUTHOR : Yu Wen-Kuang,	Zeng Qi-Ming, Liao Xiang-En");
    al_draw_text(info_font, color,0,info_font_height*2,0,"GROUP 84 FINAL PROJECT");
    al_draw_text(title_font, color, 100, 130, 0, "FOCUS EMULATED");

    int base_x = 120, base_y = 310;
    for (int i=0;i<option_count;i++) {
        float curr_y = base_y+title_font_height*i;
        if (i == selected_option) {
            al_draw_text(large_info_font, color,base_x,curr_y,0,title_screen_options[i]);
            al_draw_scaled_bitmap(player_idle_0,0,0,1200,1200,base_x-2*PLAYER_SIZE,curr_y+PLAYER_SIZE/2,PLAYER_SIZE,PLAYER_SIZE,0);
        }
        else {
            al_draw_text(info_font, color,base_x,curr_y,0,title_screen_options[i]);
        }
    }

    int info_y = WINDOW_H-120;
    al_draw_text(info_font, color,base_x,info_y,0,
    "* Use [UpArrow] or [DownArrow] to select option ");
    al_draw_text(info_font, color,base_x,info_y + info_font_height,0,
    "then use [A] to proceed.");
}

void play_level(Level* level, GameState* state, Player* player,Vector* deviation) {
    *state = IN_GAME;
    *player = player_new(level);
    *deviation = calculate_deviation(level);
    for (int i=0;i<level->target_cnt;i++) {
        int x = level->target_pos[i].x;
        int y = level->target_pos[i].y;
        level->map[y][x] = '7';
    }
}   

void proceed_as_selected(int selected_option,GameState* state,Level levels[MAX_LEVEL],Player* player,Vector* deviation) {
    switch (selected_option) {
        case 0:
            play_level(&levels[curr_level],state,player,deviation);
            break;
        case 1:

            break;
        case 2:
            
            break;
        case 3:
            exit(0);
            break;
    }
}

#pragma endregion

// ===resource manager===
#pragma region
void init_sprite() {
    brick_wall_0 = al_load_bitmap("src/brick_wall_0.png");
    must_init(brick_wall_0, "brick_wall_0");

    brick_wall_1 = al_load_bitmap("src/brick_wall_1.png");
    must_init(brick_wall_1, "brick_wall_1");

    slab = al_load_bitmap("src/slab.png");
    must_init(slab, "slab");

    player_idle_0 = al_load_bitmap("src/player_idle.png");
    must_init(player_idle_0, "src/player_idle.png");

    t_background = al_load_bitmap("src/t_background.png");
    must_init(t_background, "src/t_background.png");

    spike = al_load_bitmap("src/spike.png");
    must_init(spike, "src/spike.png");

    target_block_on = al_load_bitmap("src/target_block_on.png");
    must_init(target_block_on, "src/target_block_on.png");

    target_block_off = al_load_bitmap("src/target_block_off.png");
    must_init(target_block_off, "src/target_block_off.png");

    sign = al_load_bitmap("src/sign.png");
    must_init(sign, "src/sign.png");

    ice = al_load_bitmap("src/ice.png");
    must_init(ice, "src/ice.png");

    crystal = al_load_bitmap("src/crystal.png");
    must_init(crystal, "src/crystal.png");
}
void deinit_sprite() {
    al_destroy_bitmap(brick_wall_0);
    al_destroy_bitmap(brick_wall_1);
    al_destroy_bitmap(slab);
    al_destroy_bitmap(player_idle_0);
    al_destroy_bitmap(t_background);
    al_destroy_bitmap(spike);
    al_destroy_bitmap(target_block_on);
    al_destroy_bitmap(target_block_off);
    al_destroy_bitmap(sign);
    al_destroy_bitmap(ice);
    al_destroy_bitmap(crystal);
}

void init_audio() {
    charging = al_load_sample("src/charging.wav");
    must_init(charging, "charging");

    ding = al_load_sample("src/ding.wav");
    must_init(ding, "ding");

    teleport_pew = al_load_sample("src/teleport_pew.wav");
    must_init(teleport_pew, "teleport_pew");

    select_ding = al_load_sample("src/select.wav");
    must_init(select_ding, "select_ding");

    death_buzz = al_load_sample("src/death_buzz.wav");
    must_init(death_buzz, "death_buzz");
}

void deinit_audio() {
    al_destroy_sample(charging);
    al_destroy_sample(ding);
    al_destroy_sample(teleport_pew);
    al_destroy_sample(select_ding);
    al_destroy_sample(death_buzz);
}

#pragma endregion

// ===animation handler===
#pragma region

// ---anim struct---

typedef enum {
    ANIM_LINEAR,
    ANIM_SINE,
    ANIM_EXPONENTIAL
} AnimStyle;

typedef struct { // describes way to change a value
    Vector endpoints;
    AnimStyle anim_style;
    float t0;
    float t1;
    float t;

} Anim;

Anim new_anim(Vector endpoints, AnimStyle anim_style,float t0, float t1) {
    Anim ans;
    ans.endpoints = endpoints;
    ans.anim_style = anim_style;
    ans.t0 = t0;
    ans.t1 = t1;
    return ans;
}

float anim_eval(Anim* anim,float curr_time) {
    float x = anim->endpoints.x, y = anim->endpoints.y;
    float t = anim->t, t0 = anim->t0, t1 = anim->t1;
    float nt = (t-t0)/(t1-t0);
    float ease = 0;
    switch (anim->anim_style) {
        case ANIM_LINEAR:
            ease = nt;
            break;
        case ANIM_SINE:
            ease = sinf(nt * (M_PI / 2)); // fade out
            break;
        case ANIM_EXPONENTIAL:
            ease = nt == 0 ? 0 : powf(2, 10 * (nt - 1)); // fade in
            break;
        default:
            ease = nt; 
            break;
    }

    return anim->endpoints.x*(1-ease) + anim->endpoints.y*ease;
}


// ---some anim setting---

void animplay_death(Player* player) {
    
}

void animplay_changescene() {

}

#pragma endregion

// ===main===
#pragma region
int main() {
    // ---init---
    #pragma region

    // --allegro engine-- (don't touch)
    #pragma region
    must_init(al_init(), "allegro");
    must_init(al_install_keyboard(), "keyboard");

    timer = al_create_timer(1.0 / FPS);
    must_init(timer, "timer");

    queue = al_create_event_queue();
    must_init(queue, "queue");

    disp = al_create_display(WINDOW_W, WINDOW_H);
    must_init(disp, "display");

    must_init(al_init_image_addon(), "image addon");
    must_init(al_init_font_addon(), "font addon");
    must_init(al_init_ttf_addon(), "ttf addon");

    info_font = al_load_font("src/QuinqueFive.ttf", 24, 0);
    must_init(info_font, "info font");
    info_font_height = al_get_font_line_height(info_font);

    large_info_font = al_load_font("src/QuinqueFive.ttf", 32, 0);
    must_init(large_info_font, "info font");
    large_info_font_height = al_get_font_line_height(large_info_font);

    title_font = al_load_font("src/Round9x13.ttf", 96, 0);
    must_init(title_font, "title font");
    title_font_height = al_get_font_line_height(title_font);

    must_init(al_install_audio(),"al_install_audio");
    must_init(al_init_acodec_addon(),"al_init_acodec_addon");
    must_init(al_reserve_samples(8),"al_reserve_samples");

    init_sprite();
    init_audio();

    al_register_event_source(queue, al_get_keyboard_event_source());
    al_register_event_source(queue, al_get_display_event_source(disp));
    al_register_event_source(queue, al_get_timer_event_source(timer));

    ALLEGRO_EVENT event;

    al_start_timer(timer);

    memset(key, 0, sizeof(key));

    #pragma endregion

    // --game object placement--
    #pragma region
    Level levels[MAX_LEVEL];
    int total_level = 0;
    sequential_level_load(levels,&total_level);
    // load levels up
    Vector deviation = calculate_deviation(&levels[curr_level]);
    Player player = player_new(&levels[curr_level]);

    #pragma endregion

    // ---game loop---
    #pragma region
    GameState state = TITLE_SCREEN;
    bool light_mode = true;
    bool done = false;
    bool redraw = true;
    float last_time = al_get_time();
    float now = al_get_time();
    float delta_time = 0;
    int selected_option = 0;
    option_count = sizeof(title_screen_options)/sizeof(title_screen_options[0]);
    while(true) {
        al_wait_for_event(queue, &event);

        switch(event.type) {
            case ALLEGRO_EVENT_TIMER:
                switch (state) {
                    case IN_GAME:
                        ingame_input_handle(&player);
                        if (stage_clear) {
                            curr_level++;
                            if (curr_level == total_level) curr_level = 0;
                            play_level(&levels[curr_level],&state,&player,&deviation);
                            stage_clear = false;
                            if (charging_playing) {
                                al_stop_sample(&charging_id);
                                charging_playing = false;
                            }
                        }
                        break;
                    case TITLE_SCREEN:

                        break;
                    default:
                        break;
                }

                if (key[ALLEGRO_KEY_ESCAPE]) done = true;

                for(int i = 0; i < ALLEGRO_KEY_MAX; i++) key[i] &= ~KEY_SEEN;
                    

                redraw = true;
                break;

            case ALLEGRO_EVENT_KEY_DOWN:
                if (event.keyboard.keycode < 0 || event.keyboard.keycode >= ALLEGRO_KEY_MAX) break;
                switch (event.keyboard.keycode) {
                    case ALLEGRO_KEY_L:
                        light_mode = !light_mode;
                        break;
                    case ALLEGRO_KEY_G:
                        switch (state) {
                            case (IN_GAME):
                                has_gravity = !has_gravity;
                                break;
                            default:
                                break;
                        }
                        break;
                    case ALLEGRO_KEY_UP: 
                        switch (state) {
                            case (IN_GAME): // jumping
                                if ((player.grounded || player.coyote_timer > 0.0f) && !player.focusing) {
                                    player.rect.velocity.y = -player_jump_force;
                                }
                                break;
                            case (TITLE_SCREEN): // selecting
                                selected_option = max(selected_option-1,0);
                                if (play_audio) al_play_sample(select_ding,1.0,0,1.0,ALLEGRO_PLAYMODE_ONCE,NULL);
                                break;
                            default:
                                break;
                        }
                        break;
                    case ALLEGRO_KEY_DOWN:
                        switch (state) {
                            case (TITLE_SCREEN):
                                selected_option = min(selected_option+1,option_count-1);
                                if (play_audio) al_play_sample(select_ding,1.0,0,1.0,ALLEGRO_PLAYMODE_ONCE,NULL);
                                break;
                            default:
                                break;
                        }
                        break;
                    case ALLEGRO_KEY_A:
                        switch (state) {
                            case (IN_GAME): // entering player focus
                                if (player.can_refocus) {
                                    player.focusing = true;
                                    player.status = PLAYER_IDLE;
                                    player.focus_target = player_initial_pos(&player);
                                }
                                break;
                            case (TITLE_SCREEN):
                                proceed_as_selected(selected_option,&state,levels,&player,&deviation);
                                break;
                            default:
                                break;
                        }
                        break;
                    case ALLEGRO_KEY_TAB:
                        if (state != IN_GAME) continue;
                        stage_clear = true;
                        break;
                    case ALLEGRO_KEY_D:
                        debug = !debug;
                        break;
                    case ALLEGRO_KEY_T:
                        animplay_death(&player);
                        break;
                    case ALLEGRO_KEY_M:
                        play_audio = !play_audio;
                        if (!play_audio) {
                            // al_stop_samples();
                        }
                        break;
                    default:
                        break;
                }
            
                key[event.keyboard.keycode] = KEY_SEEN | KEY_DOWN;
                break;
            case ALLEGRO_EVENT_KEY_UP:
                if (event.keyboard.keycode < 0 || event.keyboard.keycode >= ALLEGRO_KEY_MAX) break;
                key[event.keyboard.keycode] &= ~KEY_DOWN;
                if (event.keyboard.keycode == ALLEGRO_KEY_A) {
                    if (player.focusing) {
                        player_attempt_teleport(&player,&levels[curr_level]);

                        player.focusing = false;
                        player.can_refocus = true;
                    }
                }
                break;
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                done = true;
                break;
            default:
                break;
        }

        if(done) break;

        now = al_get_time();
        delta_time = now - last_time;
        last_time = now;
        if(redraw && al_is_event_queue_empty(queue)) {  
            if (light_mode) {
                al_clear_to_color(al_map_rgb(255, 255, 255));
            } else {
                al_clear_to_color(al_map_rgb(30, 30, 30));
            }

            switch (state) {
                case (TITLE_SCREEN):
                    draw_title_screen(selected_option,light_mode);
                    break;
                case (IN_GAME):
                    level_draw(&levels[curr_level],deviation,now);
                    player_draw(&player,&levels[curr_level],deviation,now);
                    player_update(&player,&levels[curr_level],deviation,delta_time);
                    draw_info(&player,&levels[curr_level],light_mode);
                    break;
                case (LEVEL_SELECTION):
                    break;
                case (PASSED):
                    break;
                default:
                    break;
            }

            al_flip_display();
            redraw = false;
        }
    }
    #pragma endregion

    // ---deinit---
    #pragma region
    al_destroy_font(info_font);
    al_destroy_display(disp);
    al_destroy_timer(timer);
    al_destroy_event_queue(queue);

    deinit_sprite();
    deinit_audio();

    for (int i=0;i<total_level;i++) {
        level_free(&levels[i]);
    }
    #pragma endregion
}
#pragma endregion
