#include <stdio.h>
#include <stdlib.h> // For system("cls") or system("clear")
#include <math.h>   // For sin, cos, atan2, etc.
#include <string.h> // For memset, memcpy
#include <time.h>   // For clock() and CLOCKS_PER_SEC

// For non-blocking input
#ifdef _WIN32
#include <conio.h> // For _kbhit and _getch
#else
#include <termios.h> // For non-blocking input
#include <unistd.h>  // For read
#endif

// --- ANSI Color Codes (for CLI graphics) ---
#define ANSI_COLOR_RED      "\x1b[31m"
#define ANSI_COLOR_GREEN    "\x1b[32m"
#define ANSI_COLOR_YELLOW   "\x1b[33m"
#define ANSI_COLOR_BLUE     "\x1b[34m"
#define ANSI_COLOR_MAGENTA  "\x1b[35m"
#define ANSI_COLOR_CYAN     "\x1b[36m"
#define ANSI_COLOR_RESET    "\x1b[0m"
#define ANSI_COLOR_WHITE_BG "\x1b[47m"   // White background for "sky"
#define ANSI_COLOR_GRAY_BG  "\x1b[100m"  // Dark gray background for "ground"
#define ANSI_COLOR_LIGHT_GRAY "\x1b[37m" // Light gray for distant walls

// --- Game Constants ---
#define MAP_WIDTH      20
#define MAP_HEIGHT     20
#define SCREEN_WIDTH   80
#define SCREEN_HEIGHT  25
#define FOV_DEGREES    60.0f
#define FOV_RADIANS    (FOV_DEGREES * M_PI / 180.0f)
#define PLAYER_MOVE_SPEED_BASE 3.0f // Units per second
#define PLAYER_ROT_SPEED_BASE  2.5f // Radians per second

#define MAX_RENDER_DISTANCE 15.0f

// --- Map Definition ---
char g_map[MAP_HEIGHT][MAP_WIDTH] = {
    "####################",
    "#.................C#",
    "#..########....#...#",
    "#..#.......#...#...#",
    "#..#...C...#...#...#",
    "#..#...#...#...#...#",
    "#..#.......#...#...#",
    "#..########....#...#",
    "#........E.........#", // Enemy
    "#....#######.......#",
    "#....#.....#.......#",
    "#....#.....#.......#",
    "#....#.....#.......#",
    "#....#######.......#",
    "#..................#",
    "#........C.........#",
    "#...########.......#",
    "#...#......#.......#",
    "#E..#......#......E#",
    "####################"
};

// --- Player State ---
typedef struct {
    float x;
    float y;
    float angle; // Radians
    int health;
    int score;
    float moveSpeed; // Dynamic move speed
    float rotSpeed;  // Dynamic rotation speed
} Player;

Player g_player; // Initialized in main

// --- Sprite Structure ---
typedef struct {
    float x;
    float y;
    char character;
    char* color;
    int isActive;
    float distToPlayer;
    int isEnemy; // 1 if enemy, 0 if collectible
    int health; // For enemies
    float attackCooldown; // For enemies
    float lastAttackTime; // For enemies
} Sprite;

#define MAX_SPRITES 20 // Increased max sprites
Sprite g_sprites[MAX_SPRITES];
int g_numSprites = 0;

// --- Projectile Structure ---
typedef struct {
    float x;
    float y;
    float dirX;
    float dirY;
    float speed;
    int isActive;
} Projectile;

#define MAX_PROJECTILES 5
Projectile g_projectiles[MAX_PROJECTILES];
int g_numProjectiles = 0;

// --- Screen Buffer for Rendering ---
char g_screenBuffer[SCREEN_HEIGHT][SCREEN_WIDTH + 1];
char g_colorBuffer[SCREEN_HEIGHT][SCREEN_WIDTH][10];

// --- Z-Buffer for correct rendering order (walls then sprites) ---
float g_zBuffer[SCREEN_WIDTH];

// Global termios settings for restoring terminal state on Unix-like systems
#ifndef _WIN32
struct termios original_termios;
#endif

// --- Game State Variables ---
typedef enum {
    GAME_STATE_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_GAME_OVER,
    GAME_STATE_WIN
} GameState;

GameState g_currentState = GAME_STATE_MENU;
clock_t g_lastFrameTime;
// Score to win now depends on collecting all items and defeating all enemies
// g_scoreToWin = 30 from collectibles (3 * 10) + enemies (3 * 50) = 180 total potential score.
// Win condition now checks if all active collectibles AND enemies are gone.

