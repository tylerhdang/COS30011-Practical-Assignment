#include <DHT.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define DHTPIN 7
#define DHTTYPE DHT11
#define POTPIN A0  // Potentiometer on analog pin A0
#define BUTTON_PIN 10  // Button on pin 10

// Status LEDs
#define RED_LED 2
#define YELLOW_LED 3
#define GREEN_LED 4
#define BUZZER 5

// Mode indicator LEDs
#define BLUE_LED 9   // Blue LED for OFFSET mode
#define WHITE_LED 8  // White LED for OVERRIDE mode
#define SERVO_PIN 6

DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Random messages
String positiveMessages[] = {"You're good!", "Nice day!", "All OK!", "Stay chill!", "Breathe easy!"};
String negativeMessages[] = {"Too hot!", "Too damp!", "Fix this!", "Not great!", "Yikes!"};
String neutralMessages[] = {"Almost fine", "Watch it", "Close call", "Keep steady", "Borderline"};

// Operating mode
enum Mode {
  NORMAL,      // Normal operation - use actual sensor readings
  OFFSET,      // Offset mode - add potentiometer value to readings
  OVERRIDE     // Override mode - potentiometer directly sets values
};

Mode currentMode = NORMAL;
int lastButtonState = HIGH;  // Last button state (not pressed)
unsigned long lastDebounceTime = 0;  // Last debounce time
unsigned long debounceDelay = 50;    // Debounce delay

// Add debug indicator
bool buttonPressed = false;

// Control flags - used for remote control
bool autoMode = true;  // When true, system operates normally; when false, remote commands control outputs
bool redLedOverride = false;
bool yellowLedOverride = false;
bool greenLedOverride = false;
bool buzzerOverride = false;
int servoOverridePos = -1;  // -1 means no override

// Configurable threshold values for temperature and humidity
// GOOD: temp between goodTempMin and goodTempMax, humidity between goodHumMin and goodHumMax
// ACCEPTABLE: temp between acceptableTempMin and acceptableTempMax, humidity between acceptableHumMin and acceptableHumMax
// BAD: anything else
float goodTempMin = 22.0;
float goodTempMax = 27.0;
float goodHumMin = 40.0;
float goodHumMax = 60.0;
float acceptableTempMin = 20.0;
float acceptableTempMax = 30.0;
float acceptableHumMin = 30.0;
float acceptableHumMax = 70.0;

