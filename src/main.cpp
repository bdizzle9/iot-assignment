// Ex09.cpp
// COM3505 – Exercise 09
// Exercise 09: ESP32 Wi-Fi Provisioning using AP mode
// Objective:
//This program demonstrates how an ESP32 device can act as a
//web client. The ESP32 connects to a Wi-Fi network, sends an
//HTTP GET request to a server containing the user's email
//and the device MAC address, then prints the server response.

//The ESP32 also runs a small local web server so a browser
//can confirm the device is working.


//Main Features
//1. Connect to Wi-Fi
//2. Connect to remote server
//3. Send GET request with email + MAC
//4. Receive and print server response
//5. Run a local web page
#include <WiFi.h>
#include <WebServer.h>

// Access Point credentials
const char* apSSID = "ESP32-Setup";
const char* apPassword = "12345678";


// Web server
WebServer webServer(80);

// HTML template
const char *templatePage[] = {
  "<html><head><title>", "COM3505 IoT Setup", "</title>\n",
  "<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>\n",
  "<style>body{background:#f5f5f5;color:#333;font-family:sans-serif;font-size:1.2em;}"
  "h2{color:#0056b3;} form{background:#fff;padding:10px;border-radius:8px;margin:10px 0;}"
  "input[type='submit']{background:#0056b3;color:#fff;padding:5px 10px;border:none;border-radius:5px;}"
  "input[type='password']{padding:3px;width:200px;}"
  "</style>\n",
  "</head><body>\n", "<h2>Welcome to ESP32 IoT Setup!</h2>\n",
  "<!-- page payload -->\n", "<p><a href='/'>Home</a>&nbsp;&nbsp;&nbsp;</p>\n",
  "</body></html>\n"
};

// Forward declarations
void hndlRoot();
void hndlWifi();
void hndlWifichz();
void hndlStatus();
void apListForm(String &f);
String ip2str(IPAddress addr);

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Starting Ex09...");

  // Start Access Point
  WiFi.softAP(apSSID, apPassword);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  // Setup web server routes
  webServer.on("/", hndlRoot);
  webServer.on("/wifi", hndlWifi);
  webServer.on("/wifichz", hndlWifichz);
  webServer.on("/status", hndlStatus);
  webServer.onNotFound([](){ webServer.send(404, "text/plain", "Page not found"); });

  webServer.begin();
  Serial.println("HTTP server started");
}

void loop() {
  webServer.handleClient();
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

}

// Root page
void hndlRoot() {
  String html = "<p>Choose a <a href=\"/wifi\">Wi-Fi network</a> to connect.</p>";
  html += "<p>Check <a href='/status'>Wi-Fi status</a>.</p>";
  webServer.send(200, "text/html", html);
}

// Wi-Fi selection page
void hndlWifi() {
  String form = "";
  apListForm(form);
  webServer.send(200, "text/html", form);
}

// Handle Wi-Fi form submission
void hndlWifichz() {
  String ssid = webServer.arg("ssid");
  String key  = webServer.arg("key");
  String msg;

  if(ssid == "") {
    msg = "<h2>Error: No SSID selected!</h2>";
  } else {
    char ssidchars[ssid.length()+1];
    char keychars[key.length()+1];
    ssid.toCharArray(ssidchars, ssid.length()+1);
    key.toCharArray(keychars, key.length()+1);

    WiFi.begin(ssidchars, keychars);

    // Wait for connection (timeout 20s)
    unsigned long start = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(500);
      Serial.print(".");
    }

    if(WiFi.status() == WL_CONNECTED) {
      msg = "<h2>Connected successfully!</h2><p>IP: " + ip2str(WiFi.localIP()) + "</p>";
    } else {
      msg = "<h2>Connection failed.</h2><p>Try again.</p>";
    }
  }
  webServer.send(200, "text/html", msg + "<p><a href='/status'>Check status</a></p>");
}

// Status page
void hndlStatus() {
  String s = "<h2>Wi-Fi Status</h2><ul>";
  s += "<li>SSID: " + WiFi.SSID() + "</li>";
  switch(WiFi.status()) {
    case WL_CONNECTED: s += "<li>Status: Connected</li>"; break;
    case WL_NO_SSID_AVAIL: s += "<li>Status: No SSID available</li>"; break;
    case WL_CONNECT_FAILED: s += "<li>Status: Connection failed</li>"; break;
    case WL_CONNECTION_LOST: s += "<li>Status: Connection lost</li>"; break;
    case WL_DISCONNECTED: s += "<li>Status: Disconnected</li>"; break;
    default: s += "<li>Status: Unknown</li>"; break;
  }
  s += "<li>Local IP: " + ip2str(WiFi.localIP()) + "</li>";
  s += "<li>Soft AP IP: " + ip2str(WiFi.softAPIP()) + "</li>";
  s += "<li>AP SSID: " + String(apSSID) + "</li></ul>";
  webServer.send(200, "text/html", s + "<p><a href='/wifi'>Choose Wi-Fi</a></p>");
}

// Generate Wi-Fi form
void apListForm(String &f) {
  int n = WiFi.scanNetworks();
  if(n == 0) {
    f = "<p>No Wi-Fi networks found. <a href='/'>Back</a></p>";
    return;
  }
  f = "<h2>Available Wi-Fi Networks</h2><form method='POST' action='/wifichz'>";
  for(int i=0;i<n;i++){
    f += "<input type='radio' name='ssid' value='" + WiFi.SSID(i) + "'";
    if(i==0) f += " checked";
    f += "> " + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + " dBm)<br>";
  }
  f += "<br>Password: <input type='password' name='key'><br><br>";
  f += "<input type='submit' value='Connect'></form>";
}

// Convert IPAddress to string
String ip2str(IPAddress addr) {
  return String(addr[0]) + "." + String(addr[1]) + "." + String(addr[2]) + "." + String(addr[3]);
}