// --- Function Prototypes ---
void clearScreen();
void enableRawMode();
void disableRawMode();
void initializeGame();
void initializeSprites();
int compareSprites(const void *a, const void *b);
void renderMenu();
void renderGame();
void renderGameOver();
void renderWinScreen();
void processInput(float deltaTime);
void updateGame(float deltaTime);
void updateSprites(float deltaTime);
void updateProjectiles(float deltaTime);
void fireProjectile();
void drawHUD();
void drawMiniMap();

// --- Function to clear the console ---
void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    printf("\x1b[2J\x1b[H"); // ANSI escape code to clear screen and move cursor to home position
#endif
}

// --- Function to set terminal to raw mode (non-canonical, no echo) ---
void enableRawMode() {
#ifndef _WIN32
    struct termios raw;
    tcgetattr(STDIN_FILENO, &original_termios); // Save original settings
    memcpy(&raw, &original_termios, sizeof(struct termios)); // Copy original to modify

    raw.c_lflag &= ~(ECHO | ICANON); // Disable echo, disable canonical mode (line buffering)
    raw.c_cc[VMIN] = 0;  // Read returns as soon as 0 characters are available
    raw.c_cc[VTIME] = 0; // No timeout (returns immediately)

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); // Apply new settings
#endif
}

// --- Function to restore terminal to original mode ---
void disableRawMode() {
#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios); // Restore original settings
#endif
}

// --- Function to initialize game state ---
void initializeGame() {
    g_player = (Player){ .x = 2.5f, .y = 2.5f, .angle = M_PI / 4.0f, .health = 100, .score = 0,
                         .moveSpeed = PLAYER_MOVE_SPEED_BASE, .rotSpeed = PLAYER_ROT_SPEED_BASE };
    initializeSprites(); // Re-populate sprites for a new game

    // Reset projectiles
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        g_projectiles[i].isActive = 0;
    }
    g_numProjectiles = 0; // Reset active projectiles count
}

// --- Function to initialize sprites from the map ---
void initializeSprites() {
    g_numSprites = 0;
    // Iterate over the global map to find sprite initial positions
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (g_map[y][x] == 'C' && g_numSprites < MAX_SPRITES) {
                g_sprites[g_numSprites++] = (Sprite){.x = x + 0.5f, .y = y + 0.5f, .character = 'C', .color = ANSI_COLOR_YELLOW, .isActive = 1, .isEnemy = 0};
            } else if (g_map[y][x] == 'E' && g_numSprites < MAX_SPRITES) {
                g_sprites[g_numSprites++] = (Sprite){.x = x + 0.5f, .y = y + 0.5f, .character = 'E', .color = ANSI_COLOR_RED, .isActive = 1, .isEnemy = 1, .health = 50, .attackCooldown = 2.0f, .lastAttackTime = 0.0f};
            }
        }
    }
}

// --- Comparison function for qsort (sort sprites by distance) ---
int compareSprites(const void *a, const void *b) {
    Sprite *spriteA = (Sprite *)a;
    Sprite *spriteB = (Sprite *)b;
    // Sort in descending order (furthest first) for correct rendering
    if (spriteA->distToPlayer < spriteB->distToPlayer) return 1;
    if (spriteA->distToPlayer > spriteB->distToPlayer) return -1;
    return 0;
}

// --- Function to fire a projectile ---
void fireProjectile() {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (!g_projectiles[i].isActive) {
            g_projectiles[i].x = g_player.x;
            g_projectiles[i].y = g_player.y;
            g_projectiles[i].dirX = sin(g_player.angle);
            g_projectiles[i].dirY = cos(g_player.angle);
            g_projectiles[i].speed = 10.0f; // Fast projectile
            g_projectiles[i].isActive = 1;
            return;
        }
    }
}

