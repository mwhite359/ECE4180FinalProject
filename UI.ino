#include "LGFX_Config.h"
#include <LovyanGFX.hpp>
#include <math.h>
#include <HardwareSerial.h>
#include <Preferences.h>    

LGFX gfx;                 
Preferences prefs;        //Preferences object to access non volitile namespace 

// buttons used to interface with uLCD screen
#define confirm_btn 15         
#define select_btn 7

// configure UART pins for esp32 wired communication
HardwareSerial ScoreSerial(1);
#define SCORE_RX_PIN 8
#define SCORE_TX_PIN 9
String scoreLine = ""; 
int high_score;   

// states used for button/tap debounce
bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;
const int BUTTON_DEBOUNCE_MS = 200;
unsigned long lastTapTime = 0;
const int TAP_DEBOUNCE_MS = 200;  

// timer to keep track of game duration
unsigned long gameStartTime = 0;
const unsigned long GAME_DURATION_MS = 45000;  // 45 seconds

// score trackers for current and high score
int currScore = 0;
int newScore = 0;

// game difficulty states
enum Difficulty {
  DIFF_EASY = 0,
  DIFF_MEDIUM = 1,
  DIFF_HARD = 2
};

Difficulty currentDifficulty = DIFF_EASY;

// game states for UI display
enum GameState {          
  STATE_HOME,
  STATE_CONFIRM,
  STATE_PLAYING,
  STATE_ENDGAME
};

GameState gameState = STATE_HOME;

//function to draw a yellow star on UI
void drawStar(LGFX &gfx, int cx, int cy, int outerR, uint16_t color)
{
    const int n = 5;                    // 5-point star
    float outerStep = 2 * M_PI / n;     
    float innerStep = outerStep / 2;    
    float startAngle = -M_PI / 2;       

    float innerR = outerR * 0.5f;       

    int16_t x[10];
    int16_t y[10];

    // Alternating points to build star
    for (int i = 0; i < 10; i++) {
        float angle = startAngle + (i * innerStep);
        float r = (i % 2 == 0) ? outerR : innerR;
        x[i] = cx + cosf(angle) * r;
        y[i] = cy + sinf(angle) * r;
    }

    // Fill star with color
    for (int i = 0; i < 10; i++) {
        int next = (i + 1) % 10;   // Wrap around to close star
        gfx.fillTriangle(cx, cy, x[i], y[i], x[next], y[next], color);
    }
}

// UI used to bold the difficulty we are selecting 
char* words[3] = {"Easy", "Medium", "Hard"};
void drawDifficultySelection()
{
  gfx.setFont(&FreeSerifBold12pt7b);
  gfx.setTextDatum(top_center);

  for (int i = 0; i < 3; i++) {
    // Highlight the currently selected difficulty
    if (i == (int)currentDifficulty) {
      gfx.setTextColor(TFT_YELLOW, TFT_RED);  // yellow text on red background
    } else {
      gfx.setTextColor(TFT_WHITE, TFT_RED);   // white text on red background
    }
  
    char* currWord = words[i];
    gfx.drawString(currWord, 480 / 2, 80 + 60 * (i+1)); 
  }
}

// UI for home screen
void drawHomeScreen() {
  gfx.fillScreen(TFT_RED);

  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextSize(1);
  gfx.setTextColor(TFT_WHITE);

  // Draw centered Welcome
  const char* title = "Welcome!";
  int16_t textW = gfx.textWidth(title);
  int16_t textH = gfx.fontHeight();

  int16_t x = (480 - textW) / 2;   // centered x position
  gfx.setCursor(x, 20);
  gfx.println(title);

  // Draw yellow stars
  int textMidY = 20 + (textH - 5) / 2;    // vertical center of the text line
  drawStar(gfx, x - 30 - 10, textMidY, 20, TFT_YELLOW);
  drawStar(gfx, x + textW + 30 + 10, textMidY, 20, TFT_YELLOW);

  gfx.setFont(&FreeSerifBold12pt7b);
  gfx.setTextColor(TFT_WHITE);
  gfx.setTextDatum(top_center);
  const char* sub_title = "Select a difficulty:";
  int16_t txtW = gfx.textWidth(sub_title);
  gfx.drawString(sub_title, (480 - txtW) / 2, 90);

  drawDifficultySelection();      //call function to bold current selection
}

