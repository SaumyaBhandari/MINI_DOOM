#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

// --- Platform-specific includes for non-blocking input ---
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// --- ANSI Color Codes and Control Sequences ---
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_LIGHT_GRAY "\x1b[37m"

// ANSI control sequences for better rendering
#define ANSI_CURSOR_HOME "\x1b[H"
#define ANSI_CLEAR_SCREEN "\x1b[2J"
#define ANSI_HIDE_CURSOR "\x1b[?25l"
#define ANSI_SHOW_CURSOR "\x1b[?25h"

// --- Game Constants ---
#define MAP_WIDTH  20
#define MAP_HEIGHT 20
#define SCREEN_WIDTH 100
#define SCREEN_HEIGHT 30
#define FOV_DEGREES 66.0f
#define FOV_RADIANS (FOV_DEGREES * M_PI / 180.0f)
#define PLAYER_MOVE_SPEED 0.15f
#define PLAYER_ROT_SPEED 0.05f
#define MAX_RENDER_DISTANCE 20.0f

// Total display height including HUD and minimap
#define TOTAL_DISPLAY_HEIGHT (SCREEN_HEIGHT + 15)

// Buffer size for a single line, accounting for characters + many color codes + null terminator
#define MAX_ANSI_COLOR_CODE_LENGTH 10 // Max length of a typical color code like "\x1b[31m"
#define TOTAL_LINE_BUFFER_SIZE (SCREEN_WIDTH + SCREEN_WIDTH * MAX_ANSI_COLOR_CODE_LENGTH + 1)

// --- Map Definition ---
char g_map[MAP_HEIGHT][MAP_WIDTH] = {
    "####################",
    "#........H.........#",
    "#..########....#...#",
    "#..#.......#...#E..#",
    "#..#...D...#...#...#",
    "#..#...#...#...#...#",
    "#..#.......#...#...#",
    "#..########....#...#",
    "#..................#",
    "#....#######.......#",
    "#....#.....#.......#",
    "#....#.....#.......#",
    "#....#.....#.......#",
    "#....#######.......#",
    "#..................#",
    "#........A.........#",
    "#...########.......#",
    "#...#......#.......#",
    "#...#......#.......#",
    "####################"
};

// --- Player State ---
typedef struct {
    float x;
    float y;
    float angle;
    int health;
    int ammo;
    int score;
} Player;

Player g_player = { .x = 2.5f, .y = 2.5f, .angle = M_PI / 4.0f, .health = 100, .ammo = 10, .score = 0 };

// --- Game Object Structure ---
typedef enum {
    OBJ_HEALTH,
    OBJ_AMMO,
    OBJ_ENEMY,
} ObjectType;

typedef struct {
    float x, y;
    char displayChar;
    const char* color; // This is not strictly used in the new rendering, `color` in g_colorBuffer is
    ObjectType type;
    int active;
    int health;
} GameObject;

#define MAX_GAME_OBJECTS 10
GameObject g_gameObjects[MAX_GAME_OBJECTS];

int g_numGameObjects = 0;

// --- Door State ---
typedef struct {
    int mapX, mapY;
    int isOpen;
} Door;

#define MAX_DOORS 5
Door g_doors[MAX_DOORS];
int g_numDoors = 0;

// --- Display Buffers ---
// Main screen buffer
char g_screenBuffer[SCREEN_HEIGHT][SCREEN_WIDTH + 1];
// Color buffer to store color codes for each position (index 0-6 corresponding to colors)
char g_colorBuffer[SCREEN_HEIGHT][SCREEN_WIDTH];
// Complete display buffer including HUD
char g_displayBuffer[TOTAL_DISPLAY_HEIGHT][TOTAL_LINE_BUFFER_SIZE];
// Z-buffer for depth testing
float g_zBuffer[SCREEN_WIDTH];

// Previous frame buffer for comparison (reduces flicker)
char g_prevDisplayBuffer[TOTAL_DISPLAY_HEIGHT][TOTAL_LINE_BUFFER_SIZE];
int g_firstFrame = 1;

// --- Non-blocking input globals (Linux specific) ---
#ifndef _WIN32
static struct termios g_oldTermios;

