#include <Arduino.h>
#include <FastLED.h>
#include <vector>  // For tetromino definitions
#include <cstring> // For memset and snprintf
#include "wifi_credentials.h"

// ========== WIFI and WEB SERVER ========== //
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> // Includes AsyncEventSource

// <<< Your WiFi Credentials >>>
//These come from wifi_credentials.h in the ./include folder
//e.g. wifi_credentials.h content
//#ifndef WIFI_CREDENTIALS_H
//#define WIFI_CREDENTIALS_H

// WiFi credentials
//const char* ssid = "your-ssid";      // Replace with your WiFi name
//const char* password = "yourpassword";    // Replace with your WiFi password

//#endif

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create AsyncEventSource object for Server-Sent Events
AsyncEventSource events("/events");

// Flags to signal actions from web requests (volatile because accessed by web server task)
volatile bool web_req_left = false;
volatile bool web_req_right = false;
volatile bool web_req_rotate = false;
volatile bool web_req_drop = false;
volatile bool web_req_restart = false;

unsigned long lastWebActionTime = 0; // Debounce timer for web actions
const unsigned long webDebounceDelay = 150; // ms delay between web game actions

// ========== PHYSICAL MATRIX CONFIGURATION ========== //
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8
#define NUM_LEDS (MATRIX_WIDTH * MATRIX_HEIGHT)
#define DATA_PIN 4 // Set your data pin

// FastLED setup
CRGB leds[NUM_LEDS];
int currentBrightness = 40; // Adjust brightness (0-255)

// ========== GAME LOGIC CONFIGURATION ========== //
#define GAME_WIDTH 8
#define GAME_HEIGHT 32
#define LINES_PER_LEVEL 10
const unsigned long INITIAL_GAME_TICK_DELAY = 500;
const unsigned long MIN_GAME_TICK_DELAY = 100;
const float SPEED_INCREASE_FACTOR = 0.95;

// ========== LINE CLEAR EFFECT CONFIGURATION ========== //
#define LINE_CLEAR_FRAMES 5 // Number of flashes for the effect
#define LINE_CLEAR_FRAME_DELAY 50 // Milliseconds between flash frames
#define MAX_LINES_PER_CLEAR 4 // Max lines clearable at once (for Tetris)

// ========== GAME STATE & LOGIC ========== //
enum GameState {
    PLAYING,
    GAME_OVER_SEQUENCE
};
GameState currentGameState = PLAYING;

uint8_t gameBoard[GAME_WIDTH][GAME_HEIGHT];
unsigned long lastGameTick = 0;
unsigned long gameTickDelay = INITIAL_GAME_TICK_DELAY;
int score = 0;
int totalLinesCleared = 0;
int currentLevel = 0;
bool gameJustEnded = false;

// Current piece variables
int currentPieceType;
int currentPieceRotation;
int currentPieceX;
int currentPieceY;

// Tetromino shapes (T-piece corrected)
const std::vector<std::vector<std::pair<int, int>>> tetrominoes[] = {
    {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{1, 0}, {1, 1}, {1, 2}, {1, -1}}}, // I
    {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}, // O
    {   // T
        {{0, 0}, {1, 0}, {2, 0}, {1, 1}},  // Rotation 0 (Down)
        {{1, 1}, {0, 0}, {1, 0}, {1,-1}},  // Rotation 1 (Left)
        {{0, 0}, {1, 0}, {2, 0}, {1,-1}},  // Rotation 2 (Up)
        {{1, 1}, {2, 0}, {1, 0}, {1,-1}}   // Rotation 3 (Right)
    },
    {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}, {{0, 0}, {0, 1}, {1, 1}, {1, 2}}}, // S
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{1, 0}, {0, 1}, {1, 1}, {0, 2}}}, // Z
    {{{0, 0}, {1, 0}, {2, 0}, {2, 1}}, {{1, 0}, {1, 1}, {1, 2}, {0, 2}}, {{0, 1}, {1, 1}, {2, 1}, {0, 0}}, {{1, 0}, {2, 0}, {1, 1}, {1, 2}}}, // J
    {{{0, 0}, {1, 0}, {2, 0}, {0, 1}}, {{0, 0}, {1, 0}, {1, 1}, {1, 2}}, {{2, 1}, {0, 1}, {1, 1}, {2, 0}}, {{1, 0}, {1, 1}, {1, 2}, {2, 2}}}  // L
};
const int numTetrominoTypes = sizeof(tetrominoes) / sizeof(tetrominoes[0]);

// Colors for pieces
const CRGB pieceColors[] = {
    CRGB::Black, CRGB::Cyan, CRGB::Yellow, CRGB::Purple, CRGB::Green, CRGB::Red, CRGB::Blue, CRGB::Orange
};

