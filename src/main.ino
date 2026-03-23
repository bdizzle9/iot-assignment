// Ex09.cpp
// COM3505 – Exercise 09
// Exercise 09: ESP32 Wi-Fi Provisioning using AP mode
// Added: Live dashboard with AJAX sensor readings and LED pattern controls

#include <WiFi.h>
#include <WebServer.h>

// Access Point credentials
const char* apSSID     = "ESP32-Setup";
const char* apPassword = "12345678";

// LED and Sensor pins
const int TMP36_PIN = 9;

const int LED1 =10;
const int LED2 =5;
const int LED3 =6;


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
  updateLEDPattern();
  delay(10);  // Small delay to prevent overwhelming the board
}

// ================================================================
// Root / Home page
// ================================================================
void hndlRoot() {
  String wifiStatus = wifiStatusStr();
  String localIP    = (WiFi.status() == WL_CONNECTED)
                      ? ip2str(WiFi.localIP())
                      : "Not connected";

  String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Home</title>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
    body { background:#f0f4f8; color:#333; font-family:sans-serif; margin:0; padding:20px; }
    h1   { color:#0056b3; border-bottom:2px solid #0056b3; padding-bottom:8px; }
    .card {
      background:#fff; border-radius:10px; padding:16px 20px;
      margin:14px 0; box-shadow:0 2px 6px rgba(0,0,0,0.1);
    }
    .card h3 { margin:0 0 8px; color:#0056b3; }
    .badge {
      display:inline-block; padding:3px 10px; border-radius:12px;
      font-size:0.85em; font-weight:bold;
    }
    .connected    { background:#d4edda; color:#155724; }
    .disconnected { background:#f8d7da; color:#721c24; }
    a.btn {
      display:inline-block; margin-top:8px; padding:7px 16px;
      background:#0056b3; color:#fff; border-radius:6px;
      text-decoration:none; font-size:0.95em;
    }
    a.btn:hover { background:#003d80; }
    a.btn.green { background:#28a745; }
    a.btn.green:hover { background:#1e7e34; }
  </style>
</head>
<body>
  <h1>ESP32 IoT Dashboard</h1>

  <div class='card'>
    <h3>&#x1F4F6; Wi-Fi Status</h3>
    <p>Status: <span class='badge )rawhtml";

  html += (WiFi.status() == WL_CONNECTED) ? "connected'>Connected" : "disconnected'>Disconnected";

  html += R"rawhtml('</span></p>
    <p>SSID: <strong>)rawhtml";
  html += (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "—";
  html += R"rawhtml(</strong></p>
    <p>Local IP: <strong>)rawhtml";
  html += localIP;
  html += R"rawhtml(</strong></p>
    <p>AP IP: <strong>)rawhtml";
  html += ip2str(WiFi.softAPIP());
  html += R"rawhtml(</strong></p>
    <a class='btn' href='/wifi'>&#x1F517; Connect to Wi-Fi</a>
    <a class='btn' href='/status'>&#x2139;&#xFE0F; Full Status</a>
  </div>

  <div class='card'>
    <h3>&#x1F4CA; Live Sensor Dashboard</h3>
    <p>View live sensor readings, LED patterns, and controls.</p>
    <a class='btn green' href='/dashboard'>Open Dashboard</a>
  </div>
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
  <style>
    * { box-sizing:border-box; }
    body {
      background:#0d1117; color:#e6edf3;
      font-family:'Segoe UI',sans-serif; margin:0; padding:20px;
    }
    h1 { color:#58a6ff; border-bottom:1px solid #30363d; padding-bottom:10px; }
    .grid {
      display:grid; grid-template-columns:repeat(auto-fit, minmax(200px,1fr));
      gap:16px; margin-bottom:24px;
    }
    .card {
      background:#161b22; border:1px solid #30363d;
      border-radius:10px; padding:18px; text-align:center;
    }
    .card .label { font-size:0.8em; color:#8b949e; text-transform:uppercase; letter-spacing:1px; }
    .card .value { font-size:2.2em; font-weight:bold; color:#58a6ff; margin:8px 0; }
    .card .unit  { font-size:0.85em; color:#8b949e; }

    .section-title { color:#8b949e; font-size:0.9em; text-transform:uppercase;
                     letter-spacing:1px; margin:20px 0 10px; }

    .led-box {
      background:#161b22; border:1px solid #30363d;
      border-radius:10px; padding:18px;
    }
    .led-indicator {
      display:flex; align-items:center; gap:12px;
      font-size:1.1em; margin-bottom:14px;
    }
    .dot {
      width:18px; height:18px; border-radius:50%;
      background:#238636; box-shadow:0 0 8px #238636;
      animation:pulse 1s infinite alternate;
    }
    @keyframes pulse { from{opacity:1} to{opacity:0.3} }
    .dot.off { background:#30363d; box-shadow:none; animation:none; }

    .pattern-grid {
      display:grid; grid-template-columns:repeat(2,1fr); gap:10px;
    }
    .pattern-btn {
      padding:10px; border:1px solid #30363d; border-radius:8px;
      background:#21262d; color:#e6edf3; font-size:0.95em;
      cursor:pointer; transition:all 0.2s;
    }
    .pattern-btn:hover   { background:#30363d; border-color:#58a6ff; }
    .pattern-btn.active  { background:#0d419d; border-color:#58a6ff; color:#fff; }

    .status-bar {
      font-size:0.8em; color:#8b949e; margin-top:20px;
      padding:8px 12px; background:#161b22;
      border-radius:6px; border:1px solid #30363d;
    }
    .dot-live { display:inline-block; width:8px; height:8px;
                border-radius:50%; background:#238636;
                animation:pulse 1s infinite alternate; margin-right:6px; }

    a.back { color:#58a6ff; text-decoration:none; font-size:0.9em; }
    a.back:hover { text-decoration:underline; }
  </style>
</head>
<body>
  <a class='back' href='/'>&#8592; Home</a>
  <h1>&#x1F4CA; Live Dashboard</h1>

  <!-- Sensor readings -->
  <div class='section-title'>Sensor Readings</div>
  <div class='grid'>
    <div class='card'>
      <div class='label'>Temperature</div>
      <div class='value' id='temp'>--</div>
      <div class='unit'>°C</div>
    </div>
  </div>

  <!-- Temperature history graph -->
  <div class='section-title'>Temperature History (Last Minute)</div>
  <div class='card'>
    <canvas id='tempChart' width='600' height='240'></canvas>
  </div>

  <!-- LED Pattern -->
  <div class='section-title'>LED Pattern</div>
  <div class='led-box'>
    <div class='led-indicator'>
      <div class='dot' id='ledDot'></div>
      <span>Current pattern: <strong id='patternName'>--</strong></span>
    </div>
    <div class='pattern-grid'>
      <button class='pattern-btn' onclick='setPattern(0)'>&#x26AB; Off</button>
      <button class='pattern-btn' onclick='setPattern(1)'>&#x1F4A1; Blink</button>
      <button class='pattern-btn' onclick='setPattern(2)'>&#x27A1;&#xFE0F; Chase</button>
      <button class='pattern-btn' onclick='setPattern(3)'>&#x1F7E3; Pulse</button>
      <button class='pattern-btn' onclick='setPattern(4)'>&#x1F321; Temp</button>
    </div>
  </div>

  <div class='status-bar'>
    <span class='dot-live'></span>
    Live – updating every 2 seconds &nbsp;|&nbsp; Last update: <span id='lastUpdate'>--</span>
  </div>

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
      // Detach PWM from all LED pins to restore digital behavior (ESP32)
      ledcDetachPin(LED1);
      ledcDetachPin(LED2);
      ledcDetachPin(LED3);

      // Disable PWM output from Temp Sensitive mode if active
      if (currentPattern == 4) {
        analogWrite(LED1, 0);
        analogWrite(LED2, 0);
        analogWrite(LED3, 0);
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
  webServer.send(200, "text/html",
    msg + "<p><a href='/status'>Check status</a> | <a href='/'>Home</a></p>");
}

// ================================================================
// Status page
// ================================================================
void hndlStatus() {
  String s = "<h2>Wi-Fi Status</h2><ul>";
  s += "<li>SSID: " + WiFi.SSID() + "</li>";
  s += "<li>Status: " + wifiStatusStr() + "</li>";
  s += "<li>Local IP: "  + ip2str(WiFi.localIP())   + "</li>";
  s += "<li>Soft AP IP: " + ip2str(WiFi.softAPIP()) + "</li>";
  s += "<li>AP SSID: "   + String(apSSID)           + "</li></ul>";
  s += "<p><a href='/wifi'>Choose Wi-Fi</a> | <a href='/'>Home</a></p>";
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
  f  = "<h2>Available Wi-Fi Networks</h2>";
  f += "<form method='POST' action='/wifichz'>";
  for (int i = 0; i < n; i++) {
    f += "<input type='radio' name='ssid' value='" + WiFi.SSID(i) + "'";
    if (i == 0) f += " checked";
    f += "> " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)<br>";
  }
  f += "<br>Password: <input type='password' name='key'><br><br>";
  f += "<input type='submit' value='Connect'></form>";
  f += "<a href='/wifi'>Rescan</a> | <a href='/'>Home</a>";
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
      if (elapsed >= 30) {
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

    case 4: // Temp Sensitive - brightness by temperature
      if (elapsed >= 100) {
        float tempClamped = constrain(lastTemperature, 0.0, 50.0);
        int brightness = map((int)(tempClamped * 10), 0, 500, 0, 255);
        analogWrite(LED1, brightness);
        analogWrite(LED2, brightness);
        analogWrite(LED3, brightness);
        patternTimer = currentTime;
      }
      break;
  }
}