void setupNonBlockingInput() {
    struct termios newTermios;
    tcgetattr(STDIN_FILENO, &g_oldTermios);
    newTermios = g_oldTermios;
    newTermios.c_lflag &= ~(ICANON | ECHO);
    newTermios.c_cc[VMIN] = 0;
    newTermios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void restoreBlockingInput() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTermios);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

int getch_linux() {
    char buf = 0;
    if (read(STDIN_FILENO, &buf, 1) > 0) {
        return buf;
    }
    return EOF;
}

int kbhit_linux() {
    char buf = 0;
    int bytesRead = read(STDIN_FILENO, &buf, 1);
    if (bytesRead > 0) {
        ungetc(buf, stdin);
        return 1;
    }
    return 0;
}
#endif

// --- Improved screen management ---
void initializeDisplay() {
    // Clear screen once at startup and hide cursor
    printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME ANSI_HIDE_CURSOR);
    fflush(stdout);
}

void finalizeDisplay() {
    // Show cursor and reset colors
    printf(ANSI_SHOW_CURSOR ANSI_COLOR_RESET);
    fflush(stdout);
}

void updateDisplay() {
    // Only update changed lines to reduce flicker
    printf(ANSI_CURSOR_HOME); // Move cursor to top-left
    
    for (int y = 0; y < TOTAL_DISPLAY_HEIGHT; ++y) {
        // Compare with previous frame
        if (g_firstFrame || strcmp(g_displayBuffer[y], g_prevDisplayBuffer[y]) != 0) {
            printf("\x1b[%d;1H", y + 1); // Move to specific line
            printf("%s", g_displayBuffer[y]);
            strcpy(g_prevDisplayBuffer[y], g_displayBuffer[y]); // Update previous buffer
        }
    }
    
    g_firstFrame = 0;
    fflush(stdout);
}

// --- Initialize Game Objects and Doors from map ---
void initializeGameElements() {
    g_numGameObjects = 0;
    g_numDoors = 0;
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (g_map[y][x] == 'H') {
                if (g_numGameObjects < MAX_GAME_OBJECTS) {
                    g_gameObjects[g_numGameObjects] = (GameObject){.x = x + 0.5f, .y = y + 0.5f, .displayChar = '+', .color = ANSI_COLOR_GREEN, .type = OBJ_HEALTH, .active = 1, .health = 0};
                    g_numGameObjects++;
                }
            } else if (g_map[y][x] == 'A') {
                if (g_numGameObjects < MAX_GAME_OBJECTS) {
                    g_gameObjects[g_numGameObjects] = (GameObject){.x = x + 0.5f, .y = y + 0.5f, .displayChar = '!', .color = ANSI_COLOR_YELLOW, .type = OBJ_AMMO, .active = 1, .health = 0};
                    g_numGameObjects++;
                }
            } else if (g_map[y][x] == 'E') {
                if (g_numGameObjects < MAX_GAME_OBJECTS) {
                    g_gameObjects[g_numGameObjects] = (GameObject){.x = x + 0.5f, .y = y + 0.5f, .displayChar = 'M', .color = ANSI_COLOR_RED, .type = OBJ_ENEMY, .active = 1, .health = 50};
                    g_numGameObjects++;
                }
            } else if (g_map[y][x] == 'D') {
                if (g_numDoors < MAX_DOORS) {
                    g_doors[g_numDoors] = (Door){.mapX = x, .mapY = y, .isOpen = 0};
                    g_numDoors++;
                }
            }
        }
    }
}