// ========== TOUCH SENSOR SETUP ========== //
#define TOUCH_THRESHOLD 70
#define LEFT_PIN T3
#define RIGHT_PIN T4
#define ROTATE_PIN T5
#define DROP_PIN T6

unsigned long lastTouchTime = 0;
const unsigned long touchDebounceDelay = 150;
unsigned long lastGameOverInputCheck = 0;
const unsigned long gameOverInputDebounce = 300;

// ========== FONT DATA (3x5) ========== //
const uint8_t font3x5[][5] PROGMEM = { /* ... Font Data ... */
    {0b000, 0b000, 0b000, 0b000, 0b000}, {0b100, 0b100, 0b100, 0b000, 0b100}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b111, 0b101, 0b101, 0b101, 0b111}, {0b010, 0b110, 0b010, 0b010, 0b111}, {0b110, 0b001, 0b010, 0b100, 0b111}, {0b111, 0b001, 0b011, 0b001, 0b111}, {0b101, 0b101, 0b111, 0b001, 0b001}, {0b111, 0b100, 0b110, 0b001, 0b110}, {0b011, 0b100, 0b111, 0b101, 0b111}, {0b111, 0b001, 0b010, 0b100, 0b100}, {0b111, 0b101, 0b111, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b001, 0b110}, {0b000, 0b100, 0b000, 0b100, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b010, 0b001, 0b010, 0b100, 0b000}, {0b000, 0b000, 0b000, 0b000, 0b000}, {0b111, 0b101, 0b111, 0b101, 0b101}, {0b110, 0b101, 0b110, 0b101, 0b110}, {0b011, 0b100, 0b100, 0b100, 0b011}, {0b110, 0b101, 0b101, 0b101, 0b110}, {0b111, 0b100, 0b110, 0b100, 0b111}, {0b111, 0b100, 0b110, 0b100, 0b100}, {0b011, 0b100, 0b111, 0b101, 0b011}, {0b101, 0b101, 0b111, 0b101, 0b101}, {0b111, 0b010, 0b010, 0b010, 0b111}, {0b001, 0b001, 0b001, 0b101, 0b110}, {0b101, 0b110, 0b100, 0b110, 0b101}, {0b100, 0b100, 0b100, 0b100, 0b111}, {0b101, 0b111, 0b111, 0b101, 0b101}, {0b101, 0b111, 0b101, 0b101, 0b101}, {0b111, 0b101, 0b101, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b100, 0b100}, {0b111, 0b101, 0b101, 0b011, 0b001}, {0b111, 0b101, 0b110, 0b101, 0b101}, {0b011, 0b100, 0b010, 0b001, 0b110}, {0b111, 0b010, 0b010, 0b010, 0b010}, {0b101, 0b101, 0b101, 0b101, 0b111}, {0b101, 0b101, 0b101, 0b010, 0b010}, {0b101, 0b101, 0b111, 0b111, 0b101}, {0b101, 0b010, 0b010, 0b010, 0b101}, {0b101, 0b101, 0b010, 0b010, 0b010}, {0b111, 0b001, 0b010, 0b100, 0b111}
};
const int FONT_ASCII_OFFSET = 32;
const int FONT_CHAR_WIDTH = 3;
const int FONT_CHAR_HEIGHT = 5;
const int FONT_CHAR_SPACING = 1;

// ========== SCROLLING TEXT STATE ========== //
char scrollText[64];
int scrollX = 0;
unsigned long lastScrollTick = 0;
const int scrollSpeed = 100;

// ========== FUNCTION PROTOTYPES ========== //
// Game Logic
void spawnPiece();
bool isValidPosition(int pieceType, int rotation, int x, int y);
void lockPiece();
void clearLines();
void updateGame();
void resetGame();
void triggerGameOver();
void sendStatusUpdate(bool isGameOver = false);
// <<< MODIFIED Prototype for line clear effect >>>
void playMultiLineClearEffect(int linesY[], int count);

// Drawing
void drawMatrix();
void drawPiece(int pieceType, int rotation, int x, int y, CRGB color);
void drawChar(int physical_col, int physical_row_offset, char c, CRGB color);
void drawString(int physical_col, int physical_row_offset, const char *str, CRGB color);
void handleScrollingText();

// Input
void handleTouchInput();
void handleWebInput();
bool checkRestartInput();

// Mapping
int XY(int x, int y);

// Web Server Handlers
void handleRoot(AsyncWebServerRequest *request);
void handleAction(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);