// --- Rendering Functions ---
void renderMenu() {
    clearScreen();
    printf("\n\n%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_CYAN, ANSI_COLOR_RESET);
    printf("%s                      WELCOME TO MINI DOOM CLI!                           %s\n", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
    printf("%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_CYAN, ANSI_COLOR_RESET);
    printf("\n\n");
    printf("                                  'W' - Move Forward\n");
    printf("                                  'S' - Move Backward\n");
    printf("                                  'A' - Strafe Left\n");
    printf("                                  'D' - Strafe Right\n");
    printf("                                  'Q' - Rotate Left\n");
    printf("                                  'E' - Rotate Right\n");
    printf("                                  'F' - Fire (Projectile)\n");
    printf("                                  'X' - Exit Game\n");
    printf("\n\n");
    printf("%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_CYAN, ANSI_COLOR_RESET);
    printf("%s                      Press 'P' to Play or 'X' to Exit                      %s\n", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);
    printf("%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_CYAN, ANSI_COLOR_RESET);
}

void renderGameOver() {
    clearScreen();
    printf("\n\n%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_RED, ANSI_COLOR_RESET);
    printf("%s                      GAME OVER! Your health reached 0.                     %s\n", ANSI_COLOR_RED, ANSI_COLOR_RESET);
    printf("%s                      Final Score: %d                                      %s\n", ANSI_COLOR_YELLOW, g_player.score, ANSI_COLOR_RESET);
    printf("%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_RED, ANSI_COLOR_RESET);
    printf("\nPress 'R' to Restart or 'X' to Exit.\n");
}

void renderWinScreen() {
    clearScreen();
    printf("\n\n%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
    printf("%s                      CONGRATULATIONS! YOU WON!                           %s\n", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
    printf("%s                      Final Score: %d                                      %s\n", ANSI_COLOR_YELLOW, g_player.score, ANSI_COLOR_RESET);
    printf("%s--------------------------------------------------------------------------------%s\n", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
    printf("\nPress 'R' to Play Again or 'X' to Exit.\n");
}


void renderGame() {
    // --- Initialize screen and color buffers ---
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            if (y < SCREEN_HEIGHT / 2) {
                g_screenBuffer[y][x] = ' '; // Sky
                strcpy(g_colorBuffer[y][x], ANSI_COLOR_RESET); // Default for sky, background will apply
            } else {
                g_screenBuffer[y][x] = ' '; // Ground
                strcpy(g_colorBuffer[y][x], ANSI_COLOR_RESET); // Default for ground, background will apply
            }
        }
        g_screenBuffer[y][SCREEN_WIDTH] = '\0'; // Null-terminate each row
    }

    // --- Wall Raycasting (DDA Algorithm) ---
    for (int x_col = 0; x_col < SCREEN_WIDTH; ++x_col) {
        // Calculate ray angle for this column of the screen
        float cameraX = 2 * (x_col / (float)SCREEN_WIDTH) - 1; // x-coordinate in camera space [-1, 1]
        float rayDirX = sin(g_player.angle) + cos(g_player.angle) * cameraX;
        float rayDirY = cos(g_player.angle) - sin(g_player.angle) * cameraX;

        int mapX = (int)g_player.x;
        int mapY = (int)g_player.y;

        float sideDistX;
        float sideDistY;

        float deltaDistX = (rayDirX == 0) ? 1e30f : fabs(1 / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30f : fabs(1 / rayDirY);
        float perpWallDist = MAX_RENDER_DISTANCE; // Initialize to max distance

        int stepX;
        int stepY;
        int hit = 0; // Was there a wall hit?
        int side;    // 0 = x-side, 1 = y-side

        // Calculate step and initial sideDist
        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (g_player.x - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0f - g_player.x) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (mapY + 1.0f - g_player.y) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (g_player.y - mapY) * deltaDistY;
        }

        // Perform DDA
        while (hit == 0) {
            // Jump to next map square, either in x-direction, or in y-direction
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }

            // Check if ray has hit a wall or gone out of bounds
            if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) {
                hit = 1; // Ray went out of bounds
                perpWallDist = MAX_RENDER_DISTANCE; // Cap distance for out-of-bounds rays
            } else if (g_map[mapY][mapX] == '#') {
                hit = 1; // Ray hit a wall
            }
        }

        // Calculate perpendicular distance to wall only if a wall was hit within bounds
        if (perpWallDist == MAX_RENDER_DISTANCE) { // If it didn't hit a wall inside, keep max render distance
            // Do nothing, perpWallDist is already MAX_RENDER_DISTANCE
        } else { // It hit a wall inside the map
            if (side == 0) perpWallDist = (mapX - g_player.x + (1 - stepX) / 2) / rayDirX;
            else            perpWallDist = (mapY - g_player.y + (1 - stepY) / 2) / rayDirY;
        }

        // Clamp distance to avoid division by zero or negative distances, and cap at max render
        if (perpWallDist < 0.01f) perpWallDist = 0.01f;
        if (perpWallDist > MAX_RENDER_DISTANCE) perpWallDist = MAX_RENDER_DISTANCE;


        // Store distance in Z-buffer
        g_zBuffer[x_col] = perpWallDist;

        // Calculate height of line to draw on screen
        int lineHeight = (int)(SCREEN_HEIGHT / perpWallDist);
        int drawStart = -lineHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawEnd >= SCREEN_HEIGHT) drawEnd = SCREEN_HEIGHT - 1;

        // Choose wall character and color based on distance and side
        char wallChar;
        char* wallColor;

        if (perpWallDist < MAX_RENDER_DISTANCE) {
            // Textured Walls - Vary characters based on height
            if (side == 0) { // X-side wall
                wallColor = ANSI_COLOR_MAGENTA;
                if (perpWallDist < 3.0f) wallChar = '\xDB'; // Full block
                else if (perpWallDist < 6.0f) wallChar = '\xB2'; // Dark shade
                else if (perpWallDist < 9.0f) wallChar = '\xB1'; // Medium shade
                else wallChar = '\xB0'; // Light shade
            } else { // Y-side wall (darker)
                wallColor = ANSI_COLOR_CYAN;
                if (perpWallDist < 3.0f) wallChar = '\xDB';
                else if (perpWallDist < 6.0f) wallChar = '\xB2';
                else if (perpWallDist < 9.0f) wallChar = '\xB1';
                else wallChar = '\xB0';
            }

            // Draw the vertical wall slice
            for (int y = drawStart; y <= drawEnd; ++y) {
                g_screenBuffer[y][x_col] = wallChar;
                strcpy(g_colorBuffer[y][x_col], wallColor);
            }
        }
    }

    // --- Floor and Ceiling Rendering ---
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (y >= SCREEN_HEIGHT / 2) { // Ground
                float rowDist = y - SCREEN_HEIGHT / 2.0f;
                // Avoid division by zero when rowDist is zero or very small
                float depth = (rowDist == 0) ? MAX_RENDER_DISTANCE : SCREEN_HEIGHT / (2.0f * rowDist);

                char floorChar;
                char* floorColor = ANSI_COLOR_GRAY_BG; // Consistent background for ground

                if (depth < 0.5) floorChar = '#';
                else if (depth < 1.0) floorChar = '=';
                else if (depth < 2.0) floorChar = '-';
                else if (depth < 4.0) floorChar = '.';
                else floorChar = ' ';

                if (g_screenBuffer[y][x] == ' ') { // Only draw if no wall is there
                    g_screenBuffer[y][x] = floorChar;
                    strcpy(g_colorBuffer[y][x], floorColor);
                }
            } else { // Sky/Ceiling
                char skyChar;
                char* skyColor = ANSI_COLOR_WHITE_BG; // Consistent background for sky

                // Simple cloud effect (or just solid sky)
                if ((x + y) % 5 == 0 && (x % 3 == 0 || y % 7 == 0)) skyChar = '~'; // Small cloud variation
                else skyChar = ' ';

                if (g_screenBuffer[y][x] == ' ') { // Only draw if no wall is there
                    g_screenBuffer[y][x] = skyChar;
                    strcpy(g_colorBuffer[y][x], skyColor);
                }
            }
        }
    }

    // --- Sprite Rendering ---
    // Calculate distance for active sprites and prepare for sorting
    for (int i = 0; i < g_numSprites; ++i) {
        if (g_sprites[i].isActive) {
            g_sprites[i].distToPlayer = sqrtf(powf(g_player.x - g_sprites[i].x, 2) + powf(g_player.y - g_sprites[i].y, 2));
        } else {
            g_sprites[i].distToPlayer = -1.0f; // Mark inactive sprites to be ignored in sort
        }
    }

    // Sort sprites from furthest to closest
    qsort(g_sprites, g_numSprites, sizeof(Sprite), compareSprites);

    for (int i = 0; i < g_numSprites; ++i) {
        if (!g_sprites[i].isActive || g_sprites[i].distToPlayer > MAX_RENDER_DISTANCE || g_sprites[i].distToPlayer < 0.1f) continue; // Skip inactive, too far, or too close

        // Translate sprite position to camera space
        float spriteX = g_sprites[i].x - g_player.x;
        float spriteY = g_sprites[i].y - g_player.y;

        // Apply inverse camera transformation (rotate and scale)
        // invDet will always be 1.0f, but kept for clarity of the matrix operation
        // float invDet = 1.0f / (cos(g_player.angle) * cos(-g_player.angle) - sin(g_player.angle) * sin(-g_player.angle));

        float transformX = spriteX * cos(g_player.angle) + spriteY * sin(g_player.angle); // Transformed x
        float transformY = -spriteX * sin(g_player.angle) + spriteY * cos(g_player.angle); // Transformed y (depth)

        // Prevent division by zero for sprites directly on player or behind
        if (transformY <= 0) continue;

        int spriteScreenX = (int)((SCREEN_WIDTH / 2) * (1 + transformX / transformY));

        // Calculate sprite height and width (scaled by distance)
        int spriteHeight = abs((int)(SCREEN_HEIGHT / transformY));
        int spriteWidth = abs((int)(SCREEN_WIDTH / transformY));

        // Calculate start and end y position for sprite
        int drawStartY = -spriteHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawStartY < 0) drawStartY = 0;
        int drawEndY = spriteHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawEndY >= SCREEN_HEIGHT) drawEndY = SCREEN_HEIGHT - 1;

        // Calculate start and end x position for sprite
        int drawStartX = -spriteWidth / 2 + spriteScreenX;
        // int drawEndX = spriteWidth / 2 + spriteScreenX; // No need to calculate twice
        int drawEndX = drawStartX + spriteWidth; // Simpler calculation

        // Ensure sprite drawing is within screen bounds
        if (drawStartX < 0) drawStartX = 0;
        if (drawEndX >= SCREEN_WIDTH) drawEndX = SCREEN_WIDTH - 1;

        // Draw the sprite pixel by pixel
        for (int stripe = drawStartX; stripe <= drawEndX; stripe++) {
            // Z-buffer check: draw only if sprite is closer than wall at this column
            if (stripe >= 0 && stripe < SCREEN_WIDTH && transformY < g_zBuffer[stripe]) {
                for (int y_pos = drawStartY; y_pos <= drawEndY; y_pos++) {
                    if (y_pos >= 0 && y_pos < SCREEN_HEIGHT) {
                        char spriteChar = g_sprites[i].character;
                        // Animate enemy character or change based on health/state
                        if (g_sprites[i].isEnemy) {
                             if (g_sprites[i].health > 30) spriteChar = 'E'; // Healthy
                             else spriteChar = 'e'; // Damaged
                        }

                        g_screenBuffer[y_pos][stripe] = spriteChar;
                        strcpy(g_colorBuffer[y_pos][stripe], g_sprites[i].color);
                    }
                }
            }
        }
    }

    // --- Projectile Rendering ---
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (g_projectiles[i].isActive) {
            float projX = g_projectiles[i].x - g_player.x;
            float projY = g_projectiles[i].y - g_player.y;

            float transformX = projX * cos(g_player.angle) + projY * sin(g_player.angle);
            float transformY = -projX * sin(g_player.angle) + projY * cos(g_player.angle);

            // Projectile is behind the player or too close to avoid division issues
            if (transformY <= 0.01f) continue;

            int projScreenX = (int)((SCREEN_WIDTH / 2) * (1 + transformX / transformY));
            int projHeight = abs((int)(SCREEN_HEIGHT / (transformY * 2))); // Smaller projectile
            int projWidth = abs((int)(SCREEN_WIDTH / (transformY * 2)));

            int drawStartY = -projHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawStartY < 0) drawStartY = 0;
            int drawEndY = projHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawEndY >= SCREEN_HEIGHT) drawEndY = SCREEN_HEIGHT - 1;

            int drawStartX = -projWidth / 2 + projScreenX;
            int drawEndX = drawStartX + projWidth;

            if (drawStartX < 0) drawStartX = 0;
            if (drawEndX >= SCREEN_WIDTH) drawEndX = SCREEN_WIDTH - 1;

            for (int stripe = drawStartX; stripe <= drawEndX; stripe++) {
                // Z-buffer check for projectiles
                if (stripe >= 0 && stripe < SCREEN_WIDTH && transformY < g_zBuffer[stripe]) {
                    for (int y_pos = drawStartY; y_pos <= drawEndY; y_pos++) {
                        if (y_pos >= 0 && y_pos < SCREEN_HEIGHT) {
                            g_screenBuffer[y_pos][stripe] = '*'; // Projectile character
                            strcpy(g_colorBuffer[y_pos][stripe], ANSI_COLOR_YELLOW);
                        }
                    }
                }
            }
        }
    }


    // --- Print the entire buffer to the console ---
    clearScreen();
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        // Apply background colors for sky/ground regions
        if (y < SCREEN_HEIGHT / 2) {
            printf("%s", ANSI_COLOR_WHITE_BG); // Sky background
        } else {
            printf("%s", ANSI_COLOR_GRAY_BG);  // Ground background
        }

        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            // Apply foreground color from color buffer
            printf("%s%c", g_colorBuffer[y][x], g_screenBuffer[y][x]);
        }
        printf("%s\n", ANSI_COLOR_RESET); // Reset colors and new line
    }

    drawHUD();
    drawMiniMap();
}

