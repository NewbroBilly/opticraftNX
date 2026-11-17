#include <gba.h>
#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 240
#define SCREEN_H 160
#define HUD_Y 144
#define TILE_SIZE 8
#define WORLD_W 128
#define WORLD_H 64
#define WORLD_PX_W (WORLD_W * TILE_SIZE)
#define WORLD_PX_H (WORLD_H * TILE_SIZE)
#define SAVE_BASE ((volatile uint8_t *)0x0E000000)
#define SAVE_MAGIC 0x4F434741u
#define SAVE_VERSION 1u

#define RGB15(r, g, b) ((uint16_t)(((r) & 31) | (((g) & 31) << 5) | (((b) & 31) << 10)))

enum PaletteColor {
    C_SKY = 1,
    C_DIRT,
    C_GRASS,
    C_STONE,
    C_WOOD,
    C_LEAVES,
    C_HUD,
    C_WHITE,
    C_DIRT_LIGHT,
    C_DIRT_DARK,
    C_GRASS_LIGHT,
    C_STONE_LIGHT,
    C_STONE_DARK,
    C_WOOD_LIGHT,
    C_WOOD_DARK,
    C_LEAVES_LIGHT,
    C_LEAVES_DARK,
    C_BEDROCK,
    C_PLAYER_HAIR,
    C_PLAYER_SKIN,
    C_PLAYER_SHIRT,
    C_PLAYER_PANTS,
    C_PLAYER_EYE,
    C_HUD_SELECTED,
    C_HUD_BORDER,
    C_HUD_FILL,
    C_SUN,
    C_SAVE_OK,
    C_SAVE_OFF,
    C_COUNT
};

enum Block {
    BLOCK_AIR = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_WOOD,
    BLOCK_LEAVES,
    BLOCK_BEDROCK
};

enum ItemSlot {
    SLOT_DIRT = 0,
    SLOT_STONE,
    SLOT_WOOD,
    SLOT_COUNT
};

typedef struct GameSave {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t checksum;
    int16_t player_x;
    int16_t player_y;
    int16_t velocity_y;
    int8_t facing;
    uint8_t selected_slot;
    uint16_t inventory[SLOT_COUNT];
    uint8_t world[WORLD_H][WORLD_W];
} GameSave;

static GameSave game;
static volatile uint8_t *video;
static int camera_x;
static int camera_y;
static int save_flash_frames;
static uint8_t random_state = 0x5D;
static const uint8_t sky_color = C_SKY;
static const uint8_t dirt_color = C_DIRT;
static const uint8_t grass_color = C_GRASS;
static const uint8_t stone_color = C_STONE;
static const uint8_t wood_color = C_WOOD;
static const uint8_t leaves_color = C_LEAVES;
static const uint8_t hud_color = C_HUD;
static const uint8_t white_color = C_WHITE;

static void initialize_palette(void) {
    static const uint16_t colors[C_COUNT] = {
        [C_SKY] = RGB15(15, 23, 31),
        [C_DIRT] = RGB15(18, 10, 5),
        [C_GRASS] = RGB15(7, 21, 5),
        [C_STONE] = RGB15(13, 15, 18),
        [C_WOOD] = RGB15(20, 12, 4),
        [C_LEAVES] = RGB15(4, 17, 5),
        [C_HUD] = RGB15(3, 4, 8),
        [C_WHITE] = RGB15(31, 31, 31),
        [C_DIRT_LIGHT] = RGB15(23, 14, 7),
        [C_DIRT_DARK] = RGB15(11, 6, 3),
        [C_GRASS_LIGHT] = RGB15(9, 26, 7),
        [C_STONE_LIGHT] = RGB15(19, 20, 22),
        [C_STONE_DARK] = RGB15(8, 9, 12),
        [C_WOOD_LIGHT] = RGB15(25, 17, 7),
        [C_WOOD_DARK] = RGB15(13, 8, 3),
        [C_LEAVES_LIGHT] = RGB15(8, 23, 6),
        [C_LEAVES_DARK] = RGB15(2, 11, 3),
        [C_BEDROCK] = RGB15(5, 5, 7),
        [C_PLAYER_HAIR] = RGB15(18, 10, 5),
        [C_PLAYER_SKIN] = RGB15(25, 18, 11),
        [C_PLAYER_SHIRT] = RGB15(3, 3, 3),
        [C_PLAYER_PANTS] = RGB15(7, 9, 20),
        [C_PLAYER_EYE] = RGB15(0, 0, 0),
        [C_HUD_SELECTED] = RGB15(30, 25, 7),
        [C_HUD_BORDER] = RGB15(11, 12, 17),
        [C_HUD_FILL] = RGB15(5, 6, 10),
        [C_SUN] = RGB15(31, 27, 16),
        [C_SAVE_OK] = RGB15(7, 28, 9),
        [C_SAVE_OFF] = RGB15(13, 14, 19)
    };
    volatile uint16_t *palette = (volatile uint16_t *)0x05000000;
    unsigned i;
    for (i = 0; i < C_COUNT; ++i) palette[i] = colors[i];
}