// --- Function to render the game world ---
void render() {
    // Clear Z-buffer and screen buffer
    for (int i = 0; i < SCREEN_WIDTH; ++i) {
        g_zBuffer[i] = MAX_RENDER_DISTANCE;
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            g_screenBuffer[y][x] = ' ';
            g_colorBuffer[y][x] = 0; // 0 = no color (reset)
        }
        g_screenBuffer[y][SCREEN_WIDTH] = '\0';
    }

    // --- Raycasting for Walls, Floor, and Ceiling ---
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        double cameraX = 2 * x / (double)SCREEN_WIDTH - 1;
        double rayDirX = sin(g_player.angle) + cos(g_player.angle) * cameraX;
        double rayDirY = cos(g_player.angle) - sin(g_player.angle) * cameraX;

        int mapX = (int)g_player.x;
        int mapY = (int)g_player.y;

        double sideDistX;
        double sideDistY;

        double deltaDistX = (rayDirX == 0) ? 1e30 : fabs(1 / rayDirX);
        double deltaDistY = (rayDirY == 0) ? 1e30 : fabs(1 / rayDirY);

        double perpWallDist = 0;

        int stepX;
        int stepY;

        int hit = 0;
        int side = -1;

        if (rayDirX < 0) {
            stepX = -1;
            sideDistX = (g_player.x - mapX) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (mapX + 1.0 - g_player.x) * deltaDistX;
        }
        if (rayDirY < 0) {
            stepY = -1;
            sideDistY = (mapY + 1.0 - g_player.y) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (g_player.y - mapY) * deltaDistY;
        }

        // Perform DDA
        while (hit == 0 && perpWallDist < MAX_RENDER_DISTANCE) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaDistX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaDistY;
                mapY += stepY;
                side = 1;
            }

            if (mapX >= 0 && mapX < MAP_WIDTH && mapY >= 0 && mapY < MAP_HEIGHT) {
                char cell = g_map[mapY][mapX];
                if (cell == '#') {
                    hit = 1;
                } else if (cell == 'D') {
                    int isDoorClosed = 0;
                    for (int i = 0; i < g_numDoors; ++i) {
                        if (g_doors[i].mapX == mapX && g_doors[i].mapY == mapY && !g_doors[i].isOpen) {
                            isDoorClosed = 1;
                            break;
                        }
                    }
                    if (isDoorClosed) {
                        hit = 1;
                    }
                }
            } else {
                // Ray went out of bounds, treat as hit at max distance
                hit = 1;
                perpWallDist = MAX_RENDER_DISTANCE; // Ensure it's beyond render distance
            }
        }
        
        // Calculate perpendicular distance to wall
        if (side == 0) {
            perpWallDist = (mapX - g_player.x + (1 - stepX) / 2) / rayDirX;
        } else {
            perpWallDist = (mapY - g_player.y + (1 - stepY) / 2) / rayDirY;
        }

        // Ensure positive distance for perspective projection
        if (perpWallDist < 0.01) perpWallDist = 0.01; // Avoid division by zero or negative distance

        g_zBuffer[x] = perpWallDist; // Store depth for sprite rendering

        int lineHeight = (int)(SCREEN_HEIGHT / perpWallDist); // Correctly scaled line height

        int drawStart = -lineHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + SCREEN_HEIGHT / 2;
        if (drawEnd >= SCREEN_HEIGHT) drawEnd = SCREEN_HEIGHT - 1;

        char wallChar;
        char wallColor; // Use an integer index for color

        if (perpWallDist < MAX_RENDER_DISTANCE) {
            // Adjust character based on distance for pseudo-shading
            if (perpWallDist < 3.0f) {
                wallChar = '#';
            } else if (perpWallDist < 6.0f) {
                wallChar = '=';
            } else if (perpWallDist < 9.0f) {
                wallChar = '-';
            } else {
                wallChar = '.';
            }

            // Assign color based on wall side
            if (side == 1) { // Y-side wall
                wallColor = 1; // Cyan
            } else { // X-side wall
                wallColor = 2; // Blue
            }
        } else {
            wallChar = ' '; // Beyond render distance, draw nothing
            wallColor = 0;  // No specific color
        }

        // Draw the wall slice
        for (int y = drawStart; y <= drawEnd; ++y) {
            g_screenBuffer[y][x] = wallChar;
            g_colorBuffer[y][x] = wallColor;
        }

        // Floor and Ceiling (drawing from bottom/top of wall slice)
        double playerHeight = SCREEN_HEIGHT / 2.0;

        for (int y = drawEnd + 1; y < SCREEN_HEIGHT; ++y) { // Draw floor
            double currentDist = playerHeight / (y - playerHeight);
            if (currentDist < 0.01) currentDist = 0.01;

            char floorChar;
            if (currentDist < 2.0f) floorChar = '#';
            else if (currentDist < 4.0f) floorChar = '=';
            else if (currentDist < 6.0f) floorChar = '-';
            else if (currentDist < 10.0f) floorChar = ',';
            else floorChar = ' ';

            g_screenBuffer[y][x] = floorChar;
            g_colorBuffer[y][x] = 3; // Light Gray
        }

        for (int y = drawStart - 1; y >= 0; --y) { // Draw ceiling
            double currentDist = playerHeight / (playerHeight - y);
            if (currentDist < 0.01) currentDist = 0.01;

            char ceilingChar;
            if (currentDist < 2.0f) ceilingChar = '#';
            else if (currentDist < 4.0f) ceilingChar = '=';
            else if (currentDist < 6.0f) ceilingChar = '-';
            else if (currentDist < 10.0f) ceilingChar = ',';
            else ceilingChar = ' ';

            g_screenBuffer[y][x] = ceilingChar;
            g_colorBuffer[y][x] = 3; // Light Gray
        }
    }

    // --- Render Game Objects (Sprites) ---
    // Sort objects by distance (farthest to closest) for proper overdrawing without complex z-buffering
    // (Though current Z-buffer handles this, sorting for sprites can sometimes simplify depth issues)
    // The current Z-buffer is per-column, so closest-to-farthest for objects is often what you need for this.
    // Your sort from closest to farthest, then use Z-buffer. This is fine.
    for (int i = 0; i < g_numGameObjects - 1; ++i) {
        for (int j = i + 1; j < g_numGameObjects; ++j) {
            double distA = pow(g_player.x - g_gameObjects[i].x, 2) + pow(g_player.y - g_gameObjects[i].y, 2);
            double distB = pow(g_player.x - g_gameObjects[j].x, 2) + pow(g_player.y - g_gameObjects[j].y, 2);
            if (distA < distB) { // Sorts closest first
                GameObject temp = g_gameObjects[i];
                g_gameObjects[i] = g_gameObjects[j];
                g_gameObjects[j] = temp;
            }
        }
    }

    for (int i = 0; i < g_numGameObjects; ++i) {
        if (!g_gameObjects[i].active) continue;

        double spriteX = g_gameObjects[i].x - g_player.x;
        double spriteY = g_gameObjects[i].y - g_player.y;

        // Calculate sprite angle relative to player's view
        double spriteAngle = atan2(spriteY, spriteX);
        double relativeAngle = spriteAngle - g_player.angle;

        // Normalize relative angle to be within (-PI, PI]
        while (relativeAngle > M_PI) relativeAngle -= 2 * M_PI;
        while (relativeAngle <= -M_PI) relativeAngle += 2 * M_PI;

        double distance = sqrt(spriteX * spriteX + spriteY * spriteY);

        // Check if sprite is in FOV and within render distance
        if (distance > 0.1 && fabs(relativeAngle) < FOV_RADIANS / 2.0 && distance < MAX_RENDER_DISTANCE) {
            // Project sprite onto screen
            // The 0.5 * FOV_RADIANS is the half FOV angle, mapping to half screen width
            double screenX = (SCREEN_WIDTH / 2.0) + (relativeAngle / (FOV_RADIANS / 2.0)) * (SCREEN_WIDTH / 2.0);

            int spriteHeight = (int)(SCREEN_HEIGHT / distance); // Size scales with distance
            int spriteWidth = (int)(spriteHeight * 0.75); // Aspect ratio approximation

            int drawStart_Y = -spriteHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawStart_Y < 0) drawStart_Y = 0;
            int drawEnd_Y = spriteHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawEnd_Y >= SCREEN_HEIGHT) drawEnd_Y = SCREEN_HEIGHT - 1;

            int drawStart_X = (int)(screenX - spriteWidth / 2);
            int drawEnd_X = (int)(screenX + spriteWidth / 2);

            // Draw sprite column by column
            for (int stripe = drawStart_X; stripe < drawEnd_X; ++stripe) {
                if (stripe >= 0 && stripe < SCREEN_WIDTH && distance < g_zBuffer[stripe]) {
                    // Only draw if within screen bounds and closer than current wall/object at this column
                    for (int y = drawStart_Y; y <= drawEnd_Y; ++y) {
                        if (y >= 0 && y < SCREEN_HEIGHT) {
                            g_screenBuffer[y][stripe] = g_gameObjects[i].displayChar;
                            // Set color based on object type
                            if (g_gameObjects[i].type == OBJ_HEALTH) g_colorBuffer[y][stripe] = 4; // Green
                            else if (g_gameObjects[i].type == OBJ_AMMO) g_colorBuffer[y][stripe] = 5; // Yellow
                            else if (g_gameObjects[i].type == OBJ_ENEMY) g_colorBuffer[y][stripe] = 6; // Red
                        }
                    }
                }
            }
        }
    }

    // --- Build complete display buffer with proper color handling ---
    int displayRow = 0;
    
    // Main screen with colors
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        int bufferPos = 0;
        const char* lastColorCode = ""; // Track the last applied color code
        
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            char pixel = g_screenBuffer[y][x];
            char color_idx = g_colorBuffer[y][x]; // This is the index (1-6) or 0
            
            const char* currentColorCode = "";
            switch (color_idx) {
                case 1: currentColorCode = ANSI_COLOR_CYAN; break;
                case 2: currentColorCode = ANSI_COLOR_BLUE; break;
                case 3: currentColorCode = ANSI_COLOR_LIGHT_GRAY; break;
                case 4: currentColorCode = ANSI_COLOR_GREEN; break;
                case 5: currentColorCode = ANSI_COLOR_YELLOW; break;
                case 6: currentColorCode = ANSI_COLOR_RED; break;
                default: currentColorCode = ANSI_COLOR_RESET; break; // Default to reset
            }
            
            // Only apply color code if it's different from the last one
            if (strcmp(currentColorCode, lastColorCode) != 0) {
                bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                      TOTAL_LINE_BUFFER_SIZE - bufferPos, "%s", currentColorCode);
                lastColorCode = currentColorCode;
            }
            
            // Add the character
            bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                  TOTAL_LINE_BUFFER_SIZE - bufferPos, "%c", pixel);
        }
        // Ensure reset at the end of the line
        bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                              TOTAL_LINE_BUFFER_SIZE - bufferPos, "%s", ANSI_COLOR_RESET);
        g_displayBuffer[displayRow][bufferPos] = '\0'; // Null-terminate the string
        displayRow++;
    }

    // HUD
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, 
            "----------------------------------------------------------------------------------------------------");
    displayRow++;
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, 
            "%sHEALTH: %d  %s|  %sAMMO: %d  %s|  %sSCORE: %d%s",
            ANSI_COLOR_GREEN, g_player.health, ANSI_COLOR_RESET, 
            ANSI_COLOR_YELLOW, g_player.ammo, ANSI_COLOR_RESET, 
            ANSI_COLOR_CYAN, g_player.score, ANSI_COLOR_RESET);
    displayRow++;
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, 
            "----------------------------------------------------------------------------------------------------");
    displayRow++;

    // Mini-Map
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, "");
    displayRow++;
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, "--- Mini Map ---");
    displayRow++;
    
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        int bufferPos = 0;
        for (int x = 0; x < MAP_WIDTH; ++x) {
            int isDoor = 0;
            for(int i = 0; i < g_numDoors; ++i) {
                if (g_doors[i].mapX == x && g_doors[i].mapY == y) {
                    if (g_doors[i].isOpen) {
                        bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                            TOTAL_LINE_BUFFER_SIZE - bufferPos, "%sO%s", ANSI_COLOR_GREEN, ANSI_COLOR_RESET);
                    } else {
                        bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                            TOTAL_LINE_BUFFER_SIZE - bufferPos, "%sD%s", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);
                    }
                    isDoor = 1;
                    break;
                }
            }
            if (isDoor) continue; // If it was a door, skip map char check

            if ((int)g_player.x == x && (int)g_player.y == y) {
                bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                    TOTAL_LINE_BUFFER_SIZE - bufferPos, "%sP%s", ANSI_COLOR_RED, ANSI_COLOR_RESET);
            } else {
                char mapChar = g_map[y][x];
                if (mapChar == '#') {
                    bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                        TOTAL_LINE_BUFFER_SIZE - bufferPos, "%c", mapChar);
                } else if (mapChar == '.') {
                    bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                        TOTAL_LINE_BUFFER_SIZE - bufferPos, " ");
                } else { // For other items like H, A, E on the map
                    bufferPos += snprintf(g_displayBuffer[displayRow] + bufferPos, 
                                        TOTAL_LINE_BUFFER_SIZE - bufferPos, "%s%c%s", ANSI_COLOR_MAGENTA, mapChar, ANSI_COLOR_RESET);
                }
            }
        }
        g_displayBuffer[displayRow][bufferPos] = '\0'; // Null-terminate minimap line
        displayRow++;
    }
    
    // Player info and controls
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, 
            "Player X: %.1f, Y: %.1f, Angle: %.2f (deg: %.1f)",
            g_player.x, g_player.y, g_player.angle, g_player.angle * 180.0f / M_PI);
    displayRow++;
    snprintf(g_displayBuffer[displayRow], TOTAL_LINE_BUFFER_SIZE, 
            "Controls: WASD (Move), QE (Rotate), F (Interact), SPACE (Shoot), X (Exit)");
    displayRow++;

    // Fill remaining buffer lines
    while (displayRow < TOTAL_DISPLAY_HEIGHT) {
        g_displayBuffer[displayRow][0] = '\0'; // Clear the line
        displayRow++;
    }

    updateDisplay();
}

