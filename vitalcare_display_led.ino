/*
    MAX30102 Vitals Monitor - AI PERCEPTRON + OLED HMI EDITION + AMBIENT AURA + DUAL CORE
    --------------------------------------------------
    - DUAL CORE: Firebase uploads offloaded to Core 0 to prevent sensor/LED freezing.
    - PERCEPTRON: Auto-adjusts LED brightness based on sensor temp.
    - SATURATION FIX: Monitors for 262143 (max) and warns user.
    - CORE LOGIC: Refractory Period BPM + Quadratic SpO2.
    - HMI DISPLAY: State-based OLED feedback for signal validation.
    - AMBIENT AURA: ESP32-S3 RGB LED pulses smoothly.
*/

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h> 

// -----------------------
// AMBIENT AURA CONFIG (NeoPixel)
// -----------------------
#define NEOPIXEL_PIN 48 
#define NUMPIXELS 1
Adafruit_NeoPixel aura(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

bool triggerBeat = false; 
float currentAuraBrightness = 0; 

// -----------------------
// OLED DISPLAY CONFIG (I2C_2)
// -----------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA 8
#define OLED_SCL 9

TwoWire I2C_OLED = TwoWire(1); 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, OLED_RESET);

enum DisplayState { STATE_NO_FINGER, STATE_READING, STATE_UNSTABLE, STATE_STABLE_OUTPUT, STATE_ERROR };
DisplayState currentState = STATE_NO_FINGER;

unsigned long lastDisplayUpdate = 0;
const int DISPLAY_REFRESH_MS = 150; 
int ui_BPM = 0;
float ui_SpO2 = 0.0;
int ui_Fatigue = 0;
String ui_Status = "Normal";

int invalidCount = 0;
int lowSignalCount = 0;
bool hasStableData = false;
const int SAMPLES_FOR_UI_STABILITY = 10; 

// -----------------------
// WIFI & FIREBASE CONFIG
// -----------------------
#define WIFI_SSID "Galaxy A14 5G 1EE5"
#define WIFI_PASSWORD "vedant1820"
#define FIREBASE_HOST "new-project-ad7f0-default-rtdb.firebaseio.com"
const String FIREBASE_PATH = "/vitals/current_reading";

// --- DUAL CORE ASYNC VARIABLES ---
volatile bool sendDataFlag = false;
String asyncJsonPayload = "";

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 5 * 3600 + 30 * 60; 
const int DAYLIGHT_OFFSET_SEC = 0;

// -----------------------
// HARDWARE & SENSOR CONFIG (I2C_1)
// -----------------------
MAX30105 particleSensor;
const int BLUE_LED_PIN = 2;

// -----------------------
// AI PERCEPTRON CONFIG
// -----------------------
const float TEMP_THRESHOLD = 30.0; 
float currentBrightness = 50.0;    
float sharedTempC = 0.0; 
unsigned long tsLastTempCheck = 0; 
const unsigned long TEMP_CHECK_INTERVAL = 15000; // CHANGED: Now checks every 15 sec so it doesn't disturb sensor

// -----------------------
// BPM VARIABLES
// -----------------------
const uint8_t BPM_WINDOW = 6;
float bpmHistory[BPM_WINDOW];
uint8_t bpmIndex = 0;
int lastBPM = 0;

static bool wasAbove = false;
static unsigned long lastPeakTime = 0;
const long AC_PEAK_THRESHOLD = 100; 
const long MIN_PEAK_INTERVAL_MS = 300; 

static float dcEstimateIR = 0.0;
static float dcEstimateRED = 0.0;
const float DC_ALPHA = 0.96;

long instantACSignalIR = 0;
long instantACSignalRED = 0;

// -----------------------
// SPO2 & NEURAL VARIABLES
// -----------------------
float avgRatio = 0.0;
const float RATIO_ALPHA = 0.15; 
const uint8_t HRV_WINDOW = 10;
long ibiHistory[HRV_WINDOW];
uint8_t ibiIndex = 0;
int lastNeuralFatigue = 0;
long lastIBI_ms = 0;

long fatigueSum = 0;
int fatigueCount = 0;
int finalSentFatigue = 0;

const int MAX_SAMPLES = 50; 
int bpmBuffer[MAX_SAMPLES];
float spo2Buffer[MAX_SAMPLES];
int sampleIndex = 0;