static uint8_t next_random(void) {
    random_state = (uint8_t)(random_state * 73u + 41u);
    return random_state;
}

static uint32_t save_checksum(const GameSave *data) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    size_t i;
    for (i = offsetof(GameSave, player_x); i < sizeof(GameSave); ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static void save_world(void) {
    const uint8_t *source;
    size_t i;
    game.magic = SAVE_MAGIC;
    game.version = SAVE_VERSION;
    game.checksum = save_checksum(&game);
    source = (const uint8_t *)&game;

    /* Invalidate the old record first, then write its magic last. */
    SAVE_BASE[0] = 0;
    SAVE_BASE[1] = 0;
    SAVE_BASE[2] = 0;
    SAVE_BASE[3] = 0;
    for (i = sizeof(game) - 1; i >= sizeof(game) - 4; --i) {
        SAVE_BASE[i] = source[i];
    }
    for (i = 4; i < sizeof(game) - 4; ++i) {
        SAVE_BASE[i] = source[i];
    }
    for (i = 0; i < 4; ++i) {
        SAVE_BASE[i] = source[i];
    }
    save_flash_frames = 45;
}

static int load_world(void) {
    uint8_t *destination = (uint8_t *)&game;
    size_t i;
    for (i = 0; i < sizeof(game); ++i) {
        destination[i] = SAVE_BASE[i];
    }
    if (game.magic != SAVE_MAGIC || game.version != SAVE_VERSION) return 0;
    if (game.checksum != save_checksum(&game)) return 0;
    if (game.selected_slot >= SLOT_COUNT) return 0;
    if (game.player_x < 0 || game.player_x >= WORLD_PX_W || game.player_y < 0 || game.player_y >= WORLD_PX_H) return 0;
    return 1;
}

static void generate_world(void) {
    int x;
    int y;
    int surface = 18;
    int spawn_surface;
    for (y = 0; y < WORLD_H; ++y) {
        for (x = 0; x < WORLD_W; ++x) game.world[y][x] = BLOCK_AIR;
    }

    random_state = 0x5D;
    for (x = 0; x < WORLD_W; ++x) {
        surface += (int)(next_random() % 3u) - 1;
        if (surface < 15) surface = 15;
        if (surface > 22) surface = 22;
        game.world[surface][x] = BLOCK_GRASS;
        for (y = surface + 1; y < WORLD_H - 1; ++y) {
            uint8_t block = (y < surface + 5) ? BLOCK_DIRT : BLOCK_STONE;
            uint8_t cave = (uint8_t)(x * 29 + y * 17 + (x * y) / 7);
            if (y > surface + 7 && cave % 37u < 5u) block = BLOCK_AIR;
            game.world[y][x] = block;
        }
        game.world[WORLD_H - 1][x] = BLOCK_BEDROCK;
    }

    /* Small trees away from the initial spawn point. */
    for (x = 25; x < WORLD_W - 3; ++x) {
        if ((x % 13) == 4 && game.world[20][x] != BLOCK_AIR) {
            int top = 0;
            int crown_y;
            while (top < WORLD_H && game.world[top][x] == BLOCK_AIR) ++top;
            if (top < 5 || top >= WORLD_H) continue;
            for (y = top - 3; y < top; ++y) game.world[y][x] = BLOCK_WOOD;
            crown_y = top - 4;
            if (crown_y >= 0) {
                game.world[crown_y][x] = BLOCK_LEAVES;
                if (x > 0) game.world[crown_y + 1][x - 1] = BLOCK_LEAVES;
                if (x + 1 < WORLD_W) game.world[crown_y + 1][x + 1] = BLOCK_LEAVES;
            }
        }
    }

    spawn_surface = 0;
    while (spawn_surface < WORLD_H && game.world[spawn_surface][12] == BLOCK_AIR) ++spawn_surface;
    game.player_x = 12 * TILE_SIZE;
    game.player_y = spawn_surface * TILE_SIZE - 12;
    game.velocity_y = 0;
    game.facing = 1;
    game.selected_slot = SLOT_DIRT;
    game.inventory[SLOT_DIRT] = 20;
    game.inventory[SLOT_STONE] = 0;
    game.inventory[SLOT_WOOD] = 0;
    game.magic = SAVE_MAGIC;
    game.version = SAVE_VERSION;
    game.checksum = save_checksum(&game);
}

static int is_solid(int tx, int ty) {
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return 1;
    return game.world[ty][tx] != BLOCK_AIR;
}

static int rect_hits_solid(int x, int y, int w, int h) {
    int left = x / TILE_SIZE;
    int right = (x + w - 1) / TILE_SIZE;
    int top = y / TILE_SIZE;
    int bottom = (y + h - 1) / TILE_SIZE;
    int tx;
    int ty;
    for (ty = top; ty <= bottom; ++ty) {
        for (tx = left; tx <= right; ++tx) {
            if (is_solid(tx, ty)) return 1;
        }
    }
    return 0;
}

static void move_player(int horizontal, int jump) {
    int step;
    int amount;
    if (horizontal != 0) {
        game.facing = horizontal > 0 ? 1 : -1;
        amount = horizontal > 0 ? horizontal : -horizontal;
        for (step = 0; step < amount; ++step) {
            int next_x = game.player_x + (horizontal > 0 ? 1 : -1);
            if (!rect_hits_solid(next_x, game.player_y, 7, 12)) game.player_x = (int16_t)next_x;
            else break;
        }
    }

    if (jump && rect_hits_solid(game.player_x, game.player_y + 1, 7, 12)) {
        game.velocity_y = -5;
    }
    if (game.velocity_y < 4) ++game.velocity_y;
    amount = game.velocity_y >= 0 ? game.velocity_y : -game.velocity_y;
    for (step = 0; step < amount; ++step) {
        int dy = game.velocity_y > 0 ? 1 : -1;
        int next_y = game.player_y + dy;
        if (!rect_hits_solid(game.player_x, next_y, 7, 12)) game.player_y = (int16_t)next_y;
        else {
            game.velocity_y = 0;
            break;
        }
    }
}

static int target_tile(int *tx, int *ty) {
    int px = game.player_x + (game.facing > 0 ? 8 : -1);
    int py = game.player_y + 6;
    if (px < 0 || px >= WORLD_PX_W) return 0;
    *tx = px / TILE_SIZE;
    *ty = py / TILE_SIZE;
    return *ty >= 0 && *ty < WORLD_H;
}

static void mine_block(void) {
    int tx;
    int ty;
    uint8_t block;
    if (!target_tile(&tx, &ty)) return;
    block = game.world[ty][tx];
    if (block == BLOCK_AIR || block == BLOCK_BEDROCK) return;
    game.world[ty][tx] = BLOCK_AIR;
    if (block == BLOCK_STONE) ++game.inventory[SLOT_STONE];
    else if (block == BLOCK_WOOD || block == BLOCK_LEAVES) ++game.inventory[SLOT_WOOD];
    else ++game.inventory[SLOT_DIRT];
}

static void place_block(void) {
    static const uint8_t slot_blocks[SLOT_COUNT] = { BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD };
    int tx;
    int ty;
    int tile_x;
    int tile_y;
    int px = game.player_x;
    int py = game.player_y;
    if (!target_tile(&tx, &ty) || game.world[ty][tx] != BLOCK_AIR) return;
    tile_x = tx * TILE_SIZE;
    tile_y = ty * TILE_SIZE;
    if (px < tile_x + TILE_SIZE && px + 7 > tile_x && py < tile_y + TILE_SIZE && py + 12 > tile_y) return;
    if (game.inventory[game.selected_slot] == 0) return;
    game.world[ty][tx] = slot_blocks[game.selected_slot];
    --game.inventory[game.selected_slot];
}

static void fill_rect(int x, int y, int w, int h, uint16_t color, int clip_bottom) {
    int yy;
    int xx;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCREEN_W ? SCREEN_W : x + w;
    int y1 = y + h > clip_bottom ? clip_bottom : y + h;
    for (yy = y0; yy < y1; ++yy) {
        for (xx = x0; xx < x1; ++xx) video[yy * SCREEN_W + xx] = color;
    }
}

static uint16_t block_color(uint8_t block) {
    switch (block) {
        case BLOCK_GRASS: return grass_color;
        case BLOCK_DIRT: return dirt_color;
        case BLOCK_STONE: return stone_color;
        case BLOCK_WOOD: return wood_color;
        case BLOCK_LEAVES: return leaves_color;
        case BLOCK_BEDROCK: return RGB(5, 5, 7);
        default: return sky_color;
    }
}

static void draw_block(uint8_t block, int x, int y) {
    uint16_t color;
    if (block == BLOCK_AIR) return;
    color = block_color(block);
    fill_rect(x, y, TILE_SIZE, TILE_SIZE, color, HUD_Y);
    if (block == BLOCK_GRASS) {
        fill_rect(x, y, TILE_SIZE, 2, RGB(9, 26, 7), HUD_Y);
        fill_rect(x + 1, y + 4, 2, 1, RGB(23, 14, 7), HUD_Y);
        fill_rect(x + 5, y + 6, 2, 1, RGB(11, 6, 3), HUD_Y);
    } else if (block == BLOCK_DIRT) {
        fill_rect(x + 2, y + 2, 2, 1, RGB(23, 14, 7), HUD_Y);
        fill_rect(x + 5, y + 5, 1, 2, RGB(11, 6, 3), HUD_Y);
    } else if (block == BLOCK_STONE || block == BLOCK_BEDROCK) {
        fill_rect(x + 1, y + 2, 2, 1, RGB(19, 20, 22), HUD_Y);
        fill_rect(x + 5, y + 5, 2, 1, RGB(8, 9, 12), HUD_Y);
    } else if (block == BLOCK_WOOD) {
        fill_rect(x + 2, y, 1, TILE_SIZE, RGB(25, 17, 7), HUD_Y);
        fill_rect(x + 6, y, 1, TILE_SIZE, RGB(13, 8, 3), HUD_Y);
    } else if (block == BLOCK_LEAVES) {
        fill_rect(x + 1, y + 1, 2, 2, RGB(8, 23, 6), HUD_Y);
        fill_rect(x + 5, y + 5, 2, 2, RGB(2, 11, 3), HUD_Y);
    }
}

static void draw_digit(int x, int y, unsigned digit, uint16_t color) {
    static const uint8_t glyphs[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7}
    };
    int row;
    int col;
    if (digit > 9) return;
    for (row = 0; row < 5; ++row) {
        for (col = 0; col < 3; ++col) {
            if (glyphs[digit][row] & (1u << (2 - col))) video[(y + row) * SCREEN_W + x + col] = color;
        }
    }
}

