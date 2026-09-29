// Blynk credentials
#define BLYNK_TEMPLATE_ID "TMPL6StGnkA9_"
#define BLYNK_TEMPLATE_NAME "Doan1"
#define BLYNK_AUTH_TOKEN "KsYquOO-45jfkyXAWtUTUEwyircm_mcd"

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>


const char* ssid = "jiaahjien";
const char* password = "giahiencomthu";

#define VPIN_TEMPERATURE  V5
#define VPIN_HUMIDITY     V6
#define VPIN_LED_CONTROL  V4 

#define PIN_SG90      18
#define PIN_PIR       5
#define PIN_LED       23
#define PIN_DHT       0
#define DHTTYPE       DHT11


#define TIME_DOOR_OPEN             5000
#define TIME_LED_TIMEOUT           5000
#define TIME_PASSWORD_TIMEOUT      10000
#define TIME_WRONG_PASSWORD_WAIT   10000
#define TIME_LCD_UPDATE_INTERVAL   2000
#define TIME_BLYNK_UPDATE_INTERVAL 5000  


enum SystemState {
    DISPLAY_DHT,
    WAIT_FOR_PASSWORD,
    OPEN_DOOR,
    WRONG_PASSWORD_WAIT
};


DHT dht(PIN_DHT, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo sg90;
BlynkTimer timer;


const byte ROWS = 4, COLS = 3;
char hexaKeys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};
byte rowPins[ROWS] = {26, 25, 33, 32};
byte colPins[COLS] = {13, 12, 14};
Keypad keypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS );


char passwordChar = '#';   
char inputChar = '\0';     
volatile uint8_t errorCount = 0;
volatile SystemState currentState = DISPLAY_DHT;
volatile unsigned long ledActivatedTime = 0;
volatile bool ledFlag = false;
volatile bool manualLedControl = false;
volatile unsigned long passwordEntryStartTime = 0;
unsigned long lastDisplayUpdate = 0;

TaskHandle_t lcdTaskHandle, keypadTaskHandle, mainTaskHandle, blynkTaskHandle;

void lcdTask(void *parameter);
void keypadTask(void *parameter);
void mainTask(void *parameter);
void blynkTask(void *parameter);
void sendSensorDataToBlynk();

BLYNK_WRITE(VPIN_LED_CONTROL) {
    int value = param.asInt();
    Serial.printf("Blynk V4 command: %d\n", value);
    if (value == 1) {
        manualLedControl = !manualLedControl;
        digitalWrite(PIN_LED, manualLedControl ? HIGH : LOW);
        ledFlag = false;
        Serial.println(manualLedControl ? "LED manually ON - PIR disabled" : "LED manually OFF - PIR enabled");
    }
}

void IRAM_ATTR handlePIR() {
    if (!manualLedControl && digitalRead(PIN_PIR) == HIGH) {
        ledActivatedTime = millis();
        ledFlag = true;
        digitalWrite(PIN_LED, HIGH);
    }
}