const float W_HRV = -1.5; const float W_SPO2 = -0.2; const float W_BPM = 1.2; const float BIAS = 0.5;

#define THRESHOLD_MILD_HYPOXIA     94.0
#define THRESHOLD_MODERATE_HYPOXIA 90.0
#define THRESHOLD_SEVERE_HYPOXIA   85.0

unsigned long tsLastReport = 0;
unsigned long tsLastFirebaseUpdate = 0;
const unsigned long dataSendInterval = 5000;

// -----------------------
// HELPER FUNCTIONS 
// -----------------------
String getLocalTimeStr() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "Time Not Synced";
    char timeStr[30];
    strftime(timeStr, 30, "%H:%M:%S", &timeinfo);
    return String(timeStr);
}

void adjustBrightnessPerceptron() {
    if (millis() - tsLastTempCheck < TEMP_CHECK_INTERVAL) return;
    tsLastTempCheck = millis();

    float tempC = particleSensor.readTemperature();
    sharedTempC = tempC;
    float input_deviation = tempC - TEMP_THRESHOLD; 
    float weight = -2.0; 
    float bias = 50.0; 
    float newBrightness = (input_deviation * weight) + bias;

    if (newBrightness < 20.0) newBrightness = 20.0;
    if (newBrightness > 85.0) newBrightness = 85.0;

    if (abs(newBrightness - currentBrightness) > 2.0) {
        currentBrightness = newBrightness;
        particleSensor.setPulseAmplitudeRed((byte)currentBrightness);
        particleSensor.setPulseAmplitudeIR((byte)currentBrightness);
    }
}

float calculateDynamicSpO2(long red, long ir) {
    if (ir < 50000) { avgRatio = 0; return 0.0; }
    float currentRatio = (float)red / max((long)1, ir);
    if (avgRatio == 0) avgRatio = currentRatio;
    avgRatio = (RATIO_ALPHA * currentRatio) + ((1.0 - RATIO_ALPHA) * avgRatio);

    float A = 110.0, B = 14.0, C = 8.0; 
    float spo2 = A - (B * avgRatio) - (C * avgRatio * avgRatio);
    
    if (sharedTempC < 30.0) {
        float tempDiff = 30.0 - sharedTempC;
        spo2 += (tempDiff * 0.5); 
    }
    if (spo2 > 100.0) spo2 = 100.0;
    if (spo2 < 70.0) spo2 = 70.0;
    return spo2;
}

String getHypoxiaStatus(float spo2) {
    if (spo2 <= 0.0) return "No Finger";
    if (spo2 < THRESHOLD_SEVERE_HYPOXIA) return "Severe Hypox"; 
    else if (spo2 < THRESHOLD_MODERATE_HYPOXIA) return "Mod Hypoxia";
    else if (spo2 < THRESHOLD_MILD_HYPOXIA) return "Mild Hypoxia";
    else return "Normal";
}

float sigmoid(float x) { return 1.0 / (1.0 + exp(-x)); }

int calculateNeuralFatigue(long newIBI, float currentSpO2, int currentBPM) {
    if (newIBI > 0) {
        ibiHistory[ibiIndex++] = newIBI;
        if (ibiIndex >= HRV_WINDOW) ibiIndex = 0;
    }
    if (HRV_WINDOW < 2) return lastNeuralFatigue;

    float sum = 0.0;
    for (int i = 0; i < HRV_WINDOW; i++) if(ibiHistory[i] > 0) sum += ibiHistory[i];
    float meanIBI = sum / HRV_WINDOW;
    
    float varianceSum = 0.0;
    for (int i = 0; i < HRV_WINDOW; i++) if(ibiHistory[i] > 0) varianceSum += pow(ibiHistory[i] - meanIBI, 2);
    float sdnn = sqrt(varianceSum / (HRV_WINDOW - 1));

    float norm_hrv = constrain(sdnn / 50.0, 0.0, 2.0);
    float norm_spo2 = constrain(currentSpO2 / 98.0, 0.0, 1.0);
    float norm_bpm = constrain(currentBPM / 140.0, 0.0, 1.0);

    float weightedSum = (norm_hrv * W_HRV) + (norm_spo2 * W_SPO2) + (norm_bpm * W_BPM) + BIAS;
    return (int)(sigmoid(weightedSum) * 100);
}