// HTML page content stored in PROGMEM, including JavaScript for keyboard input
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 Tetris Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin-top: 20px; background-color: #282c34; color: white;}
    h1 { color: #61dafb; margin-bottom: 10px; }
    #status { margin-bottom: 10px; font-size: 1.2em; color: #e5c07b; } /* Yellowish */
    #status span { font-weight: bold; color: white; margin-left: 5px;}
    #gameStateMessage { color: #ff6961; font-weight: bold; font-size: 1.3em; min-height:1.5em; margin-top: 5px; margin-bottom: 10px;} /* Red */
    .button {
      background-color: #61dafb; border: none; color: #282c34; padding: 12px 28px; /* Slightly smaller */
      text-align: center; text-decoration: none; display: inline-block; font-size: 16px;
      margin: 8px 4px; cursor: pointer; border-radius: 8px; min-width: 90px; /* Slightly smaller */
      transition: background-color 0.3s ease;
    }
    .button:hover { background-color: #21a1f1; }
    .button-restart { background-color: #ff6961; }
    .button-restart:hover { background-color: #e05048; }
    #controls { margin-top: 5px; }
    #message { margin-top: 10px; font-size: 1.1em; color: #98c379; min-height: 1.5em;}
    #keyboard-info { margin-top: 20px; color: #abb2bf; font-size: 0.9em; }
  </style>
</head>
<body>
  <h1>ESP32 LED Matrix Tetris</h1>

  <div id="status">
    Score: <span id="scoreValue">0</span> |
    Level: <span id="levelValue">0</span> |
    Lines: <span id="linesValue">0</span>
  </div>
  <div id="gameStateMessage"></div>

  <div id="controls">
    <button class="button" onclick="sendAction('rotate')">Rotate (Up)</button><br>
    <button class="button" onclick="sendAction('left')">Left (Left)</button>
    <button class="button" onclick="sendAction('right')">Right (Right)</button><br>
    <button class="button" onclick="sendAction('drop')">Drop (Down)</button><br><br>
    <button class="button button-restart" onclick="sendAction('restart')">Restart Game</button>
  </div>

  <div id="message"></div>
  <div id="keyboard-info">Use Arrow Keys (Up, Down, Left, Right) to control the game.<br>
  Make sure this browser window is active/focused to use keyboard controls.</div>

<script>
const scoreElement = document.getElementById('scoreValue');
const levelElement = document.getElementById('levelValue');
const linesElement = document.getElementById('linesValue');
const messageDiv = document.getElementById('message');
const gameStateMessageDiv = document.getElementById('gameStateMessage'); // Get game over message div

// Throttling variables for keyboard input
let isThrottled = false;
const THROTTLE_DELAY = 100; // Allow one key action every 100ms

function sendAction(action) {
  messageDiv.textContent = 'Sending ' + action + '...';
  fetch('/action?move=' + action)
    .then(response => {
      if (!response.ok) { throw new Error('Network response was not ok: ' + response.statusText); }
      return response.text();
    })
    .then(data => {
      console.log('Action request successful:', data);
      messageDiv.textContent = action + ' sent!';
      setTimeout(function() { messageDiv.textContent = ''; }, 1500);
    })
    .catch((error) => {
      console.error('Error sending action:', error);
      messageDiv.textContent = 'Error sending ' + action;
    });
}

// EventSource for receiving status updates
if (!!window.EventSource) { // Check if browser supports EventSource
  var eventSource = new EventSource('/events');

  eventSource.addEventListener('open', function(e) {
    console.log("SSE Connection Opened.");
    messageDiv.textContent = "Status connection active.";
     setTimeout(function() { messageDiv.textContent = ''; }, 2500);
  }, false);

  eventSource.addEventListener('error', function(e) {
    if (e.target.readyState != EventSource.OPEN) {
      console.error("SSE Connection Error. State: " + e.target.readyState);
      gameStateMessageDiv.textContent = "Status Conn. Lost";
    }
  }, false);

  eventSource.addEventListener('update', function(e) { // Listen for 'update' event type
    console.log("SSE 'update' received:", e.data);
    try {
      const data = JSON.parse(e.data);
      // Update score, level, lines
      if (data.score !== undefined) scoreElement.textContent = data.score;
      if (data.level !== undefined) levelElement.textContent = data.level;
      if (data.lines !== undefined) linesElement.textContent = data.lines;

      // Update game state message
      if (data.state !== undefined) {
        if (data.state === "over") {
            gameStateMessageDiv.textContent = "GAME OVER";
        } else { // Assuming "playing" or other states clear the message
             gameStateMessageDiv.textContent = "";
        }
      }

    } catch (err) {
      console.error("Error parsing SSE data:", err);
    }
  }, false);

} else {
  console.log("Your browser doesn't support Server-Sent Events.");
  messageDiv.textContent = "Real-time updates not supported by browser.";
}


// Keyboard event listener with throttling
document.addEventListener('keydown', function(event) {
  // Don't process if currently throttled
  if (isThrottled) {
    // console.log("Keydown throttled"); // Optional: reduce console noise
    return;
  }

  console.log("Keydown event fired. Key:", event.key, "Code:", event.code); // Log all attempts
  let action = null;
  switch (event.key) {
    case "ArrowLeft": action = 'left'; break;
    case "ArrowRight": action = 'right'; break;
    case "ArrowUp": action = 'rotate'; break;
    case "ArrowDown": action = 'drop'; break;
    default: return;
  }

  if (action) {
    event.preventDefault();
    console.log('Mapped key:', event.key, '-> Action:', action, '-> Calling sendAction()');
    sendAction(action);

    // Apply throttle
    isThrottled = true;
    setTimeout(() => { isThrottled = false; }, THROTTLE_DELAY);
  }
});

</script>

</body>
</html>
)rawliteral";


// ========== SETUP ========== //
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32 FastLED Tetris - Starting Up...");

    // --- WiFi Connection ---
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println("");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("!!! WiFi Connection Failed !!! - Web interface unavailable.");
    }

    // --- Web Server Setup ---
    if (WiFi.status() == WL_CONNECTED) {
        server.on("/", HTTP_GET, handleRoot);
        server.on("/action", HTTP_GET, handleAction);
        server.addHandler(&events); // Attach the events handler
        server.onNotFound(handleNotFound);
        server.begin();
        Serial.println("HTTP server started. Open browser to the IP address.");
    } else {
        Serial.println("Skipping Web Server setup due to WiFi failure.");
    }


    // --- Game Setup ---
    Serial.printf("Physical Matrix: %d wide x %d high (%d LEDs)\n", MATRIX_WIDTH, MATRIX_HEIGHT, NUM_LEDS);
    Serial.printf("Game Grid Logic: %d wide x %d high\n", GAME_WIDTH, GAME_HEIGHT);
    Serial.printf("Leveling: %d lines per level\n", LINES_PER_LEVEL);
    Serial.println("Scoring: Nintendo Style (Base * (Level + 1))");
    Serial.println("Touch Sensors: T3(L), T4(R), T5(Rot), T6(Drop)");

    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(currentBrightness);
    FastLED.clear();
    FastLED.show();

    randomSeed(analogRead(A0));
    resetGame(); // Initializes game state and sends first SSE update
    Serial.println("Setup Complete. Ready to Play!");
}

// ========== MAIN LOOP ========== //
void loop() {
    unsigned long currentTime = millis();

    switch (currentGameState) {
    case PLAYING:
        handleTouchInput();
        handleWebInput();

        if (currentTime - lastGameTick >= gameTickDelay) {
            lastGameTick = currentTime;
            updateGame();
        }
        drawMatrix(); // Normal draw during play
        break;

    case GAME_OVER_SEQUENCE:
        handleScrollingText(); // Draw scrolling text instead of board

        if (currentTime - lastGameOverInputCheck > gameOverInputDebounce) {
            if (checkRestartInput()) {
                Serial.println("\nRestarting game...");
                resetGame(); // Resets state and sends SSE update
                web_req_restart = false;
            }
        }
        break;
    }

    // FastLED.show() is only called here, AFTER drawing functions
    FastLED.show();
    delay(10);
}

// ========== WEB SERVER HANDLERS ========== //

void handleRoot(AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
}

void handleAction(AsyncWebServerRequest *request) {
    String message = "OK";
    bool success = false;
    if (request->hasParam("move")) {
        String move = request->getParam("move")->value();
        Serial.print("Web Request Received (handleAction): move="); Serial.println(move);

        if (move == "left")       { web_req_left = true; success = true; }
        else if (move == "right") { web_req_right = true; success = true; }
        else if (move == "rotate"){ web_req_rotate = true; success = true; }
        else if (move == "drop")  { web_req_drop = true; success = true; }
        else if (move == "restart") { web_req_restart = true; success = true; }
        else { message = "Invalid Action"; Serial.println("--> Invalid action parameter received."); }
    } else {
        message = "No Action Parameter"; Serial.println("--> Web request received without 'move' parameter.");
    }

    if(success) { request->send(200, "text/plain", message); }
    else { request->send(400, "text/plain", message); }
}

void handleNotFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}


