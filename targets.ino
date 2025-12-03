#include "esp32-hal-ledc.h" 
#include <ESP32Servo.h>
#include <HardwareSerial.h>

// ---------------- Pin definitions ----------------
#define PIN_RECV1 5
#define PIN_RECV2 6
#define PIN_RECV3 7

#define SERVO1 20
#define SERVO2 21
#define SERVO3 22

#define led3 18
#define led2 10
#define led1 19

#define SPEAKER 3

// UART pins to talk to display ESP
#define RX 8 // this board's RX pin
#define TX 9 // this board's TX pin

// Third servo globals
const int SERVO3_CHANNEL = 2;      // pick a safe LEDC channel: 0,1 already used by the library
const int SERVO3_FREQ    = 50;     // 50 Hz for servos
const int SERVO3_RES_BITS = 16;    // resolution bits

// ---------------- Speaker melodies ----------------

int welcomeMelody[] = {523, 523, 659, 523, 784, 659, 523, 659, 784, 1047, 784, 659, 523, 659, 523, 392};
int welcomeDurations[] = {150, 150, 150, 150, 200, 200, 150, 150, 150, 150, 150, 150, 200, 150, 150, 400};
int welcomeLength = 16;

int hitSound[] = {400, 800, 1200};
int hitDurations[] = {50, 50, 100};
int hitLength = 3;

int gameOverMelody[] = {494, 466, 440, 392, 349};
int gameOverDurations[] = {300, 300, 300, 300, 600};
int gameOverLength = 5;

// booleans to prevent repeated playing inside game loop 
bool welcomePlayed = false;
bool gameOverPlayed = false;

// Servo Objects
Servo testServo1;
Servo testServo2;
//Servo testServo3;  // doesn't work correctly with the libary, other helpers are written to support 3rd servo

// communicating with uLCD ESP to update score and select difficulty
HardwareSerial ScoreSerial(1);
// buffer to hold received data
String cmdLine = "";

// ir counts incremented by interrupts
volatile int count1 = 0;
volatile int count2 = 0;
volatile int count3 = 0;

// ---------------- Game State Globals ----------------
int score = 0;
int timerSeconds = 45; // length of game (seconds)
unsigned long lastTick = 0;

// target state & timing
bool target1Active = false;
bool target2Active = false;
bool target3Active = false;

// make sure targets stay up for difficulty's specified duration
unsigned long target1ActivatedTime = 0;
unsigned long target2ActivatedTime = 0;
unsigned long target3ActivatedTime = 0;

// offset the checking so targets don't run probabilities at the same time
unsigned long lastTarget1Check = 0;
unsigned long lastTarget2Check = 1000;
unsigned long lastTarget3Check = 2000;

unsigned long lastRead = 0;
unsigned long lastReset = 0;
unsigned long lastPrint = 0;

// Difficulty struct
struct Difficulty {
 uint8_t prob; // random(0, prob) == 1 → target pops
 uint16_t duration; // ms target stays up
 uint16_t freq; // ms between "pop up?" checks
};

// static difficulty presets
Difficulty diffEasy = {2, 5000, 3000}; // 50% chance of activation, long duration, slower
Difficulty diffMed = {2, 3000, 2000}; // 50% chance, medium duration
Difficulty diffHard = {2, 1500, 2000}; // 50% chance, shorter, faster

Difficulty* selectedDifficulty = nullptr;  // struct pointer with associated values
bool difficultyChosen = false;  // boolean to keep game mode in welcome until true

// ---------------- HELPER METHODS ----------------
/* 
ISR Methods for each target's IR sensor; ISRs are triggered by IR receiver obtaining a signal.
Count values determines target state and constant threshold for activation can be adjusted for different lighting.
*/
void IRAM_ATTR recv1ISR() { count1++; }
void IRAM_ATTR recv2ISR() { count2++; }
void IRAM_ATTR recv3ISR() { count3++; }

