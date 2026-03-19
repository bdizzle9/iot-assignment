// Ex09.cpp
// COM3505 – Exercise 09
// Exercise 09: ESP32 Wi-Fi Provisioning using AP mode
// Added: Live dashboard with AJAX sensor readings and LED pattern controls

#include <WiFi.h>
#include <WebServer.h>

// Access Point credentials
const char* apSSID     = "ESP32-Setup";
const char* apPassword = "12345678";

// Web server
WebServer webServer(80);

// ---------- Simulated state ----------
int   currentPattern = 0;                          // 0=Off 1=Blink 2=Chase 3=Pulse
const char* patternNames[] = { "Off", "Blink", "Chase", "Pulse" };
const int   NUM_PATTERNS   = 4;

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

// ================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting Ex09...");

  randomSeed(analogRead(0));   // Seed RNG from floating pin

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID, apPassword);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

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
    <div class='card'>
      <div class='label'>Humidity</div>
      <div class='value' id='hum'>--</div>
      <div class='unit'>%</div>
    </div>
    <div class='card'>
      <div class='label'>Light Level</div>
      <div class='value' id='light'>--</div>
      <div class='unit'>lux</div>
    </div>
    <div class='card'>
      <div class='label'>Pressure</div>
      <div class='value' id='pres'>--</div>
      <div class='unit'>hPa</div>
    </div>
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
          document.getElementById('temp').textContent  = d.temperature;
          document.getElementById('hum').textContent   = d.humidity;
          document.getElementById('light').textContent = d.light;
          document.getElementById('pres').textContent  = d.pressure;

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

    fetchData();                    // Load immediately on page open
    setInterval(fetchData, 2000);   // Then every 2 seconds
  </script>
</body>
</html>
)rawhtml";

  webServer.send(200, "text/html", html);
}

// ================================================================
// AJAX endpoint – returns JSON with random sensor data
// ================================================================
void hndlSensorData() {
  // Simulated random sensor values
  float temperature = 18.0 + (random(0, 140) / 10.0);   // 18.0 – 32.0 °C
  float humidity    = 30.0 + (random(0, 600) / 10.0);   // 30.0 – 90.0 %
  int   light       = random(100, 1000);                  // 100 – 999 lux
  int   pressure    = random(990, 1030);                  // 990 – 1029 hPa

  String json = "{";
  json += "\"temperature\":"  + String(temperature, 1) + ",";
  json += "\"humidity\":"     + String(humidity, 1)    + ",";
  json += "\"light\":"        + String(light)          + ",";
  json += "\"pressure\":"     + String(pressure)       + ",";
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
      currentPattern = id;
      Serial.print("Pattern changed to: ");
      Serial.println(patternNames[currentPattern]);
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
  int n = WiFi.scanNetworks();
  if (n == 0) {
    f = "<p>No networks found. <a href='/'>Back</a></p>";
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
  f += "<p><a href='/'>Home</a></p>";
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