// ========== GAME LOGIC FUNCTIONS ========== //

// Helper function to send status updates via SSE
void sendStatusUpdate(bool isGameOver) {
    if(WiFi.status() == WL_CONNECTED && events.count() > 0) {
        char sseBuffer[120];
        const char* gameStateStr = (isGameOver || currentGameState == GAME_OVER_SEQUENCE) ? "over" : "playing";
        snprintf(sseBuffer, sizeof(sseBuffer),
                 "{\"score\":%d, \"level\":%d, \"lines\":%d, \"state\":\"%s\"}",
                 score, currentLevel, totalLinesCleared, gameStateStr);
        events.send(sseBuffer, "update", millis());
        // Serial.printf("SSE Sent: %s\n", sseBuffer);
    }
}

void resetGame() {
    Serial.println("Initializing new game state...");
    score = 0;
    totalLinesCleared = 0;
    currentLevel = 0;
    gameTickDelay = INITIAL_GAME_TICK_DELAY;
    memset(gameBoard, 0, sizeof(gameBoard));
    currentGameState = PLAYING; // Set state to PLAYING *before* sending update
    lastGameTick = millis();
    gameJustEnded = false;
    scrollText[0] = '\0';
    scrollX = 0;

    web_req_left = false; web_req_right = false; web_req_rotate = false; web_req_drop = false; web_req_restart = false;

    FastLED.clear(false);
    spawnPiece();

    sendStatusUpdate();
    drawMatrix(); // Draw initial board + piece
    FastLED.show(); // Show it on the display
}