// UI for confirm screen
void drawConfirmScreen()
{
  gfx.fillScreen(TFT_RED);

  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextColor(TFT_WHITE, TFT_RED);
  gfx.setTextDatum(top_center);

  const char* line1 = "You've selected";
  gfx.drawString(line1, 480 / 2, 60);

  // writes difficulty selected
  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextColor(TFT_YELLOW, TFT_RED);
  const char* modeName = words[currentDifficulty];
  gfx.drawString(modeName, 480 / 2, 110);

  gfx.setFont(&FreeSerifBold12pt7b);
  gfx.setTextColor(TFT_WHITE, TFT_RED);
  gfx.drawString("Press button again to start!", 480 / 2, 180);
}

// UI for end game screen
void endGameScreen() {
  gfx.fillScreen(TFT_RED);

  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextColor(TFT_WHITE, TFT_RED);
  gfx.setTextDatum(top_center);

  gfx.drawString("Game Over!", 480 / 2, 60);

  gfx.setFont(&FreeSerifBold12pt7b);
  char buf[32];
  snprintf(buf, sizeof(buf), "Final Score: %d", currScore);     //print the score 
  gfx.drawString(buf, 480 / 2, 140);

  // if we got a new high score, update in non volitile memory 
  if (currScore > high_score) {                     
    prefs.putInt("score", currScore);
    high_score = currScore;

    // UI for new high score
    snprintf(buf, sizeof(buf), "Congrats! You got a new");
    gfx.drawString(buf, 480 / 2, 260);
    snprintf(buf, sizeof(buf), "high score of %d!", currScore);
    gfx.drawString(buf, 480 / 2, 280);
  } else {

    //UI for normal score
    snprintf(buf, sizeof(buf), "High Score: %d", high_score);
    gfx.drawString(buf, 480 / 2, 280);
  }
  
}

// UI for score screen 
void scoreScreen()
{
  gfx.fillScreen(TFT_RED);

  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextColor(TFT_WHITE, TFT_RED);
  gfx.setTextDatum(top_center);

  const char* line1 = "Score:";
  gfx.drawString(line1, 480 / 2, 60);

  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.setTextDatum(middle_center);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", currScore);
  gfx.drawString(buf, 480 / 2, 160);                //update score
}

// UI for countdown
void showCountdownAndStartGame()
{
  gfx.fillScreen(TFT_RED);
  gfx.setTextDatum(middle_center);
  gfx.setTextColor(TFT_WHITE, TFT_RED);

  for (int n = 3; n >= 1; n--) {
    gfx.fillScreen(TFT_RED);
    gfx.setFont(&FreeSerifBold24pt7b);

    char buf[2];
    buf[0] = '0' + n;
    buf[1] = '\0';

    gfx.drawString(buf, 480 / 2, 160);
    delay(1000);  // 1 second per number
  }

  gfx.fillScreen(TFT_RED);
  gfx.setFont(&FreeSerifBold24pt7b);
  gfx.drawString("GO!", 480 / 2, 160);
  delay(500);

  // start game
  startGameForCurrentDifficulty();  
}

// start game logic
void startGameForCurrentDifficulty() {
  currScore = 0;
  scoreLine = "";

  int diffInt = (int)currentDifficulty;       // turn difficulty to int

  // Debug print statement 
  Serial.print("Sending difficulty to other ESP: D:");
  Serial.println(diffInt);
  
  //sending score to other esp
  ScoreSerial.print("D:");
  ScoreSerial.print((int)currentDifficulty);  // 0, 1, or 2
  ScoreSerial.print("\n");
  Serial.println("sent difficulty!");

  // start timer and change state to playing
  gameStartTime = millis();
  gameState = STATE_PLAYING;
  scoreScreen();              //draw the score screen

  // Debug statements to see which difficulty was selected
  switch (currentDifficulty) {
    case DIFF_EASY:
      startEasyGame();
      break;
    case DIFF_MEDIUM:
      startMediumGame();
      break;
    case DIFF_HARD:
      startHardGame();
      break;
  }
}