// --- Collision detection ---
int checkCollision(float newX, float newY) {
    int mapX = (int)newX;
    int mapY = (int)newY;

    if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) {
        return 1; // Collision if out of bounds
    }

    char cell = g_map[mapY][mapX];
    if (cell == '#') {
        return 1; // Wall collision
    }
    if (cell == 'D') {
        for (int i = 0; i < g_numDoors; ++i) {
            if (g_doors[i].mapX == mapX && g_doors[i].mapY == mapY && !g_doors[i].isOpen) {
                return 1; // Closed door collision
            }
        }
    }
    return 0; // No collision
}

// --- Handle Interactions ---
void handleInteraction() {
    for (int i = 0; i < g_numDoors; ++i) {
        float doorX = g_doors[i].mapX + 0.5f;
        float doorY = g_doors[i].mapY + 0.5f;
        float dist = sqrt(pow(g_player.x - doorX, 2) + pow(g_player.y - doorY, 2));

        if (dist < 1.5f) { // Close enough to interact with door
            g_doors[i].isOpen = !g_doors[i].isOpen; // Toggle door state
            return;
        }
    }

    for (int i = 0; i < g_numGameObjects; ++i) {
        if (!g_gameObjects[i].active) continue; // Skip inactive objects

        float objX = g_gameObjects[i].x;
        float objY = g_gameObjects[i].y;
        float dist = sqrt(pow(g_player.x - objX, 2) + pow(g_player.y - objY, 2));

        if (dist < 0.8f) { // Close enough to pick up/interact with object
            switch (g_gameObjects[i].type) {
                case OBJ_HEALTH:
                    g_player.health += 25;
                    if (g_player.health > 100) g_player.health = 100; // Cap health
                    break;
                case OBJ_AMMO:
                    g_player.ammo += 10;
                    break;
                case OBJ_ENEMY:
                    // Player cannot "pick up" enemies in this context,
                    // but interaction key could trigger melee attack if implemented
                    break;
            }
            g_gameObjects[i].active = 0; // Deactivate object after interaction
            return;
        }
    }
}