void triggerGameOver() {
    if (currentGameState == PLAYING) {
        currentGameState = GAME_OVER_SEQUENCE; // Set state first
        gameJustEnded = true;
        lastGameOverInputCheck = millis();
        Serial.println("\n>> GAME OVER DETECTED <<");
        Serial.print("Final Score: "); Serial.println(score);
        Serial.print("Final Level: "); Serial.println(currentLevel);
        Serial.print("Total Lines Cleared: "); Serial.println(totalLinesCleared);
        sendStatusUpdate(true); // Send GAME OVER status update via SSE
    }
}

void spawnPiece() {
    currentPieceType = random(numTetrominoTypes);
    currentPieceRotation = 0;
    currentPieceX = GAME_WIDTH / 2 - 1;
    currentPieceY = GAME_HEIGHT - 1;
    if (!isValidPosition(currentPieceType, currentPieceRotation, currentPieceX, currentPieceY)) {
        triggerGameOver();
    }
}

void lockPiece() {
    size_t num_rotations = tetrominoes[currentPieceType].size();
    if (num_rotations == 0) return;
    int currentRotationIndex = currentPieceRotation % (int)num_rotations;
    const auto &pieceShape = tetrominoes[currentPieceType][currentRotationIndex];
    uint8_t colorIndex = currentPieceType + 1;
    bool piecePartiallyOffTop = false;

    for (const auto &block : pieceShape) {
        int lockX = currentPieceX + block.first;
        int lockY = currentPieceY + block.second;
        if (lockX >= 0 && lockX < GAME_WIDTH && lockY >= 0 && lockY < GAME_HEIGHT) {
            gameBoard[lockX][lockY] = colorIndex;
        } else if (lockY >= GAME_HEIGHT) {
            piecePartiallyOffTop = true;
        }
    }

    if (piecePartiallyOffTop) { triggerGameOver(); return; }
    clearLines(); // Handles scoring, leveling, SSE update and line clear effect
    if (currentGameState == PLAYING) { spawnPiece(); }
}

void updateGame() {
    if (currentGameState != PLAYING) return;
    if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX, currentPieceY - 1)) {
        currentPieceY--;
    } else {
        lockPiece();
    }
}

bool isValidPosition(int pieceType, int rotation, int x, int y) {
    size_t num_rotations = tetrominoes[pieceType].size();
    if (num_rotations == 0) return false;
    int currentRotationIndex = rotation % (int)num_rotations;
    const auto &pieceShape = tetrominoes[pieceType][currentRotationIndex];
    for (const auto &block : pieceShape) {
        int checkX = x + block.first; int checkY = y + block.second;
        if (checkX < 0 || checkX >= GAME_WIDTH || checkY < 0) return false;
        if (checkY < GAME_HEIGHT) { if (gameBoard[checkX][checkY] != 0) return false; }
    }
    return true;
}

// <<< NEW function for MULTI-line clear animation >>>
void playMultiLineClearEffect(int linesY[], int count) {
    if (count <= 0) return; // No lines to animate

    // Store original colors temporarily if needed (not strictly necessary here)
    // Or just rely on drawMatrix to redraw correctly after

    // Animation loop
    for (int frame = 0; frame < LINE_CLEAR_FRAMES; ++frame) {
        // Set colors for all cleared lines
        for (int i = 0; i < count; ++i) { // Iterate through cleared lines
            int gameY = linesY[i];
            for (int x = 0; x < GAME_WIDTH; ++x) { // Iterate through columns
                int ledIndex = XY(x, gameY);
                if (ledIndex != -1) {
                    // Set to a random bright color (HSV for easy rainbow)
                    leds[ledIndex] = CHSV(random8(), 255, 255);
                }
            }
        }
        FastLED.show(); // Show this frame
        delay(LINE_CLEAR_FRAME_DELAY); // Pause between frames
    }

    // Clear the lines in the LED buffer after the effect
    for (int i = 0; i < count; ++i) {
        int gameY = linesY[i];
         for (int x = 0; x < GAME_WIDTH; ++x) {
             int ledIndex = XY(x, gameY);
             if (ledIndex != -1) {
                leds[ledIndex] = CRGB::Black;
             }
         }
    }
    // Don't show here - let the subsequent game state drawing handle it
    // FastLED.show();
}


