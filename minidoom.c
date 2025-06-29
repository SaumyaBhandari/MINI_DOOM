#include <stdio.h>
#include <stdlib.h> // For system("cls") or system("clear")
#include <math.h>   // For sin, cos, atan2, etc.
#include <string.h> // For memset

// --- ANSI Color Codes (for CLI graphics) ---
// See: https://en.wikipedia.org/wiki/ANSI_escape_code#Colors
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_WHITE_BG "\x1b[47m" // White background for "sky"
#define ANSI_COLOR_GRAY_BG  "\x1b[100m" // Dark gray background for "ground"
#define ANSI_COLOR_LIGHT_GRAY "\x1b[37m" // Light gray for distant walls

// --- Game Constants ---
#define MAP_WIDTH  20
#define MAP_HEIGHT 20
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25 // Adjusted for more viewing space
#define FOV_DEGREES 60.0f
#define FOV_RADIANS (FOV_DEGREES * M_PI / 180.0f)
#define PLAYER_MOVE_SPEED 0.5f
#define PLAYER_ROT_SPEED 0.1f // Radians per key press

// --- Map Definition ---
// A simple 2D map. '#' are walls, '.' are open spaces.
char g_map[MAP_HEIGHT][MAP_WIDTH] = {
    "####################",
    "#..................#",
    "#..########....#...#",
    "#..#.......#...#...#",
    "#..#...#...#...#...#",
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
    "#..................#",
    "#...########.......#",
    "#...#......#.......#",
    "#...#......#.......#",
    "####################"
};

// --- Player State ---
typedef struct {
    float x;
    float y;
    float angle; // Radians
} Player;

Player g_player = { .x = 2.5f, .y = 2.5f, .angle = M_PI / 4.0f }; // Start facing SE

// --- Screen Buffer for Rendering ---
// This will hold the characters to print for the current frame
char g_screenBuffer[SCREEN_HEIGHT][SCREEN_WIDTH + 1]; // +1 for null terminator if needed per row, or just for safety

// --- Function to clear the console ---
void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// --- Function to render the game world ---
void render() {
    // Fill buffer with sky and ground initially
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            if (y < SCREEN_HEIGHT / 2) {
                g_screenBuffer[y][x] = ' '; // Sky
            } else {
                g_screenBuffer[y][x] = ' '; // Ground
            }
        }
        g_screenBuffer[y][SCREEN_WIDTH] = '\0'; // Null-terminate each row
    }

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        // Calculate ray angle for this column of the screen
        float rayAngle = (g_player.angle - FOV_RADIANS / 2.0f) + ((float)x / SCREEN_WIDTH) * FOV_RADIANS;

        float distanceToWall = 0.0f;
        int hitWall = 0;
        int hitBoundary = 0; // Not used in this simplified version for shading, but good to keep

        float eyeX = sin(rayAngle); // Unit vector for ray direction
        float eyeY = cos(rayAngle);

        float testX = g_player.x;
        float testY = g_player.y;

        // Simple DDA-like traversal (Digital Differential Analyzer) - very basic
        // We'll increment until we hit a wall or go too far
        float stepSize = 0.05f; // Smaller step for more precision
        while (!hitWall && distanceToWall < 15.0f) { // Max render distance
            distanceToWall += stepSize;
            testX = g_player.x + eyeX * distanceToWall;
            testY = g_player.y + eyeY * distanceToWall;

            // Check if ray is out of bounds
            if (testX < 0 || testX >= MAP_WIDTH || testY < 0 || testY >= MAP_HEIGHT) {
                hitWall = 1; // Ray hit nothing, went out of bounds
                distanceToWall = 15.0f; // Set to max distance
            } else {
                // Check if ray hit a wall cell
                if (g_map[(int)testY][(int)testX] == '#') {
                    hitWall = 1;
                }
            }
        }

        // Calculate height of wall slice based on distance
        // Avoid division by zero and strange perspective for very close walls
        int wallHeight = 0;
        if (distanceToWall > 0.01f) { // Prevent extreme height for walls right on top of player
             wallHeight = (int)(SCREEN_HEIGHT / (distanceToWall * 1.5f)); // Scale factor for appearance
        }
        if (wallHeight > SCREEN_HEIGHT) wallHeight = SCREEN_HEIGHT; // Cap at screen height

        int ceiling = (SCREEN_HEIGHT / 2) - (wallHeight / 2);
        int floor = (SCREEN_HEIGHT / 2) + (wallHeight / 2);

        // Draw wall slice
        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            char pixel = ' ';
            char* color = ANSI_COLOR_RESET;

            if (y < ceiling) {
                // Sky - represented by space, colored by background
                pixel = ' ';
            } else if (y > floor) {
                // Ground - also space, colored by background
                // We could put different characters here for "texture"
                pixel = ' ';
            } else {
                // Wall segment
                // Vary character based on distance (simple shading)
                if (distanceToWall < 3.0f) {
                    pixel = '#';
                    color = ANSI_COLOR_MAGENTA;
                } else if (distanceToWall < 6.0f) {
                    pixel = '=';
                    color = ANSI_COLOR_CYAN;
                } else if (distanceToWall < 9.0f) {
                    pixel = '-';
                    color = ANSI_COLOR_BLUE;
                } else {
                    pixel = '.';
                    color = ANSI_COLOR_LIGHT_GRAY;
                }
            }
            g_screenBuffer[y][x] = pixel;
        }
    }

    // --- Print the entire buffer to the console ---
    clearScreen();
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        // Apply background colors for sky/ground regions
        // This is a simplified way to apply overall background colors
        if (y < SCREEN_HEIGHT / 2) {
            printf("%s", ANSI_COLOR_WHITE_BG); // Sky background
        } else {
            printf("%s", ANSI_COLOR_GRAY_BG);  // Ground background
        }

        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            // Apply foreground color for walls
            char pixel = g_screenBuffer[y][x];
             if (pixel == '#') {
                printf("%s%c", ANSI_COLOR_MAGENTA, pixel);
            } else if (pixel == '=') {
                printf("%s%c", ANSI_COLOR_CYAN, pixel);
            } else if (pixel == '-') {
                printf("%s%c", ANSI_COLOR_BLUE, pixel);
            } else if (pixel == '.') {
                printf("%s%c", ANSI_COLOR_LIGHT_GRAY, pixel);
            } else {
                printf("%c", pixel); // Sky/ground don't need foreground color
            }
        }
        printf("%s\n", ANSI_COLOR_RESET); // Reset colors and new line
    }

    // --- Print Mini-Map and Player Info ---
    printf("\n--- Mini Map ---\n");
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if ((int)g_player.x == x && (int)g_player.y == y) {
                printf(ANSI_COLOR_RED "P" ANSI_COLOR_RESET); // Player
            } else {
                printf("%c", g_map[y][x]);
            }
        }
        printf("\n");
    }
    printf("Player X: %.1f, Y: %.1f, Angle: %.2f (deg: %.1f)\n",
           g_player.x, g_player.y, g_player.angle, g_player.angle * 180.0f / M_PI);
    printf("WASD to move, QE to rotate. 'X' to exit.\n");
}

