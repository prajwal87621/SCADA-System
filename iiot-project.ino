// iiot-project.ino - Single-file sketch with complete handler definitions

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_INA219.h>

// WiFi credentials - CHANGE THESE
const char* ssid = "Praj";
const char* password = "1234567890";

Adafruit_INA219 ina219;
WebServer server(80);

// Motor pins
int IN1 = 14;
int IN2 = 27;
int ENA = 25;
int IN3 = 26;
int IN4 = 33;
int ENB = 32;

// Motor states
bool motorA_state = false;
bool motorB_state = false;

// Power measurement variables
float busV = 0;
float cur = 0;
float powerW = 0;

// Forward declarations (prototypes) - must match definitions below
void handleRoot();
void handleMotorA();
void handleMotorB();
void handleStatus();
void sendStatus();

String wifiStatusToString(wl_status_t s) {
  switch (s) {
    case WL_NO_SHIELD: return "NO SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO SSID AVAILABLE";
    case WL_SCAN_COMPLETED: return "SCAN COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=== ESP32 Motor Control Web Server ===");

  // Setup motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENB, OUTPUT);

  // Initialize motors OFF
  digitalWrite(ENA, LOW);
  digitalWrite(ENB, LOW);

  // Initialize INA219
  if (!ina219.begin()) {
    Serial.println("INA219 not found - check wiring.");
  } else {
    Serial.println("INA219 ready.");
  }

  // Start WiFi in station mode
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("ESP32-Motor");
  Serial.printf("Attempting connect to SSID: '%s'\n", ssid);
  WiFi.begin(ssid, password);

  // Wait with timeout (15 seconds)
  unsigned long startAttempt = millis();
  const unsigned long connectTimeout = 15000UL; // 15 seconds
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < connectTimeout) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.printf("Failed to connect (status: %s). Starting Access Point fallback.\n", wifiStatusToString(WiFi.status()).c_str());
    // Start AP fallback so user can still reach device
    const char *apSSID = "ESP32_AP";
    const char *apPW = nullptr; // open AP — change if you want a password
    bool apStarted = WiFi.softAP(apSSID, apPW);
    if (apStarted) {
      Serial.printf("Access Point '%s' started. Connect to it and open 192.168.4.1\n", apSSID);
      Serial.print("AP IP: ");
      Serial.println(WiFi.softAPIP());
    } else {
      Serial.println("Failed to start AP. Check WiFi hardware.");
    }
  }

  // Setup web server routes (use explicit HTTP_GET for clarity)
  server.on("/", HTTP_GET, handleRoot);
  server.on("/motorA", HTTP_GET, handleMotorA);
  server.on("/motorB", HTTP_GET, handleMotorB);
  server.on("/status", HTTP_GET, handleStatus);

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();

  // Update power measurements periodically
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 500) {
    busV = ina219.getBusVoltage_V();
    cur = ina219.getCurrent_mA();
    powerW = busV * (cur / 1000.0);
    lastUpdate = millis();
  }
}

// ---------- Handlers & helpers ----------