// <<< MODIFIED clearLines function >>>
void clearLines() {
    int clearedLinesY[MAX_LINES_PER_CLEAR]; // Store Y coords of cleared lines
    int numLinesFound = 0;

    // --- Pass 1: Find all full lines ---
    for (int y = 0; y < GAME_HEIGHT; ++y) {
        bool lineFull = true;
        for (int x = 0; x < GAME_WIDTH; ++x) {
            if (gameBoard[x][y] == 0) {
                lineFull = false;
                break;
            }
        }
        if (lineFull) {
            if (numLinesFound < MAX_LINES_PER_CLEAR) { // Avoid buffer overflow
                clearedLinesY[numLinesFound++] = y;
            }
            // Don't clear/shift yet, just record
        }
    }

    // --- If lines were found, process them ---
    if (numLinesFound > 0) {
        // --- Play simultaneous animation ---
        playMultiLineClearEffect(clearedLinesY, numLinesFound);

        // --- Shift down remaining lines ---
        int writeY = 0; // Where the next non-cleared row should be written
        for (int readY = 0; readY < GAME_HEIGHT; ++readY) {
            bool wasCleared = false;
            // Check if the readY row was one of the cleared ones
            for (int i = 0; i < numLinesFound; ++i) {
                if (readY == clearedLinesY[i]) {
                    wasCleared = true;
                    break;
                }
            }

            // If the row was NOT cleared, copy it down to the writeY position
            if (!wasCleared) {
                // Only copy if necessary (readY is different from writeY)
                if (readY != writeY) {
                    for (int x = 0; x < GAME_WIDTH; ++x) {
                        gameBoard[x][writeY] = gameBoard[x][readY];
                    }
                }
                writeY++; // Move to the next position to write to
            }
            // If it *was* cleared, we just skip it, effectively deleting it
        }

        // --- Clear the top rows that are now empty ---
        for (int y = writeY; y < GAME_HEIGHT; ++y) {
             for (int x = 0; x < GAME_WIDTH; ++x) {
                 gameBoard[x][y] = 0;
             }
        }

        // --- Update Score, Level, and Status ---
        int linesClearedThisTurn = numLinesFound; // Use the count found earlier
        bool levelChanged = false;
        int scoreMultiplier = currentLevel + 1;
        int baseScore = 0;
        if (linesClearedThisTurn == 1) baseScore = 40; else if (linesClearedThisTurn == 2) baseScore = 100; else if (linesClearedThisTurn == 3) baseScore = 300; else if (linesClearedThisTurn >= 4) baseScore = 1200;
        int pointsEarned = baseScore * scoreMultiplier;
        score += pointsEarned;
        totalLinesCleared += linesClearedThisTurn;

        Serial.print("Lines Cleared: "); Serial.print(linesClearedThisTurn); Serial.print(" | Points Earned: "); Serial.print(pointsEarned); Serial.print(" | Score: "); Serial.print(score); Serial.print(" | Total Lines: "); Serial.println(totalLinesCleared);

        int oldLevel = currentLevel;
        int targetLevel = totalLinesCleared / LINES_PER_LEVEL;
        if (targetLevel > currentLevel) {
            currentLevel = targetLevel;
            levelChanged = true;
            unsigned long newDelay = (unsigned long)((float)gameTickDelay * SPEED_INCREASE_FACTOR);
            if (newDelay < MIN_GAME_TICK_DELAY) newDelay = MIN_GAME_TICK_DELAY;
            if (newDelay != gameTickDelay) { gameTickDelay = newDelay; Serial.print(">> LEVEL UP! Reached Level: "); Serial.print(currentLevel); Serial.print(" | New Speed Delay (ms): "); Serial.println(gameTickDelay); }
            else { Serial.print(">> LEVEL UP! Reached Level: "); Serial.println(currentLevel); Serial.println("   (Speed already at maximum)"); }
        }

        // Send status update via SSE since score changed (and maybe level)
        sendStatusUpdate();
    }
}

// ========== DRAWING FUNCTIONS ========== //