int calculateBPM(long irSample, long redSample) {
    if (irSample < 50000) {
        lastBPM = 0; dcEstimateIR = 0.0; dcEstimateRED = 0.0;
        lastNeuralFatigue = 0; finalSentFatigue = 0; fatigueSum = 0;
        fatigueCount = 0; sampleIndex = 0; instantACSignalIR = 0; instantACSignalRED = 0;
        digitalWrite(BLUE_LED_PIN, LOW);
        return 0;
    }
    
    digitalWrite(BLUE_LED_PIN, HIGH);
    
    if (dcEstimateIR == 0.0) dcEstimateIR = irSample;
    dcEstimateIR = (dcEstimateIR * DC_ALPHA) + (irSample * (1.0 - DC_ALPHA));
    instantACSignalIR = irSample - (long)dcEstimateIR;
    
    if (dcEstimateRED == 0.0) dcEstimateRED = redSample;
    dcEstimateRED = (dcEstimateRED * DC_ALPHA) + (redSample * (1.0 - DC_ALPHA));
    instantACSignalRED = redSample - (long)dcEstimateRED;

    bool isAbove = (instantACSignalIR > AC_PEAK_THRESHOLD);
    if (!wasAbove && isAbove) { 
        triggerBeat = true; 
        
        unsigned long now = micros();
        if (lastPeakTime > 0) {
            unsigned long dt_us = now - lastPeakTime;
            uint32_t dt_ms = dt_us / 1000;
            if (dt_ms > MIN_PEAK_INTERVAL_MS) { 
                if (dt_ms < 2500) {
                    lastIBI_ms = dt_ms;
                    float bpm = 60000.0 / dt_ms;
                    bpmHistory[bpmIndex++] = bpm;
                    if (bpmIndex >= BPM_WINDOW) bpmIndex = 0;

                    float sum = 0; uint8_t c = 0;
                    for (int i = 0; i < BPM_WINDOW; i++) { if (bpmHistory[i] > 0) { sum += bpmHistory[i]; c++; } }
                    if (c > 0) lastBPM = round(sum / c); else lastBPM = round(bpm);
                }
                lastPeakTime = now; 
            }
        } else { lastPeakTime = now; }
    }
    wasAbove = isAbove;
    return lastBPM;
}

float getMedian(float *array, int size) {
    for (int i = 0; i < size - 1; i++) for (int j = 0; j < size - i - 1; j++) if (array[j] > array[j + 1]) { float t = array[j]; array[j] = array[j + 1]; array[j + 1] = t; }
    return array[size / 2]; 
}

int getMedianInt(int *array, int size) {
    for (int i = 0; i < size - 1; i++) for (int j = 0; j < size - i - 1; j++) if (array[j] > array[j + 1]) { int t = array[j]; array[j] = array[j + 1]; array[j + 1] = t; }
    return array[size / 2];
}

// -----------------------
// AURA UPDATE FUNCTION
// -----------------------
void updateAura() {
    long t = millis();
    
    switch(currentState) {
        case STATE_NO_FINGER:
            currentAuraBrightness = (sin(t / 500.0) + 1.0) * 20.0 + 5.0; 
            aura.setPixelColor(0, aura.Color(0, (int)currentAuraBrightness, (int)currentAuraBrightness + 20));
            aura.show();
            break;

        case STATE_UNSTABLE:
        case STATE_READING:
            currentAuraBrightness = (sin(t / 200.0) + 1.0) * 30.0; 
            aura.setPixelColor(0, aura.Color((int)currentAuraBrightness, (int)(currentAuraBrightness * 0.8), 0));
            aura.show();
            break;

        case STATE_STABLE_OUTPUT:
            currentAuraBrightness = (sin(t / 400.0) + 1.0) * 40.0 + 10.0; 
            if (triggerBeat) triggerBeat = false; 

            if (ui_SpO2 >= 94.0) {
                aura.setPixelColor(0, aura.Color(0, (int)currentAuraBrightness, 0)); 
            } else if (ui_SpO2 >= 90.0) {
                aura.setPixelColor(0, aura.Color((int)currentAuraBrightness, (int)(currentAuraBrightness * 0.5), 0)); 
            } else {
                aura.setPixelColor(0, aura.Color((int)currentAuraBrightness, 0, 0)); 
            }
            aura.show();
            break;

        case STATE_ERROR:
            if ((t / 100) % 2 == 0) aura.setPixelColor(0, aura.Color(150, 0, 0)); 
            else aura.setPixelColor(0, aura.Color(100, 100, 100)); 
            aura.show();
            break;
    }
}

