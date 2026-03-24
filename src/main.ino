// Ex09.cpp
// COM3505 – Exercise 09
// Exercise 09: ESP32 Wi-Fi Provisioning using AP mode
// Added: Live dashboard with AJAX sensor readings and LED pattern controls

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// Access Point credentials
const char* apSSID     = "ESP32-Setup";
const char* apPassword = "12345678";

// Flask server – update this to your PC's local IP before flashing
const char* FLASK_SERVER    = "http://192.168.1.144:5000";
const unsigned long POST_INTERVAL = 2000;   // POST sensor data every 2 s
unsigned long lastPost = 0;

// LED and Sensor pins
const int TMP36_PIN = 9;

const int LED1 = 10;  // Red LED
const int LED2 = 5;   // Orange LED
const int LED3 = 6;   // Green LED

// Aliases used by the temp-sensitive pattern for readability
const int LED_RED    = LED1;
const int LED_ORANGE = LED2;
const int LED_GREEN  = LED3;


// Web server
WebServer webServer(80);

// ---------- Simulated state ----------
int   currentPattern = 0;                          // 0=Off 1=Blink 2=Chase 3=Pulse 4=Temp Sensitive
const char* patternNames[] = { "Off", "Blink", "Chase", "Pulse", "Temp Sensitive" };
const int   NUM_PATTERNS   = 5;

// Pattern timing variables
unsigned long patternTimer = 0;
int blinkState = 0;
int chaseStep = 0;
int pulseDirection = 1;
int pulseBrightness = 0;

// Sensor data storage
float lastTemperature = 0.0;

// Forward declarations
void hndlRoot();
void hndlWifi();
void hndlWifichz();
void hndlStatus();
void hndlDashboard();
void hndlSensorData();
void hndlSetPattern();
void apListForm(String &f);
String ip2str(IPAddress addr);
String wifiStatusStr();
void updateLEDPattern();
void postSensorData();      // POST temperature to Flask every 2 s
void applyPatternFromJson(String json); // parse pattern field from Flask response

// ================================================================
void setup() {
  
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting Ex09...");

  randomSeed(analogRead(0));   // Seed RNG from floating pin

  WiFi.mode(WIFI_AP_STA);
// In setup(), after WiFi.mode(WIFI_AP_STA):
  WiFi.softAP(apSSID, apPassword);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  // Scan once at boot before the web server starts
  Serial.println("Scanning networks...");
  WiFi.scanNetworks();  // blocking here is fine — no clients yet
  Serial.println("Scan done.");

  webServer.begin();
    // Routes
    webServer.on("/",            hndlRoot);
    webServer.on("/wifi",        hndlWifi);
    webServer.on("/wifichz",     hndlWifichz);
    webServer.on("/status",      hndlStatus);
    webServer.on("/dashboard",   hndlDashboard);
    webServer.on("/sensordata",  hndlSensorData);   // AJAX endpoint
    webServer.on("/setpattern",  hndlSetPattern);   // AJAX endpoint
    webServer.onNotFound([](){
      webServer.send(404, "text/plain", "Page not found");
    });

  webServer.begin();
  Serial.println("HTTP server started");
}

void loop() {
  webServer.handleClient();
  postSensorData();       // Send temperature to Flask; apply any pattern update
  updateLEDPattern();
  delay(10);  // Small delay to prevent overwhelming the board
}

// ================================================================
// Root / Home page
// ================================================================
void hndlRoot() {
  String localIP = (WiFi.status() == WL_CONNECTED)
                   ? ip2str(WiFi.localIP()) : "Not connected";

  String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32</title>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style>
</head>
<body>
  <h2>ESP32 Home</h2>
  <p>Status: )rawhtml";

  html += (WiFi.status() == WL_CONNECTED) ? "<b>Connected</b>" : "Disconnected";
  html += " &mdash; ";
  html += (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "no network";

  html += R"rawhtml(<br>
  Local IP: )rawhtml";
  html += localIP;
  html += R"rawhtml(<br>
  AP IP: )rawhtml";
  html += ip2str(WiFi.softAPIP());
  html += R"rawhtml(</p>
  <p>
    <a href='/wifi'>Connect to Wi-Fi</a> |
    <a href='/status'>Status</a> |
    <a href='http://192.168.1.144:5000'>Flask Dashboard</a>
  </p>
  <p><small>If the Flask link does not load, make sure your device is connected
  to the same Wi-Fi network as this ESP32, not the ESP32-Setup hotspot.</small></p>