/*
Helper method; used for reseting the game if played again after reaching game over once.
*/
void resetGameState() {
  // basic scoring & game timer
  score = 0;
  timerSeconds = 45;

  // IR counters reset each game
  count1 = 0;
  count2 = 0;
  count3 = 0;

  // targets & LEDs all down/off initially
  target1Active = false;
  target2Active = false;
  target3Active = false;

  digitalWrite(led1, LOW);
  digitalWrite(led2, LOW);
  digitalWrite(led3, LOW);

  testServo1.write(180);   // DOWN
  testServo2.write(180);
  servo3WriteAngle(180);

  // sounds
  gameOverPlayed = false;  // allow game-over sound to play next time
  welcomePlayed = false;   // welcome plays each new game

  // timing (w/ offset)
  lastTarget1Check = millis();
  lastTarget2Check = millis() + 1000;
  lastTarget3Check = millis() + 2000;
  lastTick = millis();
}

/**
Helper method; score serial communication between target control ESP and uLCD display ESP.
*/
void sendScoreToDisplay(int score) {
 ScoreSerial.print("S:");
 ScoreSerial.print(score);
 ScoreSerial.print("\n");

 Serial.print("→ Sent score to display: ");
 Serial.println(score);
}

/*
Helper method; set difficulty based on received value from ScoreSerial (handleDifficultyDisplay()).

@param level integer corresponding with level; received from ScoreSerial
*/
void setDifficulty(int level) {
 switch (level) {
 case 0: // EASY
 selectedDifficulty = &diffEasy;
 Serial.println("Difficulty set to: EASY");
 break;
 case 1: // MEDIUM
 selectedDifficulty = &diffMed;
 Serial.println("Difficulty set to: MEDIUM");
 break;
 case 2: // HARD
 selectedDifficulty = &diffHard;
 Serial.println("Difficulty set to: HARD");
 break;
 default:
 Serial.println("Invalid difficulty level received.");
 selectedDifficulty = nullptr;
 break;
 }

 if (selectedDifficulty != nullptr) {
    resetGameState();         // <--- new
    difficultyChosen = true;  // we’re now in "playing" mode

    Serial.print("Game timer started. Duration: ");
    Serial.print(timerSeconds);
    Serial.println(" seconds");
 }
}

/** 
Helper method; read "D:x\n" from display ESP to determine difficulty
*/
void handleDifficultyFromDisplay() {
 // Serial.println("in handle difficulty checking...");
 while (ScoreSerial.available()) {
 Serial.println("received something!");
 char c = ScoreSerial.read();

 if (c == '\n') {
 Serial.print("Received line from display ESP: ");
 Serial.println(cmdLine);

 if (cmdLine.startsWith("D:")) {
 int d = cmdLine.substring(2).toInt();
 setDifficulty(d);
 }

 cmdLine = ""; // clear buffer
 } else if (c != '\r') {
 cmdLine += c;
 }
 }
}


/**
Servo 3 helper method; given an angle 0-180 that writes appropriate duty cycle to PWM signal for third servo.

@param angle desired angle for servo 3
*/
void servo3WriteAngle(int angle) {
  angle = constrain(angle, 0, 180);

  // Convert angle to microseconds (tune 500–2400 if needed)
  int us = map(angle, 0, 180, 500, 2400);

  // Convert microseconds (0–20000 per period at 50 Hz) to duty ticks
  uint32_t maxDuty = (1UL << SERVO3_RES_BITS) - 1;  // 2^bits - 1
  float dutyFraction = us / 20000.0f;               // 20 ms period at 50 Hz
  uint32_t duty = (uint32_t)(dutyFraction * maxDuty);

  ledcWrite(SERVO3, duty);
}