// --- Handle Shooting ---
void handleShooting() {
    if (g_player.ammo <= 0) {
        return; // No ammo
    }

    g_player.ammo--; // Consume ammo

    float rayLength = 0.0f;
    float stepSize = 0.1f; // Smaller steps for more precise hit detection
    float maxShootDistance = 10.0f;

    float eyeX = sin(g_player.angle); // Player's forward X direction
    float eyeY = cos(g_player.angle); // Player's forward Y direction

    while (rayLength < maxShootDistance) {
        float testX = g_player.x + eyeX * rayLength;
        float testY = g_player.y + eyeY * rayLength;

        // Check for collision with walls/doors first (optional, but realistic for bullets)
        int mapTestX = (int)testX;
        int mapTestY = (int)testY;
        if (mapTestX >= 0 && mapTestX < MAP_WIDTH && mapTestY >= 0 && mapTestY < MAP_HEIGHT) {
            char cell = g_map[mapTestY][mapTestX];
            if (cell == '#') {
                return; // Bullet hit a wall
            }
            if (cell == 'D') {
                 int isDoorClosed = 0;
                for (int i = 0; i < g_numDoors; ++i) {
                    if (g_doors[i].mapX == mapTestX && g_doors[i].mapY == mapTestY && !g_doors[i].isOpen) {
                        isDoorClosed = 1;
                        break;
                    }
                }
                if (isDoorClosed) {
                    return; // Bullet hit a closed door
                }
            }
        }


        // Check for collision with enemies
        for (int i = 0; i < g_numGameObjects; ++i) {
            if (g_gameObjects[i].active && g_gameObjects[i].type == OBJ_ENEMY) {
                float distToEnemy = sqrt(pow(testX - g_gameObjects[i].x, 2) + pow(testY - g_gameObjects[i].y, 2));
                if (distToEnemy < 0.5f) { // If ray is close enough to enemy center
                    g_gameObjects[i].health -= 25; // Apply damage
                    if (g_gameObjects[i].health <= 0) {
                        g_gameObjects[i].active = 0; // Enemy defeated
                        g_player.score += 100; // Award score
                    }
                    return; // Bullet hit an enemy, stop ray
                }
            }
        }
        rayLength += stepSize; // Advance ray
    }
}