void setup() {
  Serial.begin(9600);

  // Initialize DHT sensor
  dht.begin();
  
  // Initialize the display with the correct I2C address (0x3C is common)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.display();

  // Initialize LEDs and buzzer
  pinMode(RED_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  // Initialize mode indicator LEDs
  pinMode(BLUE_LED, OUTPUT);
  pinMode(WHITE_LED, OUTPUT);

  // Initialize the servo
  myServo.attach(SERVO_PIN);

  // Initialize button pin as input with internal pull-up
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Show startup message
  display.setCursor(0, 0);
  display.println("EnviroGuardian");
  display.println("Mode: NORMAL");
  display.println("Remote control: READY");
  display.display();
  delay(2000);
  
  randomSeed(analogRead(0));  // Seed randomness
}

void loop() {
  // Check for incoming commands first
  checkSerialCommands();
  
  // Read the button state with improved debounce
  checkButtonAndUpdateMode();

  // Read potentiometer value and convert to percentage
  int potValue = analogRead(POTPIN);  // 0-1023
  int potPercent = map(potValue, 0, 1023, 0, 100);

  // Read actual sensor values
  float rawTemp = dht.readTemperature();
  float rawHum = dht.readHumidity();

  // Initialize final values that will be used
  float temp = rawTemp;
  float hum = rawHum;

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Apply potentiometer adjustment based on mode
  switch(currentMode) {
    case NORMAL:
      // No adjustment to readings
      break;
      
    case OFFSET:
      // Convert pot to +/- 10 offset for temperature and +/- 20 for humidity
      float tempOffset, humOffset;
      tempOffset = map(potValue, 0, 1023, -100, 100) / 10.0;
      humOffset = map(potValue, 0, 1023, -200, 200) / 10.0;
      
      temp = rawTemp + tempOffset;
      hum = rawHum + humOffset;
      break;
      
    case OVERRIDE:
      // Potentiometer directly sets values
      temp = map(potValue, 0, 1023, 10, 40);  // 10-40°C range
      hum = map(potValue, 0, 1023, 20, 90);   // 20-90% humidity range
      break;
  }

  // Send data over serial in the format expected by EGDBCollect.py
  Serial.print("Mode: ");
  switch(currentMode) {
    case NORMAL:
      Serial.print("NORMAL");
      break;
    case OFFSET:
      Serial.print("OFFSET");
      break;
    case OVERRIDE:
      Serial.print("OVERRIDE");
      break;
  }
  Serial.print(" | Pot: ");
  Serial.print(potPercent);
  Serial.print("% | RawTemp: ");
  Serial.print(rawTemp);
  Serial.print("°C | FinalTemp: ");
  Serial.print(temp);
  Serial.print("°C | RawHum: ");
  Serial.print(rawHum);
  Serial.print("% | FinalHum: ");
  Serial.print(hum);
  Serial.print("% | ");
  
  // Update mode indicator LEDs based on current mode
  updateModeLEDs();
  
  // Display the readings and mode
  display.clearDisplay();
  display.setCursor(0, 0);

  display.print("T:");
  display.print(temp, 1);
  display.print("C  H:");
  display.print(hum, 1);
  display.println("%");
  
  display.print("Pot:");
  display.print(potPercent);
  display.print("% ");
  
  display.print("Mode: ");
  switch(currentMode) {
    case NORMAL:
      display.println("NORMAL");
      break;
    case OFFSET:
      display.println("OFFSET (BLUE)");
      break;
    case OVERRIDE:
      display.println("OVERRIDE (WHITE)");
      break;
  }

  display.print("Control: ");
  display.println(autoMode ? "AUTO" : "REMOTE");

  String statusMessage;
  String feedback;

  // Check temperature and humidity conditions for status
  if (autoMode) {
    // Automatic mode - normal operation
    if (temp >= goodTempMin && temp <= goodTempMax && hum >= goodHumMin && hum <= goodHumMax) {
      // GOOD
      digitalWrite(RED_LED, LOW);
      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(GREEN_LED, HIGH);
      tone(BUZZER, 1000, 300);
      myServo.write(0);  // Yes position
      feedback = positiveMessages[random(5)];
      statusMessage = "YES";
      Serial.println(feedback);
    } else if ((temp >= acceptableTempMin && temp <= acceptableTempMax) && (hum >= acceptableHumMin && hum <= acceptableHumMax)) {
      // ACCEPTABLE
      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, HIGH);
      tone(BUZZER, 500, 300);
      myServo.write(90); // Neutral position
      feedback = neutralMessages[random(5)];
      statusMessage = "ALMOST";
      Serial.println(feedback);
    } else {
      // BAD
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(RED_LED, HIGH);
      tone(BUZZER, 200, 300);
      myServo.write(180); // No position
      feedback = negativeMessages[random(5)];
      statusMessage = "NO";
      Serial.println(feedback);
    }
  } else {
    // Remote control mode - controlled by Pi
    digitalWrite(RED_LED, redLedOverride);
    digitalWrite(YELLOW_LED, yellowLedOverride);
    digitalWrite(GREEN_LED, greenLedOverride);
    
    if (buzzerOverride) {
      tone(BUZZER, 800, 300);
      buzzerOverride = false;  // Reset after one beep
    }
    
    if (servoOverridePos >= 0) {
      myServo.write(servoOverridePos);
    }
    
    statusMessage = "REMOTE CONTROL";
    feedback = "Controlled by Pi";
    Serial.println("Remote controlled");
  }

  display.println(statusMessage);
  display.println(feedback);

  display.display();
  
  // Shorter delay and check button and serial during wait period
  for (int i = 0; i < 20; i++) {
    checkButtonAndUpdateMode(); // Check for button presses during delay
    checkSerialCommands();      // Check for commands during delay
    delay(50);  // 20 × 50ms = 1 second total
  }
}