/*
Setup function; initializes all pinmodes, serial communication, and peripherals.
*/
void setup() {
 Serial.begin(9600);
 // UART communication begin: RX, TX
 ScoreSerial.begin(115200, SERIAL_8N1, RX, TX);
 delay(2000); // Wait for serial monitor if needed

 // speaker
 pinMode(SPEAKER, OUTPUT);

 // IR receiver pins
 pinMode(PIN_RECV1, INPUT);
 pinMode(PIN_RECV2, INPUT);
 pinMode(PIN_RECV3, INPUT);

 // LEDs
 pinMode(led1, OUTPUT);
 pinMode(led2, OUTPUT);
 pinMode(led3, OUTPUT);

 digitalWrite(led1, LOW);
 digitalWrite(led2, LOW);
 digitalWrite(led3, LOW);

 // Attach interrupts for receivers
 attachInterrupt(digitalPinToInterrupt(PIN_RECV1), recv1ISR, CHANGE);
 attachInterrupt(digitalPinToInterrupt(PIN_RECV2), recv2ISR, CHANGE);
 attachInterrupt(digitalPinToInterrupt(PIN_RECV3), recv3ISR, CHANGE);

 // Servos 1 and 2, utilizes servo library
  testServo1.setPeriodHertz(50);
  testServo2.setPeriodHertz(50);

  int ch1 = testServo1.attach(SERVO1, 500, 2400);
  int ch2 = testServo2.attach(SERVO2, 500, 2400);

  Serial.print("Servo1 channel: "); Serial.println(ch1);
  Serial.print("Servo2 channel: "); Serial.println(ch2);

  // Servo 3 PWM channel
  ledcAttach(SERVO3, SERVO3_FREQ, SERVO3_RES_BITS);

  // init all servo angles
  testServo1.write(180); // DOWN
  testServo2.write(180);
  servo3WriteAngle(180);
  delay(1000);
  
// DEBUG
//  Serial.println("\n========================================");
//  Serial.println("SETUP COMPLETE - Waiting for difficulty (D:0 / D:1 / D:2) from display ESP");
//  Serial.println("========================================\n");
}