// --- HUD (Heads-Up Display) ---
void drawHUD() {
    printf("%s", ANSI_COLOR_RESET); // Ensure reset before HUD
    printf("--------------------------------------------------------------------------------\n");
    printf(" HEALTH: %s", ANSI_COLOR_RED);
    for (int i = 0; i < g_player.health / 5; ++i) { // 20 segments for 100 health
        printf("\xDB"); // Full block character
    }
    printf("%s", ANSI_COLOR_RESET);
    printf("%*sSCORE: %d\n", (int)(SCREEN_WIDTH - 10 - (g_player.health / 5)), "", g_player.score); // Right align score
    printf("--------------------------------------------------------------------------------\n");
}

// --- Mini-Map and Player Info ---
void drawMiniMap() {
    printf("\n--- Mini Map (P = Player, C = Collectible, E = Enemy) ---\n");
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if ((int)g_player.x == x && (int)g_player.y == y) {
                printf(ANSI_COLOR_GREEN "P" ANSI_COLOR_RESET); // Player on map
            } else {
                int spriteDrawn = 0;
                for (int i = 0; i < g_numSprites; ++i) {
                    if (g_sprites[i].isActive && (int)g_sprites[i].x == x && (int)g_sprites[i].y == y) {
                        printf("%s%c%s", g_sprites[i].color, g_sprites[i].character, ANSI_COLOR_RESET);
                        spriteDrawn = 1;
                        break;
                    }
                }
                if (!spriteDrawn) {
                    printf("%c", g_map[y][x]);
                }
            }
        }
        printf("\n");
    }
    printf("Player X: %.1f, Y: %.1f, Angle: %.2f (deg: %.1f)\n",
           g_player.x, g_player.y, g_player.angle, g_player.angle * 180.0f / M_PI);
    printf("WASD to move, QE to rotate, F to fire. 'X' to exit.\n");
    printf("Collect 'C' for score, attack 'E' to defeat them.\n");
}