// -----------------------
// OLED UPDATE FUNCTION
// -----------------------
void updateOLED() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    switch(currentState) {
        case STATE_NO_FINGER:
            display.setTextSize(2);
            display.setCursor(34, 15);
            display.print("Place");
            display.setCursor(28, 35);
            display.print("Finger");
            break;

        case STATE_ERROR:
            display.setTextSize(1);
            display.setCursor(20, 20); display.print("SENSOR ERROR");
            display.setCursor(18, 35); display.print("Adjust or Clean");
            break;

        case STATE_UNSTABLE:
            display.setTextSize(1);
            display.setCursor(26, 20); display.print("Adjust Finger");
            display.setCursor(32, 35); display.print("Low Signal");
            break;

        case STATE_READING:{
            display.setTextSize(2);
            display.setCursor(4, 10);
            display.print("Reading");
            
            if ((millis() / 500) % 2 == 0) display.fillCircle(64, 38, 4, SSD1306_WHITE);
            else display.fillCircle(64, 38, 2, SSD1306_WHITE);

            display.drawRect(14, 50, 100, 8, SSD1306_WHITE);
            int progressWidth = (sampleIndex * 100) / SAMPLES_FOR_UI_STABILITY;
            if(progressWidth > 100) progressWidth = 100;
            display.fillRect(14, 50, progressWidth, 8, SSD1306_WHITE);
            break;
        }

        case STATE_STABLE_OUTPUT:
            display.setTextSize(2);
            display.setCursor(0, 0); display.print("HR:"); display.print(ui_BPM);
            display.setCursor(0, 20); display.print("O2:"); display.print(ui_SpO2, 0); display.print("%");
            display.setTextSize(1);
            display.setCursor(0, 42); display.print("Fatigue: "); display.print(ui_Fatigue); display.print("%");
            display.setCursor(0, 54); display.print("Stat: "); display.print(ui_Status);
            break;
    }
    display.display();
}

// -----------------------
// BACKGROUND TASK: FIREBASE (CORE 0)
// -----------------------
void firebaseUploadTask(void *pvParameters) {
    while (true) {
        if (sendDataFlag && WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            String url = "https://" + String(FIREBASE_HOST) + FIREBASE_PATH + ".json";
            http.begin(url);
            http.addHeader("Content-Type", "application/json");
            http.PUT(asyncJsonPayload); 
            http.end();
            
            sendDataFlag = false; // Reset flag after sending
        }
        vTaskDelay(50 / portTICK_PERIOD_MS); // Yield to watchdog so Core 0 doesn't crash
    }
}

// -----------------------
// SETUP
// -----------------------
void setup() {
    Serial.begin(115200);

    aura.begin();
    aura.setBrightness(255); 
    aura.show(); 

    pinMode(BLUE_LED_PIN, OUTPUT);
    digitalWrite(BLUE_LED_PIN, LOW);

    Wire.begin(17, 18);
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("MAX30102 not detected!");
        while (1);
    }
    particleSensor.setup((byte)currentBrightness, 4, 2, 200, 411, 4096); 
    particleSensor.enableDIETEMPRDY(); 

    I2C_OLED.begin(OLED_SDA, OLED_SCL, 400000);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println(F("SSD1306 allocation failed"));
    display.clearDisplay(); display.display();

    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi connected.");
    
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // START FIREBASE TASK ON CORE 0
    xTaskCreatePinnedToCore(
        firebaseUploadTask,   // Task function
        "FirebaseTask",       // Name of task
        8192,                 // Stack size
        NULL,                 // Task parameter
        1,                    // Priority
        NULL,                 // Task handle
        0                     // Pin to Core 0 (Loop runs on Core 1)
    );
}

