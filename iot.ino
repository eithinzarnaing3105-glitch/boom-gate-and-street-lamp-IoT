/***** ESP32 Toll Gate + Street Light (Real Hardware + Blynk) *****/
#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Toll Gate (ESP32)"
#define BLYNK_AUTH_TOKEN    "YOUR_DEVICE_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>

/* ---- Wi-Fi ---- */
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

/* ---- Pins ---- */
const int PIN_LDR_AO     = 34;   // LDR module AO -> GPIO34 (ADC)
const int PIN_STREET_LED = 23;   // 4x street LEDs (each 330Ω) all driven by GPIO23
const int PIN_LED_RED    = 19;   // traffic red
const int PIN_LED_GREEN  = 18;   // traffic green
const int PIN_TRIG       = 5;    // HC-SR04 trig
const int PIN_ECHO       = 17;   // HC-SR04 echo (level shift to 3.3V if sensor at 5V)
const int PIN_SERVO      = 13;   // servo signal

/* ---- Tunables ---- */
int   LDR_DARK_THRESHOLD = 2000;       // ESP32 ADC 0..4095. Adjust after calibration.
const int LDR_HYST       = 100;        // hysteresis to avoid flicker near threshold
const int DIST_THRESHOLD_CM  = 15;     // <15cm => car present
const unsigned long OPEN_HOLD_MS = 5000;

/* ---- Blynk Virtual Pins ---- */
#define VPIN_ALLOW   V0   // Button (Push)
#define VPIN_STATUS  V1   // Label (String)
// Event in Blynk Cloud: car_detected (push notification enabled)

/* ---- State ---- */
Servo gateServo;
bool streetOn = false;
bool waitingForApproval = false;
bool gateOpen = false;
unsigned long gateOpenStart = 0;

/* ---- Helpers ---- */
void setStatus(const char* s) { Blynk.virtualWrite(VPIN_STATUS, s); }
void traffic(bool go) {
  digitalWrite(PIN_LED_GREEN, go ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   go ? LOW  : HIGH);
}
void goOpen()  { gateServo.write(90);  traffic(true);  gateOpen = true;  gateOpenStart = millis(); setStatus("Open");  Serial.println("Gate OPENED after approval."); }
void goClose() { gateServo.write(0);   traffic(false); gateOpen = false; setStatus("Closed");       Serial.println("Gate CLOSED."); }

long measureDistanceCM() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long us = pulseIn(PIN_ECHO, HIGH, 30000UL); // timeout 30ms
  if (us == 0) return -1;
  return us / 58; // us->cm
}

/* ---- Blynk: Allow button (Push mode) ---- */
BLYNK_WRITE(VPIN_ALLOW) {
  int val = param.asInt(); // 1 when pressed
  if (val == 1 && waitingForApproval && !gateOpen) {
    goOpen();
    waitingForApproval = false;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_STREET_LED, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  gateServo.attach(PIN_SERVO, 500, 2400); // typical servo pulse range
  goClose();                              // start closed
  digitalWrite(PIN_STREET_LED, LOW);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  setStatus("Booting...");
  Serial.println("ESP32 + Blynk ready.");
}

void loop() {
  Blynk.run();

  // --- STREET LIGHTS (LDR with hysteresis) ---
  int ldr = analogRead(PIN_LDR_AO);   // 0..4095
  // Debug to calibrate:
  // Serial.print("LDR: "); Serial.println(ldr);

  if (!streetOn && ldr > (LDR_DARK_THRESHOLD + LDR_HYST)) {
    streetOn = true;
    digitalWrite(PIN_STREET_LED, HIGH);
    // Serial.println("Street lights ON");
  } else if (streetOn && ldr < (LDR_DARK_THRESHOLD - LDR_HYST)) {
    streetOn = false;
    digitalWrite(PIN_STREET_LED, LOW);
    // Serial.println("Street lights OFF");
  }

  // --- CAR DETECTION ---
  long dist = measureDistanceCM();

  // New detection → notify and wait for approval
  if (dist > 0 && dist < DIST_THRESHOLD_CM && !waitingForApproval && !gateOpen) {
    waitingForApproval = true;
    setStatus("Car detected. Waiting approval...");
    Serial.println("Car detected! Waiting for approval...");
    // Push notification (configure 'car_detected' event in template)
    Blynk.logEvent("car_detected", "Car waiting at Toll Gate. Tap Allow to open.");
  }

  // If waiting but car leaves, cancel
  if (waitingForApproval && (dist <= 0 || dist >= DIST_THRESHOLD_CM)) {
    waitingForApproval = false;
    setStatus("Car left. Request cancelled.");
    Serial.println("Car left / undetected. Request cancelled.");
  }

  // Auto-close after hold time
  if (gateOpen && millis() - gateOpenStart >= OPEN_HOLD_MS) {
    goClose();
  }

  delay(20);
}