// --- Input Processing ---
void processInput(float deltaTime) {
    char input = 0; // Initialize input to 0 or a non-command character
#ifdef _WIN32
    if (_kbhit()) {
        input = _getch();
    }
#else
    // Read from stdin; if no character is available, it returns -1
    char buf[1];
    if (read(STDIN_FILENO, buf, 1) == 1) {
        input = buf[0];
    }
#endif

    float moveAmount = g_player.moveSpeed * deltaTime;
    float rotAmount = g_player.rotSpeed * deltaTime;

    switch (g_currentState) {
        case GAME_STATE_MENU:
            if (input == 'p' || input == 'P') {
                initializeGame();
                g_currentState = GAME_STATE_PLAYING;
            } else if (input == 'x' || input == 'X') {
                exit(0); // Exit immediately from menu
            }
            break;
        case GAME_STATE_PLAYING:
            if (g_player.health <= 0) { // Check for game over *before* processing more input
                g_currentState = GAME_STATE_GAME_OVER;
                return;
            }

            float newPlayerX = g_player.x;
            float newPlayerY = g_player.y;

            float playerDirX = sin(g_player.angle);
            float playerDirY = cos(g_player.angle);

            switch (input) {
                case 'w': // Move forward
                case 'W':
                    newPlayerX += playerDirX * moveAmount;
                    newPlayerY += playerDirY * moveAmount;
                    break;
                case 's': // Move backward
                case 'S':
                    newPlayerX -= playerDirX * moveAmount;
                    newPlayerY -= playerDirY * moveAmount;
                    break;
                case 'a': // Strafe left
                case 'A':
                    newPlayerX += cos(g_player.angle) * moveAmount;
                    newPlayerY -= sin(g_player.angle) * moveAmount;
                    break;
                case 'd': // Strafe right
                case 'D':
                    newPlayerX -= cos(g_player.angle) * moveAmount;
                    newPlayerY += sin(g_player.angle) * moveAmount;
                    break;
                case 'q': // Rotate left
                case 'Q':
                    g_player.angle -= rotAmount;
                    break;
                case 'e': // Rotate right
                case 'E':
                    g_player.angle += rotAmount;
                    break;
                case 'f': // Fire projectile
                case 'F':
                    fireProjectile();
                    break;
                case 'x': // Exit to menu
                case 'X':
                    g_currentState = GAME_STATE_MENU;
                    break;
                default:
                    // Do nothing for unrecognized input or no input
                    break;
            }

            // --- Collision Detection and Interaction ---
            // Convert new float coordinates to integer map coordinates
            int currentMapX = (int)g_player.x;
            int currentMapY = (int)g_player.y;
            int targetMapX = (int)newPlayerX;
            int targetMapY = (int)newPlayerY;

            // Simple collision: Check if the new position is within map bounds and not a wall
            // Check X movement first, allowing sliding along Y walls
            if (targetMapX >= 0 && targetMapX < MAP_WIDTH && currentMapY >= 0 && currentMapY < MAP_HEIGHT &&
                g_map[currentMapY][targetMapX] != '#') { // Only move if target cell is not a wall
                g_player.x = newPlayerX;
            }
            // Check Y movement second, allowing sliding along X walls
            if (currentMapX >= 0 && currentMapX < MAP_WIDTH && targetMapY >= 0 && targetMapY < MAP_HEIGHT &&
                g_map[targetMapY][currentMapX] != '#') { // Only move if target cell is not a wall
                g_player.y = newPlayerY;
            }

            // Keep angle within [0, 2*PI)
            g_player.angle = fmodf(g_player.angle, 2 * M_PI);
            if (g_player.angle < 0) g_player.angle += 2 * M_PI;

            break; // End of GAME_STATE_PLAYING input
        case GAME_STATE_GAME_OVER:
        case GAME_STATE_WIN:
            if (input == 'r' || input == 'R') {
                initializeGame();
                g_currentState = GAME_STATE_PLAYING;
            } else if (input == 'x' || input == 'X') {
                exit(0);
            }
            break;
    }
}


