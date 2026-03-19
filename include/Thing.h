// Thing.h
// COM3505 lab exercise runner declarations
//
// This header file declares shared variables and helper functions
// that are used across multiple exercises (e.g. Ex01, Ex02).
// Keeping these declarations here allows different .cpp files
// to access common functionality without duplicating code.

#ifndef THING_H
#define THING_H

#include <Arduino.h>  // Core Arduino definitions (pinMode, Serial, etc.)

// function prototypes, needed by newer Arduino IDEs (and IDF)
void setup06(); void loop06();

// ---------------------------------------------------------------------------
// Debug flags
// These can be used to enable or disable debug output at runtime.
// Declared as extern so they can be defined in exactly one .cpp file
// and accessed from others.
// ---------------------------------------------------------------------------

extern bool startupDBG;   // enable debug messages during setup()
extern bool netDBG;       // enable debug messages related to networking

// ---------------------------------------------------------------------------
// Loop tracking
// A counter that can be incremented each time loop() runs.
// Useful for debugging timing and behaviour over time.
// ---------------------------------------------------------------------------

//extern unsigned long loopIteration;

// ---------------------------------------------------------------------------
// MAC address utilities (Ex02)
// The MAC address uniquely identifies the ESP32 device.
// We store it as a string for easy printing and logging.
// ---------------------------------------------------------------------------

//extern char MAC_ADDRESS[];   // buffer holding the device MAC address
//void getMAC(char *);         // read MAC from hardware and format as string

// ---------------------------------------------------------------------------
// LED utility functions
// Simple helper functions for controlling LEDs.
// These abstract away the digitalWrite calls and make code clearer.
// Default parameters are provided for blink() so it can be called
// with no arguments (or just one).
// ---------------------------------------------------------------------------

void ledOn();                          // turn built-in LED on
void ledOff();                         // turn built-in LED off
void blink(int = 1, int = 300);        // blink LED (times, delay in ms)

// declarations for Ex04

// debugging infrastructure; setting different DBGs true triggers prints ////
#define dbg(b, s) if(b) Serial.print(s)
#define dln(b, s) if(b) Serial.println(s)
#define startupDBG      true
#define loopDBG         true
#define monitorDBG      true
#define netDBG          true
#define miscDBG         true
#define analogDBG       true
#define otaDBG          true

// declarations for Ex05

// what time did we start?
extern unsigned long firstSliceMillis;

// what time did we last run this action?
extern unsigned long lastSliceMillis;

// a control iterator for slicing up the main loop ///////////////////////////
extern unsigned long loopIteration;
extern const int LOOP_ROLLOVER; // how many loops per action sequence

// declarations for Ex06

// the wifi and HTTP server libraries ////////////////////////////////////////
#include <WiFi.h>
#include <WebServer.h>

// globals for a wifi access point and webserver /////////////////////////////
extern String apSSID;           // SSID of the AP
extern WebServer webServer;     // a simple web server

// function protos
void setup06();
void loop06();
void startAP();
void printIPs();
void startWebServer();
void handleNotFound();
void handleRoot();
void handleHello();

// ---------------------------------------------------------------------------
// End of header guard
// ---------------------------------------------------------------------------

#endif