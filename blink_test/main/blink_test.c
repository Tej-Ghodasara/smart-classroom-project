#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Pin Definitions ---
const int PIN_MQ135[] = {32, 33, 34};
const int PIN_LDR[]   = {35, 36, 39};
const int PIN_DHT[]   = {15, 2, 4};
const int PIN_IR_OUT  = 12; // Outside door
const int PIN_IR_IN   = 13; // Inside door
const int PIN_IR_DESK = 14; 
const int PIN_LED     = 25;
const int PIN_SERVO   = 26;

// --- Thresholds & Config ---
#define GAS_THRESHOLD 2000     // Adjust based on your MQ135 calibration
#define LIGHT_THRESHOLD 1500   // Adjust based on LDR ambient light
#define DHTTYPE DHT11

DHT dht1(PIN_DHT[0], DHTTYPE);
DHT dht2(PIN_DHT[1], DHTTYPE);
DHT dht3(PIN_DHT[2], DHTTYPE);
Servo windowServo;

// --- Shared State Structure ---
struct SystemState {
    int occupants;
    float avgTemp;
    float avgHum;
    int avgGas;
    int avgLight;
    bool deskOccupied;
};

SystemState state = {0, 0.0, 0.0, 0, 0, false};
SemaphoreHandle_t stateMutex;

// --- Task Handles ---
TaskHandle_t OccupancyTaskHandle;
TaskHandle_t EnvironmentTaskHandle;
TaskHandle_t ControlTaskHandle;

// --- Occupancy Task (High Priority, Fast Polling) ---
void OccupancyTask(void *pvParameters) {
    int lastIrOut = HIGH;
    int lastIrIn = HIGH;
    
    for (;;) {
        // IR sensors typically read LOW when an object is detected
        int irOut = digitalRead(PIN_IR_OUT);
        int irIn = digitalRead(PIN_IR_IN);
        
        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            state.deskOccupied = (digitalRead(PIN_IR_DESK) == LOW);
            
            // Simple entry detection: Out triggered, then In triggered
            if (irOut == LOW && lastIrOut == HIGH) {
                // Wait briefly to see if they pass through the inside sensor
                vTaskDelay(pdMS_TO_TICKS(500)); 
                if (digitalRead(PIN_IR_IN) == LOW) {
                    state.occupants++;
                    Serial.println("Person Entered!");
                }
            }
            // Simple exit detection: In triggered, then Out triggered
            else if (irIn == LOW && lastIrIn == HIGH) {
                vTaskDelay(pdMS_TO_TICKS(500));
                if (digitalRead(PIN_IR_OUT) == LOW) {
                    if (state.occupants > 0) state.occupants--;
                    Serial.println("Person Exited!");
                }
            }
            xSemaphoreGive(stateMutex);
        }
        
        lastIrOut = irOut;
        lastIrIn = irIn;
        vTaskDelay(pdMS_TO_TICKS(50)); // Fast polling for IR
    }
}

// --- Environment Task (Medium Priority, Slow Polling) ---
void EnvironmentTask(void *pvParameters) {
    for (;;) {
        // Read DHT (takes time, hence separate task)
        float t1 = dht1.readTemperature();
        float t2 = dht2.readTemperature();
        float t3 = dht3.readTemperature();
        
        // Read Analogs
        long gasSum = 0, lightSum = 0;
        for (int i = 0; i < 3; i++) {
            gasSum += analogRead(PIN_MQ135[i]);
            lightSum += analogRead(PIN_LDR[i]);
        }

        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            // Calculate averages (ignoring NaN for simplicity in this example)
            state.avgTemp = (t1 + t2 + t3) / 3.0;
            state.avgGas = gasSum / 3;
            state.avgLight = lightSum / 3;
            xSemaphoreGive(stateMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // DHT11 requires at least 2s between reads
    }
}

// --- Actuator Control Task (Medium Priority) ---
void ControlTask(void *pvParameters) {
    for (;;) {
        SystemState localState;
        
        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            localState = state;
            xSemaphoreGive(stateMutex);
        }

        // 1. Lighting Logic: If room is occupied AND it's dark
        if (localState.occupants > 0 && localState.avgLight > LIGHT_THRESHOLD) {
            digitalWrite(PIN_LED, HIGH);
        } else {
            digitalWrite(PIN_LED, LOW);
        }

        // 2. Ventilation Logic: If gas/smoke is too high, open window
        if (localState.avgGas > GAS_THRESHOLD) {
            windowServo.write(90); // Open window 90 degrees
        } else {
            windowServo.write(0);  // Close window
        }

        // Debug output
        Serial.printf("Ppl: %d | Temp: %.1f | Gas: %d | Light: %d\n", 
                      localState.occupants, localState.avgTemp, localState.avgGas, localState.avgLight);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void setup() {
    Serial.begin(115200);
    
    // Init Pins
    pinMode(PIN_IR_OUT, INPUT_PULLUP);
    pinMode(PIN_IR_IN, INPUT_PULLUP);
    pinMode(PIN_IR_DESK, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);
    
    dht1.begin(); dht2.begin(); dht3.begin();
    
    // ESP32Servo requires allocation of timers
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    windowServo.setPeriodHertz(50);
    windowServo.attach(PIN_SERVO, 500, 2400); 

    // Create Mutex
    stateMutex = xSemaphoreCreateMutex();

    if (stateMutex != NULL) {
        // Create Tasks
        // xTaskCreate(Function, Name, Stack Size, Params, Priority, Handle)
        xTaskCreate(OccupancyTask, "Occupancy", 2048, NULL, 3, &OccupancyTaskHandle);
        xTaskCreate(EnvironmentTask, "Environment", 4096, NULL, 2, &EnvironmentTaskHandle);
        xTaskCreate(ControlTask, "Control", 2048, NULL, 2, &ControlTaskHandle);
    }
}

void loop() {
    // Empty. FreeRTOS scheduler handles execution.
    vTaskDelete(NULL); 
}