/*
Main game loop. Handles all target receiver and gun transmitter game logic and communication from uLCD ESP.
*/
void loop() {
 unsigned long now = millis();
 if (!welcomePlayed) {
 // Serial.println("4. Playing welcome sound...");
 
 for (int i = 0; i < welcomeLength; i++) {
 tone(SPEAKER, welcomeMelody[i]);
 delay(welcomeDurations[i]);
 noTone(SPEAKER);
 delay(50);
 }
 welcomePlayed = true; // prevents replay
 }
 
 // always listen for difficulty commands
 handleDifficultyFromDisplay();

 // if no difficulty chosen yet, just idle
 if (!difficultyChosen || selectedDifficulty == nullptr) {
 return;
 }

 // handle game timer
 if (timerSeconds > 0 && now - lastTick >= 1000) {
 timerSeconds--;
 lastTick = now;
 Serial.print("Time left: ");
 Serial.println(timerSeconds);
 }

 // GAME OVER handling; if timer expired -> stop raising targets, but leave state as-is
 if (timerSeconds <= 0) {
 // ensure all targets are down
 if (target1Active) {
 target1Active = false;
 testServo1.write(180);
 digitalWrite(led1, LOW);
 }
 if (target2Active) {
 target2Active = false;
 testServo2.write(180);
 digitalWrite(led2, LOW);
 }
 if (target3Active) {
 target3Active = false;
 servo3WriteAngle(180);
 digitalWrite(led3, LOW);
 }
 
 if (!gameOverPlayed) {
 // Serial.println("4. Playing GAME OVER sound...");

 for (int i = 0; i < gameOverLength; i++) {
 tone(SPEAKER, gameOverMelody[i]);
 delay(gameOverDurations[i]);
 noTone(SPEAKER);
 delay(50);
 }

 gameOverPlayed = true; // prevents replay
 }
  // reset difficulty selections to unselected
  difficultyChosen = false;
  selectedDifficulty = nullptr;

  return;
 }


 // ACTIVATE TARGETS: randomly raise targets based on difficulty->freq & prob
 // target 1
 if (now - lastTarget1Check >= selectedDifficulty->freq) {
 if (random(0, selectedDifficulty->prob) == 1) {
 target1Active = true;
 target1ActivatedTime = now;
 // Serial.println(">>> Target 1 is UP!");
 digitalWrite(led1, HIGH);  // LED ON
 testServo1.write(90);  // UP
 }
 lastTarget1Check = now;
 }

// target 2
 if (now - lastTarget2Check >= selectedDifficulty->freq) {
 if (random(0, selectedDifficulty->prob) == 1) {
 target2Active = true;
 target2ActivatedTime = now;
 // Serial.println(">>> Target 2 is UP!");
 digitalWrite(led2, HIGH);
 testServo2.write(90);
 }
 lastTarget2Check = now;
 }

// target 3
 if (now - lastTarget3Check >= selectedDifficulty->freq) {
 if (random(0, selectedDifficulty->prob) == 1) {
 target3Active = true;
 target3ActivatedTime = now;
 // Serial.println(">>> Target 3 is UP!");
 digitalWrite(led3, HIGH);
 servo3WriteAngle(90);
 }
 lastTarget3Check = now;
 }

 // timeouts: drop targets after selectedDifficulty->duration if not hit
 if (target1Active && (now - target1ActivatedTime >= selectedDifficulty->duration)) {
 target1Active = false;
 testServo1.write(180);
 // Serial.println(">>> Target 1 TIMEOUT");
 digitalWrite(led1, LOW);
 }
 if (target2Active && (now - target2ActivatedTime >= selectedDifficulty->duration)) {
 target2Active = false;
 testServo2.write(180);
 // Serial.println(">>> Target 2 TIMEOUT");
 digitalWrite(led2, LOW);
 }
 if (target3Active && (now - target3ActivatedTime >= selectedDifficulty->duration)) {
 target3Active = false;
 servo3WriteAngle(180);
 // Serial.println(">>> Target 3 TIMEOUT");
 digitalWrite(led3, LOW);
 }

 // DEBUG: periodic status printing & counter reset
//  if (now - lastPrint >= 1000) {
//  Serial.print("R1:");
//  Serial.print(count1);
//  Serial.print(" | R2:");
//  Serial.print(count2);
//  Serial.print(" | R3:");
//  Serial.print(count3);
//  Serial.print(" | Score:");
//  Serial.print(score);

//  if (count1 > 0 || count2 > 0 || count3 > 0) {
//  Serial.print(" <-- DETECTING IR!");
//  }
//  Serial.println();

//  lastPrint = now;
//  }

// reset IR signal receiving count every 2 seconds to prevent false activations
 if (now - lastReset >= 2000) {
 count1 = 0;
 count2 = 0;
 count3 = 0;
 lastReset = now;
 }

 // CHECK HITS: send score when hit, play hit sound, and lay down target
 // target 1
 if (count1 > 1 && target1Active) {
 // Serial.println("✓✓✓ Target 1 HIT! ✓✓✓");
 // sound
 for (int i = 0; i < hitLength; i++) {
 tone(SPEAKER, hitSound[i]);
 delay(hitDurations[i]);
 noTone(SPEAKER);
 delay(50);
 }
 digitalWrite(led1, LOW);
 target1Active = false;
 testServo1.write(180);
 score += 10;
 sendScoreToDisplay(score);
 count1 = count2 = count3 = 0;
 lastReset = now;
 }

// target 2
 if (count2 > 1 && target2Active) {
 // Serial.println("✓✓✓ Target 2 HIT! ✓✓✓");
 for (int i = 0; i < hitLength; i++) {
 tone(SPEAKER, hitSound[i]);
 delay(hitDurations[i]);
 noTone(SPEAKER);
 delay(50);
 }
 digitalWrite(led2, LOW);
 target2Active = false;
 testServo2.write(180);
 score += 10;
 sendScoreToDisplay(score);
 count1 = count2 = count3 = 0;
 lastReset = now;
 }

 // target 3
 if (count3 > 1 && target3Active) {
 // Serial.println("✓✓✓ Target 3 HIT! ✓✓✓");
 for (int i = 0; i < hitLength; i++) {
 tone(SPEAKER, hitSound[i]);
 delay(hitDurations[i]);
 noTone(SPEAKER);
 delay(50);
 }
 digitalWrite(led3, LOW);
 target3Active = false;
 servo3WriteAngle(180);
 score += 10;
 sendScoreToDisplay(score);
 count1 = count2 = count3 = 0;
 lastReset = now;
 }

 // DEBUG: Wrong target
//  if (count1 > 40 && !target1Active) {
//  // Serial.println("✗ Target 1 not active!");
//  count1 = 0;
//  }
//  if (count2 > 40 && !target2Active) {
//  Serial.println("✗ Target 2 not active!");
//  count2 = 0;
//  }
//  if (count3 > 40 && !target3Active) {
//  Serial.println("✗ Target 3 not active!");
//  count3 = 0;
//  }

 lastRead = now;  // set last read
}