void handleScrollingText() {
    unsigned long currentTime = millis();
    if (gameJustEnded) {
        snprintf(scrollText, sizeof(scrollText), "GAME OVER   SCORE: %d   LEVEL: %d   ", score, currentLevel);
        scrollX = MATRIX_WIDTH;
        lastScrollTick = currentTime;
        gameJustEnded = false;
    }
    if (currentTime - lastScrollTick >= scrollSpeed) {
        scrollX--;
        lastScrollTick = currentTime;
        int textWidth = strlen(scrollText) * (FONT_CHAR_WIDTH + FONT_CHAR_SPACING);
        if (scrollX < -textWidth) { scrollX = MATRIX_WIDTH; }
    }
    FastLED.clear(false);
    int textRowOffset = (MATRIX_HEIGHT - FONT_CHAR_HEIGHT) / 2;
    drawString(scrollX, textRowOffset, scrollText, CRGB::Yellow);
}

void drawMatrix() {
    FastLED.clear(false);
    for (int x = 0; x < GAME_WIDTH; ++x) { for (int y = 0; y < GAME_HEIGHT; ++y) { if (gameBoard[x][y] != 0) { int ledIndex = XY(x, y); if (ledIndex != -1) { leds[ledIndex] = pieceColors[gameBoard[x][y]]; } } } }
    if (currentGameState == PLAYING) { drawPiece(currentPieceType, currentPieceRotation, currentPieceX, currentPieceY, pieceColors[currentPieceType + 1]); }
}

void drawPiece(int pieceType, int rotation, int x, int y, CRGB color) {
    size_t num_rotations = tetrominoes[pieceType].size();
     if (num_rotations == 0) return;
    int currentRotationIndex = rotation % (int)num_rotations;
    const auto &pieceShape = tetrominoes[pieceType][currentRotationIndex];
    for (const auto &block : pieceShape) { int drawX = x + block.first; int drawY = y + block.second; if (drawY >= 0 && drawY < GAME_HEIGHT) { int ledIndex = XY(drawX, drawY); if (ledIndex != -1) { leds[ledIndex] = color; } } }
}

void drawChar(int physical_col, int physical_row_offset, char c, CRGB color) {
    int fontIndex = c - FONT_ASCII_OFFSET;
    if (c < 32 || c > 90 || fontIndex >= (sizeof(font3x5) / sizeof(font3x5[0]))) { fontIndex = '?' - FONT_ASCII_OFFSET; if (fontIndex < 0 || fontIndex >= (sizeof(font3x5) / sizeof(font3x5[0]))) { return; } }
    uint8_t charBitmap[5];
    memcpy_P(charBitmap, &font3x5[fontIndex][0], 5);
    for (int row = 0; row < FONT_CHAR_HEIGHT; row++) { uint8_t rowData = charBitmap[row]; int target_physical_row = physical_row_offset + row; if (target_physical_row >= 0 && target_physical_row < MATRIX_HEIGHT) { for (int col = 0; col < FONT_CHAR_WIDTH; col++) { if ((rowData >> (FONT_CHAR_WIDTH - 1 - col)) & 0x01) { int target_physical_col = physical_col + col; if (target_physical_col >= 0 && target_physical_col < MATRIX_WIDTH) { int textLedIndex; int phys_x = target_physical_col; int phys_y = target_physical_row; if (phys_x % 2 == 0) { textLedIndex = phys_x * MATRIX_HEIGHT + phys_y; } else { textLedIndex = phys_x * MATRIX_HEIGHT + (MATRIX_HEIGHT - 1 - phys_y); } if (textLedIndex >= 0 && textLedIndex < NUM_LEDS) { leds[textLedIndex] = color; } } } } } }
}

void drawString(int physical_col, int physical_row_offset, const char *str, CRGB color) {
    int current_physical_col = physical_col;
    while (*str) { if (current_physical_col + FONT_CHAR_WIDTH >= 0 && current_physical_col < MATRIX_WIDTH) { drawChar(current_physical_col, physical_row_offset, *str, color); } else if (current_physical_col >= MATRIX_WIDTH) { break; } current_physical_col += FONT_CHAR_WIDTH + FONT_CHAR_SPACING; str++; }
}

// ========== INPUT FUNCTIONS ========== //