// --- Game Update Logic ---
void updateGame(float deltaTime) {
    if (g_currentState == GAME_STATE_PLAYING) {
        updateSprites(deltaTime);
        updateProjectiles(deltaTime);

        // Check for immediate game over condition
        if (g_player.health <= 0) {
            g_currentState = GAME_STATE_GAME_OVER;
            return; // Important to return to prevent further updates for a game over state
        }

        // Check for win condition (all collectibles collected AND all enemies defeated)
        int activeCollectibles = 0;
        for (int i = 0; i < g_numSprites; ++i) {
            if (g_sprites[i].isActive && !g_sprites[i].isEnemy) {
                activeCollectibles++;
            }
        }

        int enemiesAlive = 0;
        for (int i = 0; i < g_numSprites; ++i) {
            if (g_sprites[i].isActive && g_sprites[i].isEnemy) {
                enemiesAlive = 1;
                break;
            }
        }

        if (activeCollectibles == 0 && !enemiesAlive) {
            g_currentState = GAME_STATE_WIN;
            return; // Important to return
        }
    }
}

void updateSprites(float deltaTime) {
    for (int i = 0; i < g_numSprites; ++i) {
        if (g_sprites[i].isActive) {
            // Collectible interaction
            if (!g_sprites[i].isEnemy) {
                // Check if player is close enough to collect
                if (sqrtf(powf(g_player.x - g_sprites[i].x, 2) + powf(g_player.y - g_sprites[i].y, 2)) < 0.7f) {
                    g_player.score += 10;
                    g_sprites[i].isActive = 0; // Deactivate sprite
                    printf("\a"); // Beep sound (if terminal supports it)
                }
            }
            // Enemy AI
            else {
                // Simple enemy movement: move towards player
                float dx = g_player.x - g_sprites[i].x;
                float dy = g_player.y - g_sprites[i].y;
                float dist = sqrtf(dx * dx + dy * dy);

                if (dist > 1.5f) { // If not too close, move
                    float moveAmount = 1.0f * deltaTime; // Enemy speed
                    g_sprites[i].x += (dx / dist) * moveAmount;
                    g_sprites[i].y += (dy / dist) * moveAmount;

                    // Simple collision with walls for enemy (optional, more complex for proper pathfinding)
                    int enemyMapX = (int)g_sprites[i].x;
                    int enemyMapY = (int)g_sprites[i].y;
                    if (enemyMapX < 0 || enemyMapX >= MAP_WIDTH || enemyMapY < 0 || enemyMapY >= MAP_HEIGHT ||
                        g_map[enemyMapY][enemyMapX] == '#') {
                        // If enemy moved into a wall, try to move back
                        g_sprites[i].x -= (dx / dist) * moveAmount;
                        g_sprites[i].y -= (dy / dist) * moveAmount;
                    }
                }

                // Enemy attack
                // Check if player is within attack range and cooldown is over
                if (dist < 1.5f && (clock() - g_sprites[i].lastAttackTime) / (float)CLOCKS_PER_SEC > g_sprites[i].attackCooldown) {
                    g_player.health -= 10; // Player takes damage
                    g_sprites[i].lastAttackTime = clock();
                    printf("\a"); // Beep sound for damage
                }
            }
        }
    }
}