// debug print statements
void startEasyGame() {
  Serial.println("Starting EASY game...");
}
void startMediumGame() {
  Serial.println("Starting MEDIUM game...");
}
void startHardGame() {
  Serial.println("Starting HARD game...");
}

// function that takes in score updates from another esp32
void handleScoreSerial() {
  while (ScoreSerial.available()) {
    char c = ScoreSerial.read();

    if (c == '\n') {        // We have a recieved a new line
      if (scoreLine.startsWith("S:")) {
        int incomingScore = scoreLine.substring(2).toInt();   // grab the new score substring

        if (incomingScore != currScore) {   
          currScore = incomingScore;

          // debug statements
          Serial.print("Score updated from other ESP: ");
          Serial.println(currScore);

          // Redraw score screen 
          scoreScreen();  
        }
      }
      scoreLine = "";       // reset buffer for next line
    } else if (c != '\r') {
      // ignore single chars
      scoreLine += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  ScoreSerial.begin(115200, SERIAL_8N1, SCORE_RX_PIN, SCORE_TX_PIN);    //start 
  gfx.init();
  gfx.setRotation(3);        // landscape orientation for uLCD screen

  //configure buttons
  pinMode(confirm_btn, INPUT_PULLUP);
  pinMode(select_btn, INPUT_PULLUP);

  prefs.begin("HighScore", false);    
  high_score = prefs.getInt("score", 0);        //populate high score from NVS
  //prefs.clear()                     // if you need to clear NVS 

  // draw home screen initially
  drawHomeScreen(); 
  Serial.println("home screen drawn!");

  gameState = STATE_HOME;       //set initial state
}


void loop() {
  
  if (digitalRead(select_btn) == LOW) {
    if (millis() - lastTapTime > TAP_DEBOUNCE_MS) {   //debounce
      lastTapTime = millis();

      switch (gameState) {
        case STATE_HOME:
          // On button press, cycle through difficulties
          currentDifficulty = (Difficulty)(((int)currentDifficulty + 1) % 3);
          Serial.print("Tap: difficulty -> ");
          Serial.println(words[currentDifficulty]);
          drawDifficultySelection();
          break;
        
        case STATE_CONFIRM:           
          // Press acts as back to home
          Serial.println("Back tap: returning to home screen.");
          gameState = STATE_HOME;
          drawHomeScreen();
          break;

      }
    }
  }

  // read button logic level
  int reading = digitalRead(confirm_btn);

  // if button pressed
  if (lastButtonState == HIGH && reading == LOW) {        // falling edge
    if (millis() - lastButtonTime > BUTTON_DEBOUNCE_MS) {     //debounce
      lastButtonTime = millis();

      switch (gameState) {
        case STATE_HOME:
          // First press: go to confirm screen
          gameState = STATE_CONFIRM;
          Serial.print("Mode selected: ");
          Serial.println(words[currentDifficulty]);
          drawConfirmScreen();
          break;

        case STATE_CONFIRM:
          // button press in confirm state starts game
          Serial.println("Confirm button: starting countdown...");
          showCountdownAndStartGame();
          break;

        case STATE_ENDGAME:
          // button press when we are on end screen sends us back home
          Serial.println("Restarting to home...");
          gameState = STATE_HOME;
          drawHomeScreen();
          break;
      }
    }
  }

  lastButtonState = reading;

  if (gameState == STATE_PLAYING) {
    //taking in the current score from other esp
    handleScoreSerial();

    //timer that transitions to game over after 45 seconds
    unsigned long elapsed = millis() - gameStartTime;
    if (elapsed >= GAME_DURATION_MS) {
      Serial.println("Game timer expired, ending game.");
      gameState = STATE_ENDGAME;
      endGameScreen();
    }
  }
}



