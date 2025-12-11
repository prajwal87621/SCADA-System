/* ESP32 WebSocket + INA219 motor controller
   Matches backend WebSocket protocol in server.js
   - Sends: { type: "esp32_register" } on connect
   - Receives: { type: "motor_command", motor: "A"/"B", state: true/false }
   - Sends periodic: { type: "state_update", motorA, motorB, voltage, current, power }
*/

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_INA219.h>

// ========== CONFIG ==========
const char* ssid = "prajwal";            // <-- change
const char* password = "Prajwal87621@"; // <-- change

// WebSocket host (no protocol)
const char* BACKEND_HOST = "scada-system.onrender.com";
const uint16_t BACKEND_PORT = 443;     // 443 for wss
const char* BACKEND_PATH = "/";        // change if your WS has a path

// Timing
const unsigned long STATE_UPDATE_INTERVAL = 3000;  // send state every 3s
const unsigned long POWER_READ_INTERVAL   = 500;   // read power every 500ms

// ========== HARDWARE ==========
Adafruit_INA219 ina219;

// Motor pins (as in your original)
const int IN1 = 14;
const int IN2 = 27;
const int ENA = 25;
const int IN3 = 26;
const int IN4 = 33;
const int ENB = 32;

// ========== STATE ==========
bool motorA_state = false;
bool motorB_state = false;

float busV = 0.0;
float cur_mA = 0.0;
float powerW = 0.0;

unsigned long lastStateSend = 0;
unsigned long lastPowerRead = 0;

// WebSocket client
WebSocketsClient webSocket;

// Forward declarations
void connectWiFi();
void readPowerData();
void setMotorA(bool state);
void setMotorB(bool state);
void sendStateUpdate();             // send state_update via WS
void handleIncomingJson(const String &payload);

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 WebSocket Motor Controller ===");

  // Motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENB, OUTPUT);

  setMotorA(false);
  setMotorB(false);

  // INA219 init
  Serial.print("Init INA219... ");
  if (!ina219.begin()) {
    Serial.println("FAILED - check wiring!");
  } else {
    Serial.println("OK");
  }

  // WiFi
  connectWiFi();

  // WebSocket init (wss)
  // For secure WebSocket use beginSSL(host, port, path)
  webSocket.beginSSL(BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
  webSocket.onEvent([](WStype_t type, uint8_t * payload, size_t length) {
    // convert payload to String for easier Json handling
    String data = "";
    if (payload && length) {
      data = String((char*)payload);
    }

    switch(type) {
      case WStype_CONNECTED:
        Serial.println("✅ WS Connected");
        // Register as ESP32
        {
          StaticJsonDocument<256> doc;
          doc["type"] = "esp32_register";
          // optionally include an id
          doc["id"] = "esp32-unit-1";
          String out;
          serializeJson(doc, out);
          webSocket.sendTXT(out);
          Serial.println("→ Sent esp32_register");
        }
        break;

      case WStype_TEXT:
        Serial.print("📥 WS TEXT: ");
        Serial.println(data);
        handleIncomingJson(data);
        break;

      case WStype_DISCONNECTED:
        Serial.println("❌ WS Disconnected");
        break;

      case WStype_ERROR:
        Serial.println("⚠️ WS Error");
        break;

      default:
        break;
    }
  });

  webSocket.setReconnectInterval(5000); // auto reconnect every 5s
  lastStateSend = millis();
  lastPowerRead = millis();
}

// ========== LOOP ==========
void loop() {
  // maintain websocket
  webSocket.loop();

  // wifi reconnect (if disconnected)
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiAttempt = 0;
    if (millis() - lastWiFiAttempt > 5000) {
      Serial.println("WiFi lost. Reconnecting...");
      connectWiFi();
      lastWiFiAttempt = millis();
    }
  }

  // read power periodically
  if (millis() - lastPowerRead >= POWER_READ_INTERVAL) {
    readPowerData();
    lastPowerRead = millis();
  }

  // send periodic state updates
  if (millis() - lastStateSend >= STATE_UPDATE_INTERVAL) {
    sendStateUpdate();
    lastStateSend = millis();
  }
}

// ========== WIFI ==========
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  } else {
    Serial.println("\n✗ WiFi connection failed");
  }
}

// ========== POWER READ ==========
void readPowerData() {
  // INA219 library returns bus voltage and current in mA
  busV = ina219.getBusVoltage_V();
  cur_mA = ina219.getCurrent_mA();
  powerW = busV * (cur_mA / 1000.0);
}

// ========== MOTOR CONTROL ==========
void setMotorA(bool state) {
  motorA_state = state;
  if (state) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(ENA, HIGH);
    Serial.println("→ Motor A: ON");
  } else {
    digitalWrite(ENA, LOW);
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    Serial.println("→ Motor A: OFF");
  }
}

void setMotorB(bool state) {
  motorB_state = state;
  if (state) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    digitalWrite(ENB, HIGH);
    Serial.println("→ Motor B: ON");
  } else {
    digitalWrite(ENB, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
    Serial.println("→ Motor B: OFF");
  }
}

// ========== SEND STATE VIA WS ==========
void sendStateUpdate() {
  if (webSocket.isConnected()) {
    StaticJsonDocument<256> doc;
    doc["type"] = "state_update";
    doc["motorA"] = motorA_state;
    doc["motorB"] = motorB_state;
    doc["voltage"] = busV;
    doc["current"] = cur_mA;
    doc["power"] = powerW;
    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(out);
    Serial.print("↑ Sent state_update: ");
    Serial.println(out);
  } else {
    // Optionally you can POST to REST fallback endpoint if WS is down.
    Serial.println("WS not connected - skipping state_update");
  }
}

// ========== HANDLE INCOMING JSON ==========
void handleIncomingJson(const String &payload) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  const char* type = doc["type"];
  if (!type) return;

  if (strcmp(type, "motor_command") == 0) {
    const char* motor = doc["motor"];
    bool state = doc["state"] | false;
    Serial.print("Command -> motor: "); Serial.print(motor); Serial.print(" state: "); Serial.println(state ? "ON" : "OFF");
    if (motor && strcmp(motor, "A") == 0) {
      setMotorA(state);
    } else if (motor && strcmp(motor, "B") == 0) {
      setMotorB(state);
    }
    // After executing, send updated state immediately
    sendStateUpdate();
  }
  else if (strcmp(type, "initial_state") == 0) {
    // Server sent initial_state on connect (from server.js)
    bool mA = doc["motorA"] | false;
    bool mB = doc["motorB"] | false;
    Serial.println("Applying initial_state from server");
    setMotorA(mA);
    setMotorB(mB);
    sendStateUpdate();
  }
  // handle other incoming types if you need
}