// --- Main Game Loop ---
int main() {
    printf("Initializing Mini Doom CLI...\n");
    printf("Make sure your terminal supports ANSI escape codes for colors.\n");
    printf("Press Enter to start.\n");
    getchar(); // Wait for user to acknowledge

    char input;
    int running = 1;

    // Set initial position and angle
    g_player.x = 2.5f;
    g_player.y = 2.5f;
    g_player.angle = M_PI / 4.0f; // Facing southeast initially

    while (running) {
        render(); // Draw the current frame

        // --- Input Handling ---
        // Basic blocking input for simplicity.
        // For non-blocking, you'd need platform-specific headers/libraries
        // like <conio.h> for Windows (_kbhit, _getch) or <termios.h> for Linux.
        printf("Input: ");
        input = getchar(); // Reads character + newline
        // Consume the newline character if it's there after actual input
        if (input != '\n') {
            while (getchar() != '\n' && getchar() != EOF);
        }


        float playerDirX = sin(g_player.angle);
        float playerDirY = cos(g_player.angle);

        float newPlayerX = g_player.x;
        float newPlayerY = g_player.y;

        switch (input) {
            case 'w': // Move forward
            case 'W':
                newPlayerX += playerDirX * PLAYER_MOVE_SPEED;
                newPlayerY += playerDirY * PLAYER_MOVE_SPEED;
                break;
            case 's': // Move backward
            case 'S':
                newPlayerX -= playerDirX * PLAYER_MOVE_SPEED;
                newPlayerY -= playerDirY * PLAYER_MOVE_SPEED;
                break;
            case 'a': // Strafe left (rotate 90 deg left from current view, then move)
            case 'A':
                newPlayerX += cos(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY -= sin(g_player.angle) * PLAYER_MOVE_SPEED;
                break;
            case 'd': // Strafe right
            case 'D':
                newPlayerX -= cos(g_player.angle) * PLAYER_MOVE_SPEED;
                newPlayerY += sin(g_player.angle) * PLAYER_MOVE_SPEED;
                break;
            case 'q': // Rotate left
            case 'Q':
                g_player.angle -= PLAYER_ROT_SPEED;
                break;
            case 'e': // Rotate right
            case 'E':
                g_player.angle += PLAYER_ROT_SPEED;
                break;
            case 'x': // Exit
            case 'X':
                running = 0;
                break;
        }

        // Basic collision detection: Don't move into a wall
        // Convert new float coordinates to integer map coordinates
        int mapX = (int)newPlayerX;
        int mapY = (int)newPlayerY;

        // Check if the new position is within map bounds and not a wall
        if (mapX >= 0 && mapX < MAP_WIDTH && mapY >= 0 && mapY < MAP_HEIGHT &&
            g_map[mapY][mapX] == '.') { // Only move if target cell is empty
            g_player.x = newPlayerX;
            g_player.y = newPlayerY;
        }

        // Keep angle within [0, 2*PI)
        if (g_player.angle < 0) g_player.angle += 2 * M_PI;
        if (g_player.angle >= 2 * M_PI) g_player.angle -= 2 * M_PI;
    }

    clearScreen();
    printf("Thanks for playing Mini Doom CLI!\n");
    printf(ANSI_COLOR_RESET); // Ensure colors are reset on exit
    return 0;
}