// Check for incoming serial commands
void checkSerialCommands() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    switch(cmd) {
      case 'A': // Toggle auto/remote mode
        autoMode = true;
        break;
      case 'R': // Toggle to remote mode
        autoMode = false;
        break;
        
      // LED controls
      case '1': // Red LED on
        redLedOverride = true;
        autoMode = false;
        break;
      case '2': // Red LED off
        redLedOverride = false;
        autoMode = false;
        break;
      case '3': // Yellow LED on
        yellowLedOverride = true;
        autoMode = false;
        break;
      case '4': // Yellow LED off
        yellowLedOverride = false;
        autoMode = false;
        break;
      case '5': // Green LED on
        greenLedOverride = true;
        autoMode = false;
        break;
      case '6': // Green LED off
        greenLedOverride = false;
        autoMode = false;
        break;
        
      // Buzzer control
      case 'B': // Trigger buzzer
        buzzerOverride = true;
        autoMode = false;
        break;
        
      // Servo controls
      case 'S': // Set servo position
        if (Serial.available() >= 3) {
          // Read three digits for servo position (000-180)
          char buf[4] = {0};
          Serial.readBytes(buf, 3);
          servoOverridePos = atoi(buf);
          
          // Constrain to valid servo range
          servoOverridePos = constrain(servoOverridePos, 0, 180);
          autoMode = false;
        }
        break;
        
      // Mode change commands
      case 'M': // Change mode
        if (Serial.available() > 0) {
          char modeCmd = Serial.read();
          switch(modeCmd) {
            case '1': // Normal mode
              currentMode = NORMAL;
              break;
            case '2': // Offset mode
              currentMode = OFFSET;
              break;
            case '3': // Override mode
              currentMode = OVERRIDE;
              break;
          }
          // Update the mode LEDs immediately when mode changes
          updateModeLEDs();
        }
        break;
        
      // Threshold update commands
      case 'T': // Update temperature thresholds
        if (Serial.available() > 0) {
          char thresholdType = Serial.read();
          float value = 0.0;
          
          // Read the threshold value (format: float as string)
          if (Serial.available() > 0) {
            String valueStr = Serial.readStringUntil('\n');
            value = valueStr.toFloat();
            
            switch(thresholdType) {
              case '1': // Good min temperature
                goodTempMin = value;
                break;
              case '2': // Good max temperature
                goodTempMax = value;
                break;
              case '3': // Acceptable min temperature
                acceptableTempMin = value;
                break;
              case '4': // Acceptable max temperature
                acceptableTempMax = value;
                break;
            }
          }
        }
        break;
        
      case 'H': // Update humidity thresholds
        if (Serial.available() > 0) {
          char thresholdType = Serial.read();
          float value = 0.0;
          
          // Read the threshold value (format: float as string)
          if (Serial.available() > 0) {
            String valueStr = Serial.readStringUntil('\n');
            value = valueStr.toFloat();
            
            switch(thresholdType) {
              case '1': // Good min humidity
                goodHumMin = value;
                break;
              case '2': // Good max humidity
                goodHumMax = value;
                break;
              case '3': // Acceptable min humidity
                acceptableHumMin = value;
                break;
              case '4': // Acceptable max humidity
                acceptableHumMax = value;
                break;
            }
          }
        }
        break;
        
      case 'G': // Get current thresholds
        // Send all threshold values in a format that can be parsed by the Pi
        Serial.print("THRESHOLDS|");
        Serial.print(goodTempMin); Serial.print("|");
        Serial.print(goodTempMax); Serial.print("|");
        Serial.print(goodHumMin); Serial.print("|");
        Serial.print(goodHumMax); Serial.print("|");
        Serial.print(acceptableTempMin); Serial.print("|");
        Serial.print(acceptableTempMax); Serial.print("|");
        Serial.print(acceptableHumMin); Serial.print("|");
        Serial.println(acceptableHumMax);
        break;
    }
  }
}

// Function to update the mode indicator LEDs based on current mode
void updateModeLEDs() {
  // Turn off all mode LEDs first
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(WHITE_LED, LOW);
  
  // Turn on the appropriate LED based on current mode
  switch(currentMode) {
    case NORMAL:
      // No LEDs on in NORMAL mode
      break;
    case OFFSET:
      digitalWrite(BLUE_LED, HIGH);  // Blue LED for OFFSET mode
      break;
    case OVERRIDE:
      digitalWrite(WHITE_LED, HIGH); // White LED for OVERRIDE mode
      break;
  }
}

// Function to improve button handling with more reliable mode changing
void checkButtonAndUpdateMode() {
  static bool modeChangeFlag = false;  // Flag to track if we're in the process of changing mode
  
  // Read the current button state
  int currentButtonState = digitalRead(BUTTON_PIN);
  
  // Check if the button state has changed
  if (currentButtonState != lastButtonState) {
    // Reset the debounce timer
    lastDebounceTime = millis();
  }
  
  // If enough time has passed, consider the button state stable
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Button is pressed (LOW)
    if (currentButtonState == LOW) {
      buttonPressed = true;
      
      // Only change mode once per button press using the flag
      if (!modeChangeFlag) {
        modeChangeFlag = true;  // Set flag to prevent multiple mode changes while held
        
        // Cycle through modes
        switch (currentMode) {
          case NORMAL:
            currentMode = OFFSET;
            break;
          case OFFSET:
            currentMode = OVERRIDE;
            break;
          case OVERRIDE:
            currentMode = NORMAL;
            break;
        }
        
        // Update the mode LEDs immediately when mode changes
        updateModeLEDs();
        
        // Display mode change with clear visual feedback
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("MODE CHANGED!");
        display.println();
        
        display.print("New mode: ");
        switch (currentMode) {
          case NORMAL:
            display.println("NORMAL");
            display.println("No LED indicators");
            break;
          case OFFSET:
            display.println("OFFSET");
            display.println("BLUE LED on");
            break;
          case OVERRIDE:
            display.println("OVERRIDE");
            display.println("WHITE LED on");
            break;
        }
        
        // Visual indicator that button event was recognized
        display.println();
        display.println("Button press detected!");
        display.display();
        
        // Beep to confirm mode change (different tones for each mode)
        switch (currentMode) {
          case NORMAL:
            tone(BUZZER, 800, 200);
            break;
          case OFFSET:
            tone(BUZZER, 1000, 200);
            break;
          case OVERRIDE:
            tone(BUZZER, 1200, 200);
            break;
        }
        
        delay(1000);  // Show mode change message and prevent bouncing
      }
    } 
    else {
      // Button is released
      buttonPressed = false;
      modeChangeFlag = false;  // Reset the flag when button is released
    }
  }
  
  // Save the current button state for next comparison
  lastButtonState = currentButtonState;
}