</body>
</html>
)rawhtml";

  webServer.send(200, "text/html", html);
}

// ================================================================
// Dashboard page – AJAX-powered live updates
// ================================================================
void hndlDashboard() {
  String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Dashboard</title>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style>
</head>
<body>
  <p><a href='/'>Home</a></p>
  <h2>Dashboard</h2>

  <p>Temperature: <b id='temp'>--</b> &deg;C</p>

  <p><b>Temperature History</b></p>
  <canvas id='tempChart' height='200'></canvas>

  <p><b>LED Pattern:</b> <span id='patternName'>--</span></p>
  <div>
    <button class='pattern-btn' onclick='setPattern(0)'>Off</button>
    <button class='pattern-btn' onclick='setPattern(1)'>Blink</button>
    <button class='pattern-btn' onclick='setPattern(2)'>Chase</button>
    <button class='pattern-btn' onclick='setPattern(3)'>Pulse</button>
    <button class='pattern-btn' onclick='setPattern(4)'>Temp</button>
  </div>

  <p><small>Updating every 2s &mdash; last update: <span id='lastUpdate'>--</span></small></p>

  <script>
    // Fetch sensor + pattern data from /sensordata every 2 seconds
    function fetchData() {
      fetch('/sensordata')
        .then(r => r.json())
        .then(d => {
          document.getElementById('temp').textContent = d.temperature;
          pushTemperature(parseFloat(d.temperature));

          document.getElementById('patternName').textContent = d.patternName;

          // Update dot style
          const dot = document.getElementById('ledDot');
          dot.className = d.pattern === 0 ? 'dot off' : 'dot';

          // Highlight active button
          document.querySelectorAll('.pattern-btn').forEach((btn, i) => {
            btn.classList.toggle('active', i === d.pattern);
          });

          // Timestamp
          const now = new Date();
          document.getElementById('lastUpdate').textContent =
            now.toLocaleTimeString();
        })
        .catch(() => {
          document.getElementById('lastUpdate').textContent = 'Error – retrying...';
        });
    }

    // Send chosen pattern to /setpattern?id=N
    function setPattern(id) {
      fetch('/setpattern?id=' + id)
        .then(() => fetchData());   // Refresh immediately after change
    }

    // Chart state (manual drawing)
    const tempHistory = [];
    const maxSamples = 30; // 30 samples at 2 sec = 60 sec

    const canvas = document.getElementById('tempChart');
    const ctx = canvas.getContext('2d');

    function drawGraph() {
      const width = canvas.width;
      const height = canvas.height;
      const padding = 30;

      // clear
      ctx.fillStyle = '#0d1117';
      ctx.fillRect(0, 0, width, height);

      // axis lines
      ctx.strokeStyle = '#586e75';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(padding, padding);
      ctx.lineTo(padding, height - padding);
      ctx.lineTo(width - padding, height - padding);
      ctx.stroke();

      if (tempHistory.length === 0) return;

      const maxTemp = Math.max(50, ...tempHistory.map(e => e));
      const minTemp = Math.min(0, ...tempHistory.map(e => e));
      const range = maxTemp - minTemp || 1;

      // draw lines
      ctx.strokeStyle = '#ff4136';
      ctx.lineWidth = 2;
      ctx.beginPath();
      tempHistory.forEach((temp, idx) => {
        const x = padding + (idx / (maxSamples - 1 || 1)) * (width - 2 * padding);
        const y = height - padding - ((temp - minTemp) / range) * (height - 2 * padding);
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();

      // draw points
      ctx.fillStyle = '#ff4136';
      tempHistory.forEach((temp, idx) => {
        const x = padding + (idx / (maxSamples - 1 || 1)) * (width - 2 * padding);
        const y = height - padding - ((temp - minTemp) / range) * (height - 2 * padding);
        ctx.beginPath();
        ctx.arc(x, y, 2.5, 0, Math.PI * 2);
        ctx.fill();
      });

      // y-axis labels
      ctx.fillStyle = '#eee';
      ctx.font = '12px sans-serif';
      for (let i = 0; i <= 5; i++) {
        const value = minTemp + (i / 5) * range;
        const y = height - padding - (i / 5) * (height - 2 * padding);
        ctx.fillText(value.toFixed(1) + '°C', 3, y + 4);
      }
    }

    function pushTemperature(value) {
      if (tempHistory.length >= maxSamples) tempHistory.shift();
      tempHistory.push(value);
      drawGraph();
    }

    fetchData();                    // Load immediately on page open
    setInterval(fetchData, 2000);   // Then every 2 seconds
  </script>
</body>
</html>
)rawhtml";

  webServer.send(200, "text/html", html);
}

// ================================================================
// AJAX endpoint – returns JSON with temperature sensor data
// ================================================================
void hndlSensorData() {
  // TMP36 temperature sensor reading (analog value 0-4095)
  int raw = analogRead(TMP36_PIN);
  float voltage = raw * (3.3 / 4095.0);
  float temperature = (voltage - 0.5) * 100.0;

  // Store sensor value for pattern use
  lastTemperature = temperature;

  String json = "{";
  json += "\"temperature\":"  + String(temperature, 1) + ",";
  json += "\"pattern\":"      + String(currentPattern) + ",";
  json += "\"patternName\":\"" + String(patternNames[currentPattern]) + "\"";
  json += "}";

  // Allow AJAX from any origin
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "application/json", json);
}