void updateProjectiles(float deltaTime) {
    for (int i = 0; i < MAX_PROJECTILES; ++i) {
        if (g_projectiles[i].isActive) {
            g_projectiles[i].x += g_projectiles[i].dirX * g_projectiles[i].speed * deltaTime;
            g_projectiles[i].y += g_projectiles[i].dirY * g_projectiles[i].speed * deltaTime;

            // Check collision with walls
            int projMapX = (int)g_projectiles[i].x;
            int projMapY = (int)g_projectiles[i].y;

            if (projMapX < 0 || projMapX >= MAP_WIDTH || projMapY < 0 || projMapY >= MAP_HEIGHT ||
                g_map[projMapY][projMapX] == '#') {
                g_projectiles[i].isActive = 0; // Projectile hit a wall
            }

            // Check collision with enemies
            for (int j = 0; j < g_numSprites; ++j) {
                if (g_sprites[j].isActive && g_sprites[j].isEnemy) {
                    float distToSprite = sqrtf(powf(g_projectiles[i].x - g_sprites[j].x, 2) + powf(g_projectiles[i].y - g_sprites[j].y, 2));
                    if (distToSprite < 0.7f) { // Collision radius (can be adjusted)
                        g_sprites[j].health -= 25; // Damage enemy
                        g_projectiles[i].isActive = 0; // Projectile hit enemy
                        printf("\a"); // Beep for hit
                        if (g_sprites[j].health <= 0) {
                            g_sprites[j].isActive = 0; // Enemy defeated
                            g_player.score += 50; // Score for defeating enemy
                        }
                        break; // Projectile is consumed after hitting an enemy
                    }
                }
            }
        }
    }
}