void keypadTask(void *parameter) {
    char key;
    for (;;) {
        key = keypad.getKey();
        if (key) {
            Serial.printf("Key pressed: %c\n", key);
            if (key == '*' && currentState == DISPLAY_DHT) {
                currentState = WAIT_FOR_PASSWORD;
                passwordEntryStartTime = millis();
                inputChar = '\0';
                Serial.println("Changed to password entry mode");
            }
            else if (currentState == WAIT_FOR_PASSWORD) {
                inputChar = key;
                if (inputChar == passwordChar) {
                    currentState = OPEN_DOOR;
                    inputChar = '\0';
                    Serial.println("Password correct! Opening door...");
                } else {
                    errorCount++;
                    Serial.printf("Wrong password! Attempts: %d\n", errorCount);
                    inputChar = '\0';
                    if (errorCount >= 3) {
                        currentState = WRONG_PASSWORD_WAIT;
                        errorCount = 0;
                        Serial.println("Too many wrong attempts!");
                    }
                }
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void lcdTask(void *parameter) {
    float temperature, humidity;
    char lcdBuffer[17];
    
    for (;;) {
        switch (currentState) {
            case DISPLAY_DHT:
                if (millis() - lastDisplayUpdate >= TIME_LCD_UPDATE_INTERVAL) {
                    temperature = dht.readTemperature();
                    humidity = dht.readHumidity();
                    if (!isnan(temperature) && !isnan(humidity)) {
                        lcd.clear();
                        snprintf(lcdBuffer, sizeof(lcdBuffer), "Temp: %.1f C %c", temperature, manualLedControl ? 'M' : 'A');
                        lcd.setCursor(0, 0);
                        lcd.print(lcdBuffer);
                        snprintf(lcdBuffer, sizeof(lcdBuffer), "Hum:  %.1f %%", humidity);
                        lcd.setCursor(0, 1);
                        lcd.print(lcdBuffer);
                        lastDisplayUpdate = millis();
                    }
                }
                vTaskDelay(500 / portTICK_PERIOD_MS);
                break;
                
            case WAIT_FOR_PASSWORD:
                lcd.clear();
                lcd.setCursor(1, 0);
                lcd.print("Enter Password");
                snprintf(lcdBuffer, sizeof(lcdBuffer), "Time: %ds %c", 
                         (TIME_PASSWORD_TIMEOUT - (millis() - passwordEntryStartTime)) / 1000,
                         inputChar ? '*' : ' ');
                lcd.setCursor(0, 1);
                lcd.print(lcdBuffer);
                if (millis() - passwordEntryStartTime > TIME_PASSWORD_TIMEOUT) {
                    currentState = DISPLAY_DHT;
                    Serial.println("Password timeout - back to DHT display");
                }
                vTaskDelay(200 / portTICK_PERIOD_MS);
                break;
                
            case OPEN_DOOR:
                lcd.clear();
                lcd.setCursor(1, 0);
                lcd.print("---OPENDOOR---");
                sg90.write(90);
                vTaskDelay(TIME_DOOR_OPEN / portTICK_PERIOD_MS);
                sg90.write(0);
                currentState = DISPLAY_DHT;
                errorCount = 0;
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                break;
                
            case WRONG_PASSWORD_WAIT:
                lcd.clear();
                lcd.setCursor(1, 0);
                lcd.print("WRONG 3 TIMES");
                lcd.setCursor(1, 1);
                lcd.print("Wait 10 seconds");
                vTaskDelay(TIME_WRONG_PASSWORD_WAIT / portTICK_PERIOD_MS);
                currentState = DISPLAY_DHT;
                errorCount = 0;
                break;
        }
    }
}

void mainTask(void *parameter) {
    for (;;) {
        if (!manualLedControl && ledFlag && (millis() - ledActivatedTime >= TIME_LED_TIMEOUT)) {
            digitalWrite(PIN_LED, LOW);
            ledFlag = false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void blynkTask(void *parameter) {
    for (;;) {
        Blynk.run();
        timer.run();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void sendSensorDataToBlynk() {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (!isnan(temperature) && !isnan(humidity)) {
        Blynk.virtualWrite(VPIN_TEMPERATURE, temperature);
        Blynk.virtualWrite(VPIN_HUMIDITY, humidity);
        Serial.printf("Sent to Blynk: Temp=%.1f°C, Humidity=%.1f%%\n", temperature, humidity);
    } else {
        Serial.println("Failed to read from DHT sensor!");
    }
}


const uint8_t GAS_SENSOR_KITCHEN = 34;
const uint8_t BUZZER_KITCHEN      = 4;
const uint8_t LED_WARNING_KITCHEN = 16;
const uint8_t LED_KITCHEN         = 19;


const uint8_t LED_BEDROOM         = 2;
const uint8_t RELAY_PIN_BEDROOM   = 27;


bool ledState_kitchen = false;
bool ledState_bedroom = false;
bool fanState_bedroom = false;

const uint16_t GAS_THRESHOLD        = 800;
const uint16_t RECONNECT_DELAY      = 5000;  
const uint16_t SENSOR_CHECK_INTERVAL= 2000;   

void checkGasSensor() {
  int gasValue = analogRead(GAS_SENSOR_KITCHEN);
  Serial.print("Phòng bếp: Gas Value: ");
  Serial.println(gasValue);

  bool isDangerous = gasValue > GAS_THRESHOLD;
  
  digitalWrite(LED_WARNING_KITCHEN, isDangerous ? HIGH : LOW);
  
  if (isDangerous) {
    tone(BUZZER_KITCHEN, 1000);
    Blynk.virtualWrite(V1, "Nguy hiểm!");
    Serial.println("Phòng bếp: Cảnh báo! Khí gas vượt ngưỡng!");
  } else {
    noTone(BUZZER_KITCHEN);
    Blynk.virtualWrite(V1, "An toàn");
  }
}

void checkConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Mất WiFi, đang kết nối lại...");
    WiFi.begin(ssid, password);
  }
  if (!Blynk.connected()) {
    Serial.println("Mất kết nối Blynk, đang thử lại...");
    Blynk.connect();
  }
}

BLYNK_WRITE(V0) {
  ledState_kitchen = param.asInt();
  digitalWrite(LED_KITCHEN, ledState_kitchen);
}

BLYNK_WRITE(V2) {
  ledState_bedroom = param.asInt();
  digitalWrite(LED_BEDROOM, ledState_bedroom);
}

BLYNK_WRITE(V3) {
  fanState_bedroom = param.asInt();
  digitalWrite(RELAY_PIN_BEDROOM, fanState_bedroom ? HIGH : LOW);
  Serial.println(fanState_bedroom ? "Quạt: BẬT (Blynk)" : "Quạt: TẮT (Blynk)");
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_LED, OUTPUT);
    sg90.setPeriodHertz(50);
    sg90.attach(PIN_SG90, 500, 2400);
    lcd.init();
    lcd.backlight();
    lcd.print("   SYSTEM INIT   ");
    delay(1000);
    pinMode(BUZZER_KITCHEN, OUTPUT);
    pinMode(LED_WARNING_KITCHEN, OUTPUT);
    pinMode(LED_KITCHEN, OUTPUT);
    pinMode(LED_BEDROOM, OUTPUT);
    pinMode(RELAY_PIN_BEDROOM, OUTPUT);
    digitalWrite(RELAY_PIN_BEDROOM, LOW);
    digitalWrite(LED_WARNING_KITCHEN, LOW);
    digitalWrite(LED_KITCHEN, LOW);
    digitalWrite(LED_BEDROOM, LOW);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    WiFi.begin(ssid, password);
    int wifiAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
        delay(500);
        lcd.setCursor(wifiAttempts % 16, 1);
        lcd.print(".");
        wifiAttempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected!");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString());
        delay(2000);
        Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed!");
        lcd.setCursor(0, 1);
        lcd.print("Running offline");
        delay(2000);
    }
    
    dht.begin();
    
    attachInterrupt(digitalPinToInterrupt(PIN_PIR), handlePIR, CHANGE);
    
    Serial.printf("PASSWORD: %c\n", passwordChar);
    
    xTaskCreatePinnedToCore(lcdTask, "LCD", 4096, NULL, 1, &lcdTaskHandle, 1);
    xTaskCreatePinnedToCore(keypadTask, "Keypad", 2048, NULL, 2, &keypadTaskHandle, 1);
    xTaskCreatePinnedToCore(mainTask, "Main", 2048, NULL, 1, &mainTaskHandle, 0);
    xTaskCreatePinnedToCore(blynkTask, "Blynk", 4096, NULL, 1, &blynkTaskHandle, 0);
    
    timer.setInterval(TIME_BLYNK_UPDATE_INTERVAL, sendSensorDataToBlynk);
    timer.setInterval(SENSOR_CHECK_INTERVAL, checkGasSensor);
    timer.setInterval(RECONNECT_DELAY, checkConnection);
}

void loop() {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}