void handleRoot() {
  // HTML page (raw string literal)
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Motor Control</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      background: white;
      border-radius: 20px;
      padding: 30px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
      max-width: 500px;
      width: 100%;
    }
    h1 { text-align: center; color: #333; margin-bottom: 10px; font-size: 28px; }
    .subtitle { text-align: center; color: #666; margin-bottom: 30px; font-size: 14px; }
    .motor-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 30px; }
    .motor-card { background: #f8f9fa; border-radius: 15px; padding: 25px; text-align: center; transition: all 0.3s ease; cursor: pointer; border: 3px solid transparent; }
    .motor-card:hover { transform: translateY(-5px); box-shadow: 0 10px 25px rgba(0,0,0,0.1); }
    .motor-card.active { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); border-color: #667eea; }
    .motor-card.active h2, .motor-card.active .status { color: white; }
    .motor-icon { font-size: 48px; margin-bottom: 10px; }
    h2 { color: #333; font-size: 20px; margin-bottom: 10px; }
    .status { font-size: 14px; font-weight: bold; padding: 5px 15px; border-radius: 20px; display: inline-block; margin-top: 5px; }
    .status.off { background: #e0e0e0; color: #666; }
    .status.on { background: rgba(255,255,255,0.3); color: white; }
    .power-info { background: #f8f9fa; border-radius: 15px; padding: 20px; margin-bottom: 20px; }
    .power-info h3 { color: #333; margin-bottom: 15px; font-size: 18px; }
    .power-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #e0e0e0; }
    .power-row:last-child { border-bottom: none; }
    .power-label { color: #666; font-size: 14px; }
    .power-value { color: #333; font-weight: bold; font-size: 14px; }
    .ip-address { text-align: center; color: #999; font-size: 12px; margin-top: 15px; }
    @media (max-width: 480px) { .motor-grid { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚙️ Motor Control</h1>
    <p class="subtitle">ESP32 Web Interface</p>
    
    <div class="motor-grid">
      <div class="motor-card" id="motorA" onclick="toggleMotor('A')">
        <div class="motor-icon">🔧</div>
        <h2>Motor A</h2>
        <span class="status off" id="statusA">OFF</span>
      </div>
      
      <div class="motor-card" id="motorB" onclick="toggleMotor('B')">
        <div class="motor-icon">⚙️</div>
        <h2>Motor B</h2>
        <span class="status off" id="statusB">OFF</span>
      </div>
    </div>
    
    <div class="power-info">
      <h3>📊 Power Monitoring</h3>
      <div class="power-row">
        <span class="power-label">Voltage:</span>
        <span class="power-value" id="voltage">0.00 V</span>
      </div>
      <div class="power-row">
        <span class="power-label">Current:</span>
        <span class="power-value" id="current">0.00 mA</span>
      </div>
      <div class="power-row">
        <span class="power-label">Power:</span>
        <span class="power-value" id="power">0.0000 W</span>
      </div>
    </div>
    
    <div class="ip-address">ESP32 IP: <span id="ipAddress">Loading...</span></div>
  </div>

  <script>
    function toggleMotor(motor) {
      fetch('/motor' + motor)
        .then(response => response.json())
        .then(data => {
          updateUI(data);
        })
        .catch(error => console.error('Error:', error));
    }
    
    function updateStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          updateUI(data);
          // show ip if provided
          if (data.ip) document.getElementById('ipAddress').textContent = data.ip;
        })
        .catch(error => console.error('Error:', error));
    }
    
    function updateUI(data) {
      // Update Motor A
      const motorACard = document.getElementById('motorA');
      const statusA = document.getElementById('statusA');
      if (data.motorA) {
        motorACard.classList.add('active');
        statusA.textContent = 'ON';
        statusA.className = 'status on';
      } else {
        motorACard.classList.remove('active');
        statusA.textContent = 'OFF';
        statusA.className = 'status off';
      }
      
      // Update Motor B
      const motorBCard = document.getElementById('motorB');
      const statusB = document.getElementById('statusB');
      if (data.motorB) {
        motorBCard.classList.add('active');
        statusB.textContent = 'ON';
        statusB.className = 'status on';
      } else {
        motorBCard.classList.remove('active');
        statusB.textContent = 'OFF';
        statusB.className = 'status off';
      }
      
      // Update power info
      document.getElementById('voltage').textContent = data.voltage.toFixed(2) + ' V';
      document.getElementById('current').textContent = data.current.toFixed(2) + ' mA';
      document.getElementById('power').textContent = data.power.toFixed(4) + ' W';
    }
    
    // Update status every 1 second
    setInterval(updateStatus, 1000);
    // Initial update
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

  // Send HTML
  server.send(200, "text/html", html);
}

void handleMotorA() {
  motorA_state = !motorA_state;

  if (motorA_state) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(ENA, HIGH);
    Serial.println("Motor A turned ON");
  } else {
    digitalWrite(ENA, LOW);
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    Serial.println("Motor A turned OFF");
  }

  sendStatus();
}

void handleMotorB() {
  motorB_state = !motorB_state;

  if (motorB_state) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    digitalWrite(ENB, HIGH);
    Serial.println("Motor B turned ON");
  } else {
    digitalWrite(ENB, LOW);
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
    Serial.println("Motor B turned OFF");
  }

  sendStatus();
}

void handleStatus() {
  sendStatus();
}

void sendStatus() {
  // Include IP in status so UI can show it (works whether STA or AP)
  String ipStr = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

  String json = "{";
  json += "\"motorA\":" + String(motorA_state ? "true" : "false") + ",";
  json += "\"motorB\":" + String(motorB_state ? "true" : "false") + ",";
  json += "\"voltage\":" + String(busV, 2) + ",";
  json += "\"current\":" + String(cur, 2) + ",";
  json += "\"power\":" + String(powerW, 4) + ",";
  json += "\"ip\":\"" + ipStr + "\"";
  json += "}";

  server.send(200, "application/json", json);
}
