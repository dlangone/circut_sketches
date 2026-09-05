// ==========================================
// PIN CONFIGURATION
// ==========================================
const int pinX = 34;   // VRX (X-axis) Analog Pin
const int pinY = 32;   // VRY (Y-axis) Analog Pin
const int pinSW = 13;  // SW (Switch/Button) Digital Pin
const int pinLED = 2;  // LED Pin (GPIO 2 is the built-in LED on most ESP32s)

// ==========================================
// CALIBRATION & ADC CONSTANTS
// ==========================================
const int ADC_CENTER = 2048;  // Ideal 12-bit mid-point for ESP32 ADC
const int DEADZONE = 150;     // Hardware drift buffer around center point

void setup() {
  // Initialize serial communication for the Serial Monitor/Plotter
  Serial.begin(115200);
  
  // Configure digital input/output pins
  pinMode(pinSW, INPUT_PULLUP); // Uses internal pull-up resistor (1 = released, 0 = pressed)
  pinMode(pinLED, OUTPUT);       // Configures the LED pin to output power
  
  // Configures the ESP32 ADC to read full 0 to 3.3V range
  analogSetAttenuation(ADC_11db); 
}

void loop() {
  // 1. Read the raw analog and digital inputs
  int rawX = analogRead(pinX);
  int rawY = analogRead(pinY);
  int btnState = digitalRead(pinSW); 

  // 2. Apply Deadzone filtering to stabilize center drift
  if (abs(rawX - ADC_CENTER) < DEADZONE) rawX = ADC_CENTER;
  if (abs(rawY - ADC_CENTER) < DEADZONE) rawY = ADC_CENTER;

  // 3. Scale raw values (0-4095) down to a clean, usable control range (-100 to 100)
  int mapX = map(rawX, 0, 4095, -100, 100);
  int mapY = map(rawY, 0, 4095, -100, 100);

  // 4. LED Motion Logic
  // Turn LED ON if joystick is pushed in any direction away from the deadzone
  if (mapX != 0 || mapY != 0) {
    digitalWrite(pinLED, HIGH); 
  } else {
    digitalWrite(pinLED, LOW);  // Turn LED OFF when joystick sits at rest
  }

  // 5. Output data formatted for both Serial Monitor and Serial Plotter
  Serial.print("X_Axis:"); Serial.print(mapX);
  Serial.print(",");
  Serial.print("Y_Axis:"); Serial.print(mapY);
  Serial.print(",");
  Serial.print("Button:"); Serial.println(btnState == LOW ? 100 : 0); // Spikes graph line to 100 when pressed
  
  // Small pause to balance system responsiveness and CPU cycles
  delay(50); 
}