int main() {
#ifndef _WIN32
    setupNonBlockingInput();
#endif
    printf("Initializing Mini Doom CLI...\n"); 

    // Initialize display and game elements
    initializeDisplay();
    initializeGameElements();

    int gameRunning = 1;
    char inputChar;

    // Main game loop
    while (gameRunning) {
        // --- Input Handling ---
#ifdef _WIN32
        if (_kbhit()) {
            inputChar = _getch();
        } else {
            inputChar = 0; // No input
        }
#else
        inputChar = getch_linux();
#endif

        float newPlayerX = g_player.x;
        float newPlayerY = g_player.y;
        int moveAttempt = 0;

        // Process player input
        switch (inputChar) {
            case 'w':
            case 'W':
                newPlayerX += sin(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY += cos(g_player.angle) * PLAYER_MOVE_SPEED;
                moveAttempt = 1;
                break;
            case 's':
            case 'S':
                newPlayerX -= sin(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY -= cos(g_player.angle) * PLAYER_MOVE_SPEED;
                moveAttempt = 1;
                break;
            case 'a':
            case 'A':
                // Strafe left (perpendicular to current view)
                newPlayerX += cos(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY -= sin(g_player.angle) * PLAYER_MOVE_SPEED;
                moveAttempt = 1;
                break;
            case 'd':
            case 'D':
                // Strafe right (perpendicular to current view)
                newPlayerX -= cos(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY += sin(g_player.angle) * PLAYER_MOVE_SPEED;
                moveAttempt = 1;
                break;
            case 'q':
            case 'Q':
                g_player.angle -= PLAYER_ROT_SPEED;
                break;
            case 'e':
            case 'E':
                g_player.angle += PLAYER_ROT_SPEED;
                break;
            case 'f':
            case 'F':
                handleInteraction();
                break;
            case ' ': // Space bar for shooting
                handleShooting();
                break;
            case 'x':
            case 'X':
                gameRunning = 0; // Set flag to exit game
                break;
        }

        // Apply movement if no collision occurs
        if (moveAttempt && !checkCollision(newPlayerX, newPlayerY)) {
            g_player.x = newPlayerX;
            g_player.y = newPlayerY;
        }
        // Basic slide collision resolution (try moving along one axis if direct move fails)
        else if (moveAttempt) {
            // Try moving only in X direction
            if (!checkCollision(newPlayerX, g_player.y)) {
                g_player.x = newPlayerX;
            }
            // Try moving only in Y direction
            else if (!checkCollision(g_player.x, newPlayerY)) {
                g_player.y = newPlayerY;
            }
        }

        // --- Game Logic Update ---
        // For a more complete game, enemy AI, health regeneration/damage over time,
        // and other dynamic elements would be updated here.
        if (g_player.health <= 0) {
            gameRunning = 0; // End game if player health reaches zero
        }

        // --- Render Frame ---
        render();

        // --- Frame Rate Control ---
#ifdef _WIN32
        Sleep(50); // Pause for 50 milliseconds (approx. 20 FPS)
#else
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50 * 1000000; // 50 milliseconds in nanoseconds
        nanosleep(&ts, NULL);
#endif
    }

    // --- Game Teardown ---
    finalizeDisplay();
#ifndef _WIN32
    restoreBlockingInput(); // Restore terminal settings on Linux
#endif
    printf("Game Over! Your Score: %d\n", g_player.score);
    return 0;
}