// --- Main Game Loop ---
int main() {
    printf("Initializing Mini Doom CLI...\n");
    printf("Make sure your terminal supports ANSI escape codes for colors.\n");

    enableRawMode(); // Set terminal to raw mode for non-blocking input
    g_lastFrameTime = clock(); // Initialize last frame time

    while (1) { // Main game loop
        clock_t currentFrameTime = clock();
        float deltaTime = (float)(currentFrameTime - g_lastFrameTime) / CLOCKS_PER_SEC;
        g_lastFrameTime = currentFrameTime;

        processInput(deltaTime); // Handle user input and update player position/state based on input
        updateGame(deltaTime);   // Update game logic (enemy AI, projectiles, game state)

        switch (g_currentState) {
            case GAME_STATE_MENU:
                renderMenu();
                break;
            case GAME_STATE_PLAYING:
                renderGame();
                break;
            case GAME_STATE_GAME_OVER:
                renderGameOver();
                break;
            case GAME_STATE_WIN:
                renderWinScreen();
                break;
        }

        // Small delay to prevent busy-waiting and reduce CPU usage, making the game refresh
        // at a roughly capped rate. Adjust for desired FPS.
        #ifdef _WIN32
            Sleep(10); // 10ms delay = ~100 FPS cap
        #else
            usleep(10000); // 10000 microseconds = 10ms delay
        #endif
    }

    // This part is technically unreachable now with `exit(0)`
    // but good practice for cleanup if game loop could exit differently.
    clearScreen();
    printf("Thanks for playing Mini Doom CLI!\n");
    printf(ANSI_COLOR_RESET); // Ensure colors are reset on exit
    disableRawMode(); // Restore terminal settings

    return 0;
}