static void draw_count(int x, int y, unsigned count) {
    unsigned hundreds = count / 100;
    unsigned tens = (count / 10) % 10;
    unsigned ones = count % 10;
    if (hundreds) draw_digit(x, y, hundreds, white_color);
    if (count >= 10) draw_digit(x + 4, y, tens, white_color);
    draw_digit(x + (count >= 100 ? 8 : count >= 10 ? 4 : 0), y, ones, white_color);
}

static void draw_player(void) {
    int sx = game.player_x - camera_x;
    int sy = game.player_y - camera_y;
    fill_rect(sx + 1, sy, 5, 3, RGB(18, 10, 5), HUD_Y);
    fill_rect(sx, sy + 3, 7, 5, RGB(25, 18, 11), HUD_Y);
    fill_rect(sx + 1, sy + 8, 2, 4, RGB(7, 9, 20), HUD_Y);
    fill_rect(sx + 4, sy + 8, 2, 4, RGB(7, 9, 20), HUD_Y);
    fill_rect(sx + (game.facing > 0 ? 5 : 1), sy + 4, 1, 1, RGB(3, 3, 3), HUD_Y);
}

static void draw_hud(void) {
    static const uint16_t slot_colors[SLOT_COUNT] = { dirt_color, stone_color, wood_color };
    int i;
    fill_rect(0, HUD_Y, SCREEN_W, SCREEN_H - HUD_Y, hud_color, SCREEN_H);
    for (i = 0; i < SLOT_COUNT; ++i) {
        int x = 5 + i * 54;
        uint16_t border = i == game.selected_slot ? RGB(30, 25, 7) : RGB(11, 12, 17);
        fill_rect(x, HUD_Y + 2, 49, 12, border, SCREEN_H);
        fill_rect(x + 1, HUD_Y + 3, 47, 10, RGB(5, 6, 10), SCREEN_H);
        fill_rect(x + 4, HUD_Y + 5, 6, 6, slot_colors[i], SCREEN_H);
        if (i == SLOT_DIRT) fill_rect(x + 4, HUD_Y + 5, 6, 2, grass_color, SCREEN_H);
        draw_count(x + 16, HUD_Y + 5, game.inventory[i]);
    }
    fill_rect(222, HUD_Y + 4, 10, 8,
              save_flash_frames ? RGB(7, 28, 9) : RGB(13, 14, 19), SCREEN_H);
}