void handleTouchInput() {
    // Handles input from physical touch sensors
    unsigned long now = millis();
    if (now - lastTouchTime < touchDebounceDelay) return;
    bool actionTaken = false;
    int touchL = touchRead(LEFT_PIN); int touchR = touchRead(RIGHT_PIN); int touchRot = touchRead(ROTATE_PIN); int touchDrop = touchRead(DROP_PIN);

    if (touchL < TOUCH_THRESHOLD) { // Move Left
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX - 1, currentPieceY)) { currentPieceX--; actionTaken = true; }
    }
    else if (touchR < TOUCH_THRESHOLD) { // Move Right
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX + 1, currentPieceY)) { currentPieceX++; actionTaken = true; }
    }
    else if (touchRot < TOUCH_THRESHOLD) { // Rotate
        int nextRotation = (currentPieceRotation + 1);
        bool rotated = false;
        if (isValidPosition(currentPieceType, nextRotation, currentPieceX, currentPieceY)) { currentPieceRotation++; rotated = true; }
        else if (isValidPosition(currentPieceType, nextRotation, currentPieceX - 1, currentPieceY)) { currentPieceRotation++; currentPieceX--; rotated = true; } // Kick L
        else if (isValidPosition(currentPieceType, nextRotation, currentPieceX + 1, currentPieceY)) { currentPieceRotation++; currentPieceX++; rotated = true; } // Kick R
        if(rotated) actionTaken = true;
     }
    else if (touchDrop < TOUCH_THRESHOLD) { // Drop (Soft Drop)
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX, currentPieceY - 1)) { currentPieceY--; actionTaken = true; lastGameTick = now; }
        else { lockPiece(); actionTaken = true; }
    }

    if (actionTaken) { lastTouchTime = now; }
}

void handleWebInput() {
    // Handles input flags set by the web server (buttons or keyboard)
    unsigned long now = millis();
    if (now - lastWebActionTime < webDebounceDelay) return; // Apply debounce to web actions
    bool actionTaken = false;

    // Process flags set by the web server handler
    if (web_req_left) {
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX - 1, currentPieceY)) {
             currentPieceX--; actionTaken = true; // Serial.println("--> Processed web_req_left");
        } // else { Serial.println("--> Web left blocked."); }
        web_req_left = false;
    }
    else if (web_req_right) {
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX + 1, currentPieceY)) {
            currentPieceX++; actionTaken = true; // Serial.println("--> Processed web_req_right");
        } // else { Serial.println("--> Web right blocked."); }
        web_req_right = false;
    }
    else if (web_req_rotate) {
        int nextRotation = (currentPieceRotation + 1);
        bool rotated = false;
        if (isValidPosition(currentPieceType, nextRotation, currentPieceX, currentPieceY)) {
            currentPieceRotation++; rotated = true;
        } else if (isValidPosition(currentPieceType, nextRotation, currentPieceX - 1, currentPieceY)) { // Kick L
            currentPieceRotation++; currentPieceX--; rotated = true;
        } else if (isValidPosition(currentPieceType, nextRotation, currentPieceX + 1, currentPieceY)) { // Kick R
            currentPieceRotation++; currentPieceX++; rotated = true;
        }
        if(rotated) {
            actionTaken = true; // Serial.println("--> Processed web_req_rotate");
        } // else { Serial.println("--> Web rotate request blocked by collision check."); }
        web_req_rotate = false; // Reset flag regardless of success
    }
    else if (web_req_drop) {
        if (isValidPosition(currentPieceType, currentPieceRotation, currentPieceX, currentPieceY - 1)) {
            currentPieceY--; actionTaken = true; lastGameTick = now; // Serial.println("--> Processed web_req_drop (move)");
        } else {
            lockPiece(); actionTaken = true; // Serial.println("--> Processed web_req_drop (lock)");
        }
        web_req_drop = false;
    }

    if (actionTaken) { lastWebActionTime = now; } // Update web debounce timer
}

bool checkRestartInput() {
    // Checks both touch sensors (only during game over) and web request flag
    if (currentGameState == GAME_OVER_SEQUENCE) { if (touchRead(LEFT_PIN) < TOUCH_THRESHOLD || touchRead(RIGHT_PIN) < TOUCH_THRESHOLD || touchRead(ROTATE_PIN) < TOUCH_THRESHOLD || touchRead(DROP_PIN) < TOUCH_THRESHOLD) { return true; } } // Touch check
    if (web_req_restart) { return true; } // Web check
    return false;
}

// ========== COORDINATE MAPPING ========== //
int XY(int x, int y) {
    // Maps game grid (x,y) to physical LED index with serpentine layout
    if (x < 0 || x >= GAME_WIDTH || y < 0 || y >= GAME_HEIGHT) { return -1; }
    int phys_x = y; int phys_y = x; // Rotation
    if (phys_x < 0 || phys_x >= MATRIX_WIDTH || phys_y < 0 || phys_y >= MATRIX_HEIGHT) { return -1; }
    int ledIndex;
    if (phys_x % 2 == 0) { ledIndex = phys_x * MATRIX_HEIGHT + phys_y; } // Even columns: 0 -> H-1
    else { ledIndex = phys_x * MATRIX_HEIGHT + (MATRIX_HEIGHT - 1 - phys_y); } // Odd columns: H-1 -> 0
    if (ledIndex >= 0 && ledIndex < NUM_LEDS) { return ledIndex; } else { return -1; }
}