// -----------------------
// LOOP (CORE 1)
// -----------------------
void loop() {
    adjustBrightnessPerceptron();

    long irValue  = particleSensor.getIR();
    long redValue = particleSensor.getRed();

    // 1. STATE MANAGEMENT
    if (irValue > 260000 || redValue > 260000) {
        currentState = STATE_ERROR; hasStableData = false; irValue = 0; redValue = 0; 
    } else if (irValue < 50000) {
        currentState = STATE_NO_FINGER; hasStableData = false; invalidCount = 0; lowSignalCount = 0;
    } else {
        if (instantACSignalIR < 20) lowSignalCount++; else lowSignalCount = 0;
        if (lowSignalCount > 50 || invalidCount > 10) { currentState = STATE_UNSTABLE; hasStableData = false; } 
        else if (hasStableData) { currentState = STATE_STABLE_OUTPUT; } 
        else { currentState = STATE_READING; }
    }

    // 2. DATA PROCESSING
    int BPM = calculateBPM(irValue, redValue);
    float spo2 = calculateDynamicSpO2(redValue, irValue);

    if (BPM > 40 && spo2 > 50 && sampleIndex < MAX_SAMPLES && irValue >= 50000) {
        bool isValid = (sampleIndex > 0) ? (abs(BPM - bpmBuffer[sampleIndex - 1]) <= 30) : true;
        if (isValid) { bpmBuffer[sampleIndex] = BPM; spo2Buffer[sampleIndex] = spo2; sampleIndex++; invalidCount = 0; } 
        else { invalidCount++; }
    }
    
    lastNeuralFatigue = calculateNeuralFatigue(lastIBI_ms, spo2, BPM);
    if (BPM > 0) { fatigueSum += lastNeuralFatigue; fatigueCount++; }

    // 3. FAST UI FEEDBACK
    if (!hasStableData && sampleIndex >= SAMPLES_FOR_UI_STABILITY) {
        ui_BPM = getMedianInt(bpmBuffer, sampleIndex);
        ui_SpO2 = getMedian(spo2Buffer, sampleIndex);
        ui_Status = getHypoxiaStatus(ui_SpO2);
        ui_Fatigue = (fatigueCount > 0) ? (fatigueSum / fatigueCount) / 4.5 : 0;
        if(ui_Fatigue > 30) ui_Fatigue = 30;
        hasStableData = true;
    }

    // 4. PREPARE DATA FOR FIREBASE BACKGROUND TASK
    if (millis() - tsLastFirebaseUpdate >= dataSendInterval) {
        if (WiFi.status() == WL_CONNECTED && sampleIndex > 5 && !sendDataFlag) {
            
            int medianBPM = getMedianInt(bpmBuffer, sampleIndex);
            float medianSpO2 = getMedian(spo2Buffer, sampleIndex);
            
            if (fatigueCount > 0) {
                finalSentFatigue = (int)((fatigueSum / fatigueCount) / 4.5);
                if (finalSentFatigue > 30) finalSentFatigue = 30;
                fatigueSum = 0; fatigueCount = 0;
            }

            ui_BPM = medianBPM; ui_SpO2 = medianSpO2;
            ui_Fatigue = finalSentFatigue; ui_Status = getHypoxiaStatus(medianSpO2);

            String payload = "{";
            payload += "\"timestamp_ms\":" + String(millis()) + ",";
            payload += "\"time\":\"" + getLocalTimeStr() + "\",";
            payload += "\"BPM\":" + String(medianBPM) + ","; 
            payload += "\"SpO2\":" + String(medianSpO2, 1) + ",";
            payload += "\"Hypoxia_Status\":\"" + ui_Status + "\",";
            payload += "\"Fatigue_Percent\":" + String(finalSentFatigue) + ",";
            payload += "\"IR_Raw\":" + String(irValue) + "}";

            // Hand the payload to Core 0
            asyncJsonPayload = payload;
            sendDataFlag = true; 

            sampleIndex = 0; 
        }
        tsLastFirebaseUpdate = millis();
    }

    // 5. SMOOTH ASYNC UI UPDATES
    updateAura(); 
    if (millis() - lastDisplayUpdate > DISPLAY_REFRESH_MS) { updateOLED(); lastDisplayUpdate = millis(); }

    if (millis() - tsLastReport >= 100) {
        Serial.print("IR_Raw:"); Serial.print(irValue);
        Serial.print(",IR_AC:"); Serial.print(instantACSignalIR);
        Serial.print(",BPM:"); Serial.print(BPM);
        Serial.print(",SpO2:"); Serial.print(spo2, 1);
        Serial.println();
        tsLastReport = millis();
    }
}