static void update_camera(void) {
    int wanted_x = game.player_x + 3 - SCREEN_W / 2;
    int wanted_y = game.player_y + 6 - HUD_Y / 2;
    int max_x = WORLD_PX_W - SCREEN_W;
    int max_y = WORLD_PX_H - HUD_Y;
    camera_x = wanted_x < 0 ? 0 : wanted_x > max_x ? max_x : wanted_x;
    camera_y = wanted_y < 0 ? 0 : wanted_y > max_y ? max_y : wanted_y;
}

static void render_frame(void) {
    int x;
    int y;
    int tx0;
    int ty0;
    int tx;
    int ty;
    int target_x;
    int target_y;
    int sx;
    int sy;

    fill_rect(0, 0, SCREEN_W, HUD_Y, sky_color, HUD_Y);
    fill_rect(188 - camera_x / 4, 17 - camera_y / 6, 18, 18, RGB(31, 27, 16), HUD_Y);
    tx0 = camera_x / TILE_SIZE;
    ty0 = camera_y / TILE_SIZE;
    for (ty = ty0; ty <= (camera_y + HUD_Y) / TILE_SIZE; ++ty) {
        for (tx = tx0; tx <= (camera_x + SCREEN_W) / TILE_SIZE; ++tx) {
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            x = tx * TILE_SIZE - camera_x;
            y = ty * TILE_SIZE - camera_y;
            draw_block(game.world[ty][tx], x, y);
        }
    }

    if (target_tile(&target_x, &target_y)) {
        sx = target_x * TILE_SIZE - camera_x;
        sy = target_y * TILE_SIZE - camera_y;
        if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < HUD_Y) {
            for (x = 0; x < TILE_SIZE && sx + x < SCREEN_W; ++x) {
                if (sy < HUD_Y) video[sy * SCREEN_W + sx + x] = white_color;
                if (sy + TILE_SIZE - 1 < HUD_Y) video[(sy + TILE_SIZE - 1) * SCREEN_W + sx + x] = white_color;
            }
            for (y = 0; y < TILE_SIZE && sy + y < HUD_Y; ++y) {
                video[(sy + y) * SCREEN_W + sx] = white_color;
                if (sx + TILE_SIZE - 1 < SCREEN_W) video[(sy + y) * SCREEN_W + sx + TILE_SIZE - 1] = white_color;
            }
        }
    }
    draw_player();
    draw_hud();
}

int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);
    SetMode(MODE_3 | BG2_ENABLE);
    if (!load_world()) generate_world();

    while (1) {
        uint16_t down;
        uint16_t held;
        scanKeys();
        down = keysDown();
        held = keysHeld();

        if (held & KEY_LEFT) move_player(-2, 0);
        if (held & KEY_RIGHT) move_player(2, 0);
        move_player(0, (down & KEY_UP) != 0);
        if (down & KEY_A) mine_block();
        if (down & KEY_B) place_block();
        if (down & KEY_L) game.selected_slot = (game.selected_slot + SLOT_COUNT - 1) % SLOT_COUNT;
        if (down & KEY_R) game.selected_slot = (game.selected_slot + 1) % SLOT_COUNT;
        if (down & KEY_START) save_world();
        if (save_flash_frames > 0) --save_flash_frames;

        update_camera();
        render_frame();
        VBlankIntrWait();
    }
    return 0;
}