// ================================================================
// AJAX endpoint – change LED pattern
// ================================================================
void hndlSetPattern() {
  if (webServer.hasArg("id")) {
    int id = webServer.arg("id").toInt();
    if (id >= 0 && id < NUM_PATTERNS) {
      // Only detach PWM when leaving a PWM pattern (Pulse=3, Temp=4)
      // Detaching unconditionally on digital patterns leaves pins undefined
      if (currentPattern == 3 || currentPattern == 4) {
        analogWrite(LED1, 0);
        analogWrite(LED2, 0);
        analogWrite(LED3, 0);
        ledcDetachPin(LED1);
        ledcDetachPin(LED2);
        ledcDetachPin(LED3);
      }

      currentPattern = id;
      patternTimer = millis();                 // reset timer to now
      blinkState = 0;
      chaseStep = 0;
      pulseBrightness = 0;
      pulseDirection = 1;

      // force immediate baseline state for new pattern
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);

      if (currentPattern == 0) {
        // keep off
      } else if (currentPattern == 1) {
        // start blink with off state; updateLEDPattern will flip after interval
      } else if (currentPattern == 2) {
        chaseStep = 0;
      } else if (currentPattern == 3) {
        pulseBrightness = 0;
      }

      Serial.print("Pattern changed to: ");
      Serial.println(patternNames[currentPattern]);

      // immediately apply the new pattern state to avoid stale behavior
      updateLEDPattern();
    }
  }
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "text/plain", "OK");
}

// ================================================================
// Wi-Fi selection page
// ================================================================
void hndlWifi() {
  String form = "";
  apListForm(form);
  webServer.send(200, "text/html", form);
}

// ================================================================
// Handle Wi-Fi form submission
// ================================================================
void hndlWifichz() {
  String ssid = webServer.arg("ssid");
  String key  = webServer.arg("key");
  String msg;

  if (ssid == "") {
    msg = "<h2>Error: No SSID selected!</h2>";
  } else {
    char ssidchars[ssid.length() + 1];
    char keychars[key.length() + 1];
    ssid.toCharArray(ssidchars, ssid.length() + 1);
    key.toCharArray(keychars, key.length() + 1);

    WiFi.begin(ssidchars, keychars);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      msg = "<h2>Connected!</h2><p>IP: " + ip2str(WiFi.localIP()) + "</p>";
    } else {
      msg = "<h2>Connection failed.</h2><p>Try again.</p>";
    }
  }
  String head = "<!DOCTYPE html><html><head><title>ESP32</title><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style></head><body><p><a href='/'>Home</a></p>";
  webServer.send(200, "text/html",
    head + msg + "<p><a href='/status'>Status</a> | <a href='/wifi'>Try again</a></p></body></html>");
}

// ================================================================
// Status page
// ================================================================
void hndlStatus() {
  String s = "<!DOCTYPE html><html><head><title>ESP32</title><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style></head><body>";
  s += "<p><a href='/'>Home</a></p>";
  s += "<h2>Wi-Fi Status</h2><ul>";
  s += "<li>SSID: " + WiFi.SSID() + "</li>";
  s += "<li>Status: " + wifiStatusStr() + "</li>";
  s += "<li>Local IP: "  + ip2str(WiFi.localIP())   + "</li>";
  s += "<li>Soft AP IP: " + ip2str(WiFi.softAPIP()) + "</li>";
  s += "<li>AP SSID: "   + String(apSSID)           + "</li></ul>";
  s += "<p><a href='/wifi'>Connect to Wi-Fi</a></p></body></html>";
  webServer.send(200, "text/html", s);
}

// ================================================================
// Helpers
// ================================================================


void apListForm(String &f) {
  int n = WiFi.scanComplete();
  if (n <= 0) {
    // Trigger a fresh blocking scan — only runs when user explicitly asks
    n = WiFi.scanNetworks();
  }
  if (n <= 0) {
    f = "<p>No networks found. <a href='/wifi'>Try again</a></p>";
    return;
  }
  f  = "<!DOCTYPE html><html><head><title>ESP32</title><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>body{font-family:sans-serif;max-width:500px;margin:20px auto;padding:0 12px}canvas{width:100%}button{margin:4px 2px;padding:6px 12px;cursor:pointer}button.active{font-weight:bold;text-decoration:underline}</style></head><body>";
  f += "<p><a href='/'>Home</a></p>";
  f += "<h2>Connect to Wi-Fi</h2>";
  f += "<form method='POST' action='/wifichz'>";
  for (int i = 0; i < n; i++) {
    f += "<input type='radio' name='ssid' value='" + WiFi.SSID(i) + "'";
    if (i == 0) f += " checked";
    f += "> " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)<br>";
  }
  f += "<br>Password: <input type='password' name='key'><br><br>";
  f += "<input type='submit' value='Connect'></form>";
  f += "<p><a href='/wifi'>Rescan</a> | <a href='/'>Home</a></p></body></html>";
  WiFi.scanDelete();
}

String ip2str(IPAddress addr) {
  return String(addr[0]) + "." + String(addr[1]) + "." +
         String(addr[2]) + "." + String(addr[3]);
}

String wifiStatusStr() {
  switch (WiFi.status()) {
    case WL_CONNECTED:       return "Connected";
    case WL_NO_SSID_AVAIL:   return "No SSID available";
    case WL_CONNECT_FAILED:  return "Connection failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED:    return "Disconnected";
    default:                 return "Unknown";
  }
}

// ================================================================
// Flask integration – POST sensor data, receive pattern updates
// ================================================================

// Parse {"pattern": N} out of a Flask JSON response and apply it
void applyPatternFromJson(String json) {
  int idx = json.indexOf("\"pattern\":");
  if (idx < 0) return;
  int newPattern = json.substring(idx + 10).toInt();
  if (newPattern == currentPattern) return;
  if (newPattern < 0 || newPattern >= NUM_PATTERNS) return;

  // Only detach PWM when leaving a PWM pattern (Pulse=3, Temp=4)
  if (currentPattern == 3 || currentPattern == 4) {
    analogWrite(LED1, 0); analogWrite(LED2, 0); analogWrite(LED3, 0);
    ledcDetachPin(LED1); ledcDetachPin(LED2); ledcDetachPin(LED3);
  }

  currentPattern  = newPattern;
  patternTimer    = millis();
  blinkState      = 0;
  chaseStep       = 0;
  pulseBrightness = 0;
  pulseDirection  = 1;
  digitalWrite(LED1, LOW); digitalWrite(LED2, LOW); digitalWrite(LED3, LOW);

  Serial.print("Pattern set by Flask: ");
  Serial.println(patternNames[currentPattern]);
}

// POST temperature to Flask every POST_INTERVAL ms.
// Flask responds with the current pattern so the ESP32 stays in sync
// with whatever the browser has selected.
void postSensorData() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastPost < POST_INTERVAL) return;
  lastPost = millis();

  // Read TMP36 (same calculation as hndlSensorData)
  int   raw         = analogRead(TMP36_PIN);
  float voltage     = raw * (3.3f / 4095.0f);
  float temperature = (voltage - 0.5f) * 100.0f;
  lastTemperature   = temperature;

  HTTPClient http;
  http.begin(String(FLASK_SERVER) + "/data");
  http.addHeader("Content-Type", "application/json");

  String body = "{\"temperature\":" + String(temperature, 1) + "}";
  int code = http.POST(body);

  if (code == 200) {
    applyPatternFromJson(http.getString());
  } else {
    Serial.printf("Flask POST failed, HTTP %d\n", code);
  }
  http.end();
}

// ================================================================
// Non-blocking LED pattern update (runs in loop)
// ================================================================
void updateLEDPattern() {
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - patternTimer;

  switch (currentPattern) {
    case 0: // Off
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);
      break;

    case 1: // Blink - toggle LED1 every 200ms

      if (elapsed >= 200) {
        blinkState = 1 - blinkState;
        digitalWrite(LED1, blinkState ? HIGH : LOW);
        digitalWrite(LED2, blinkState ? HIGH : LOW);
        digitalWrite(LED3, blinkState ? HIGH : LOW);
        patternTimer = currentTime;
      }
      break;

    case 2: // Chase - rotate through LEDs
      if (elapsed >= 150) {
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, LOW);
        
        int ledPin;
        if (chaseStep == 0) ledPin = LED1;
        else if (chaseStep == 1) ledPin = LED2;
        else ledPin = LED3;
        
        digitalWrite(ledPin, HIGH);
        chaseStep = (chaseStep + 1) % 3;
        patternTimer = currentTime;
      }
      break;

    case 3: // Pulse - fade in/out
      if (elapsed >= 15) {
        analogWrite(LED1, pulseBrightness);
        analogWrite(LED2, pulseBrightness);
        analogWrite(LED3, pulseBrightness);
        
        if (pulseDirection == 1) {
          pulseBrightness += 5;
          if (pulseBrightness >= 255) {
            pulseBrightness = 255;
            pulseDirection = -1;
          }
        } else {
          pulseBrightness -= 5;
          if (pulseBrightness <= 0) {
            pulseBrightness = 0;
            pulseDirection = 1;
          }
        }
        patternTimer = currentTime;
      }
      break;

    case 4: // Temp Sensitive - colour-coded thresholds
      // < 20°C  → Green solid (cool)
      // 20–23°C → Orange slow blink (normal)
      // > 23°C  → Red fast blink (warm)
      if (lastTemperature < 20.0) {
        // Cool – green solid, others off
        digitalWrite(LED_RED,    LOW);
        digitalWrite(LED_ORANGE, LOW);
        digitalWrite(LED_GREEN,  HIGH);
      } else if (lastTemperature <= 23.0) {
        // Normal – orange slow blink (600 ms), others off
        if (elapsed >= 600) {
          blinkState = 1 - blinkState;
          digitalWrite(LED_RED,    LOW);
          digitalWrite(LED_ORANGE, blinkState ? HIGH : LOW);
          digitalWrite(LED_GREEN,  LOW);
          patternTimer = currentTime;
        }
      } else {
        // Warm – red fast blink (200 ms), others off
        if (elapsed >= 200) {
          blinkState = 1 - blinkState;
          digitalWrite(LED_RED,    blinkState ? HIGH : LOW);
          digitalWrite(LED_ORANGE, LOW);
          digitalWrite(LED_GREEN,  LOW);
          patternTimer = currentTime;
        }
      }
      break;
  }
}