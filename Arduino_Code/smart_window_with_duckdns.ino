/*
 * ================================================
 *        النافذة الذكية الاحترافية v2.2
 *        Smart Window Professional System
 * ================================================
 * التحديثات:
 * ✅ إضافة نظام SPIFFS لتحميل الملفات
 * ✅ دعم صفحة الويب من الذاكرة الداخلية
 * ✅ جميع التحسينات السابقة
 * ================================================
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Servo.h>
#include <EEPROM.h>
#include <WebSocketsServer.h>
#include <FS.h>  // ✅ نظام الملفات

// ============ إعدادات WiFi ============
const char* ssid = "mimo";
const char* password = "1212000088";

// ============ تعريف المنافذ ============
#define DHT11_PIN D4
#define DHT_TYPE DHT11
#define RAIN_SENSOR D5
#define LDR_PIN A0
#define WINDOW_SERVO D6
#define BLIND_SERVO D7

// ============ الكائنات ============
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
DHT dht(DHT11_PIN, DHT_TYPE);
Servo windowServo;
Servo blindServo;

// ============ هياكل البيانات ============
struct Settings {
  int lightThreshold;
  int tempThreshold;
  bool autoMode;
  bool autoWindow;
  bool autoBlinds;
  int windowCalibration;
  int blindCalibration;
  char deviceName[32];
} settings;

struct Schedule {
  bool enabled;
  int hour;
  int minute;
  int windowPos;
  int blindPos;
};
Schedule schedules[5];

struct Scene {
  char name[32];
  int windowPos;
  int blindPos;
  bool enabled;
};
Scene scenes[5];

struct LogEntry {
  unsigned long timestamp;
  char event[64];
  char level[10];
};
LogEntry logs[50];
int logIndex = 0;

// ============ المتغيرات ============
float temperature = 0;
float humidity = 0;
int lightLevel = 0;
bool isRaining = false;
int windowPosition = 0;
int blindPosition = 0;

float tempHistory[24] = {0};
float humidityHistory[24] = {0};
int lightHistory[24] = {0};
int historyIndex = 0;
unsigned long lastHistoryUpdate = 0;

unsigned long startTime = 0;
int windowMovements = 0;
int blindMovements = 0;
int rainDetections = 0;

// ============ دوال مساعدة ============
void addLog(const char* event, const char* level = "INFO") {
  logs[logIndex].timestamp = millis();
  strncpy(logs[logIndex].event, event, 63);
  strncpy(logs[logIndex].level, level, 9);
  logIndex = (logIndex + 1) % 50;
  
  Serial.print("[");
  Serial.print(level);
  Serial.print("] ");
  Serial.println(event);
  
  DynamicJsonDocument doc(256);
  doc["type"] = "log";
  doc["timestamp"] = millis() / 1000;
  doc["event"] = event;
  doc["level"] = level;
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.put(sizeof(settings), schedules);
  EEPROM.put(sizeof(settings) + sizeof(schedules), scenes);
  EEPROM.commit();
  addLog("Settings saved");
}

void loadSettings() {
  EEPROM.get(0, settings);
  
  if (settings.lightThreshold < 0 || settings.lightThreshold > 1024) {
    settings.lightThreshold = 500;
    settings.tempThreshold = 28;
    settings.autoMode = true;
    settings.autoWindow = true;
    settings.autoBlinds = true;
    settings.windowCalibration = 0;
    settings.blindCalibration = 0;
    strcpy(settings.deviceName, "SmartWindow");
    
    for(int i = 0; i < 5; i++) {
      schedules[i].enabled = false;
      schedules[i].hour = 0;
      schedules[i].minute = 0;
      schedules[i].windowPos = 0;
      schedules[i].blindPos = 0;
    }
    
    strcpy(scenes[0].name, "صباح");
    scenes[0].windowPos = 50;
    scenes[0].blindPos = 100;
    scenes[0].enabled = true;
    
    strcpy(scenes[1].name, "نهار");
    scenes[1].windowPos = 30;
    scenes[1].blindPos = 50;
    scenes[1].enabled = true;
    
    strcpy(scenes[2].name, "مساء");
    scenes[2].windowPos = 0;
    scenes[2].blindPos = 0;
    scenes[2].enabled = true;
    
    strcpy(scenes[3].name, "نوم");
    scenes[3].windowPos = 0;
    scenes[3].blindPos = 0;
    scenes[3].enabled = true;
    
    strcpy(scenes[4].name, "تهوية");
    scenes[4].windowPos = 100;
    scenes[4].blindPos = 100;
    scenes[4].enabled = true;
    
    saveSettings();
    addLog("Default settings loaded");
  } else {
    EEPROM.get(sizeof(settings), schedules);
    EEPROM.get(sizeof(settings) + sizeof(schedules), scenes);
    addLog("Settings loaded from EEPROM");
  }
}

void setWindowPosition(int pos) {
  pos = constrain(pos, 0, 100);
  
  if (pos != windowPosition) {
    int angle = map(pos, 0, 100, 0 + settings.windowCalibration, 
                                 180 + settings.windowCalibration);
    angle = constrain(angle, 0, 180);
    
    int currentAngle = windowServo.read();
    int step = (angle > currentAngle) ? 1 : -1;
    
    for(int i = currentAngle; i != angle; i += step) {
      windowServo.write(i);
      delay(15);
    }
    
    windowPosition = pos;
    windowMovements++;
    
    char msg[64];
    sprintf(msg, "Window moved to %d%%", pos);
    addLog(msg);
  }
}

void setBlindPosition(int pos) {
  pos = constrain(pos, 0, 100);
  
  if (pos != blindPosition) {
    int angle = map(pos, 0, 100, 0 + settings.blindCalibration, 
                                 180 + settings.blindCalibration);
    angle = constrain(angle, 0, 180);
    
    int currentAngle = blindServo.read();
    int step = (angle > currentAngle) ? 1 : -1;
    
    for(int i = currentAngle; i != angle; i += step) {
      blindServo.write(i);
      delay(15);
    }
    
    blindPosition = pos;
    blindMovements++;
    
    char msg[64];
    sprintf(msg, "Blind moved to %d%%", pos);
    addLog(msg);
  }
}

// ============ 🆕 دالة تحميل الملفات من SPIFFS ============
void handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  
  String contentType = "text/plain";
  if (path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";
  else if (path.endsWith(".json")) contentType = "application/json";
  else if (path.endsWith(".png")) contentType = "image/png";
  else if (path.endsWith(".jpg")) contentType = "image/jpeg";
  else if (path.endsWith(".ico")) contentType = "image/x-icon";
  
  if (SPIFFS.exists(path)) {
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
  } else {
    server.send(404, "text/plain", "File Not Found: " + path);
  }
}

// ============ الإعداد ============
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n========================================");
  Serial.println("   النافذة الذكية الاحترافية v2.2");
  Serial.println("========================================\n");
  
  startTime = millis();
  
  // ✅ تهيئة نظام الملفات SPIFFS
  Serial.print("Initializing SPIFFS... ");
  if (SPIFFS.begin()) {
    Serial.println("✓ SPIFFS mounted successfully!");
    addLog("SPIFFS initialized");
    
    // عرض الملفات الموجودة
    Dir dir = SPIFFS.openDir("/");
    Serial.println("Files in SPIFFS:");
    while (dir.next()) {
      Serial.print("  - ");
      Serial.print(dir.fileName());
      Serial.print(" (");
      Serial.print(dir.fileSize());
      Serial.println(" bytes)");
    }
  } else {
    Serial.println("✗ SPIFFS mount failed!");
    addLog("SPIFFS mount failed", "ERROR");
  }
  
  EEPROM.begin(1024);
  loadSettings();
  
  dht.begin();
  pinMode(RAIN_SENSOR, INPUT);
  
  windowServo.attach(WINDOW_SERVO);
  blindServo.attach(BLIND_SERVO);
  setWindowPosition(0);
  setBlindPosition(0);
  
  addLog("Hardware initialized");
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("✓ IP: ");
    Serial.println(WiFi.localIP());
    addLog(("WiFi connected: " + WiFi.localIP().toString()).c_str());
  } else {
    Serial.println("\n✗ WiFi failed!");
    addLog("WiFi connection failed", "ERROR");
  }
  
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  addLog("WebSocket started on port 81");
  
  setupRoutes();
  server.begin();
  addLog("HTTP server started on port 80");
  
  Serial.println("✓ System Ready!");
  Serial.println("========================================\n");
}

// ============ الحلقة الرئيسية ============
void loop() {
  server.handleClient();
  webSocket.loop();
  
  static unsigned long lastRead = 0;
  if (millis() - lastRead > 2000) {
    readSensors();
    lastRead = millis();
  }
  
  if (settings.autoMode) {
    automaticControl();
  }
  
  static unsigned long lastScheduleCheck = 0;
  if (millis() - lastScheduleCheck > 60000) {
    checkSchedules();
    lastScheduleCheck = millis();
  }
  
  if (millis() - lastHistoryUpdate > 3600000) {
    tempHistory[historyIndex] = temperature;
    humidityHistory[historyIndex] = humidity;
    lightHistory[historyIndex] = lightLevel;
    historyIndex = (historyIndex + 1) % 24;
    lastHistoryUpdate = millis();
  }
}

void readSensors() {
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  lightLevel = analogRead(LDR_PIN);
  bool wasRaining = isRaining;
  isRaining = (digitalRead(RAIN_SENSOR) == LOW);
  
  if (isnan(temperature)) temperature = 0;
  if (isnan(humidity)) humidity = 0;
  
  if (isRaining && !wasRaining) {
    rainDetections++;
    addLog("Rain detected!", "WARNING");
  }
  
  DynamicJsonDocument doc(256);
  doc["type"] = "sensors";
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["lightLevel"] = lightLevel;
  doc["isRaining"] = isRaining;
  doc["windowPosition"] = windowPosition;
  doc["blindPosition"] = blindPosition;
  
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

void automaticControl() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 10000) return;
  lastCheck = millis();
  
  if (settings.autoWindow && isRaining && windowPosition > 0) {
    addLog("Auto: Closing window (rain)", "WARNING");
    setWindowPosition(0);
  }
  else if (settings.autoWindow && !isRaining && 
           temperature > settings.tempThreshold && windowPosition < 50) {
    addLog("Auto: Opening window (high temp)");
    setWindowPosition(50);
  }
  else if (settings.autoWindow && temperature < (settings.tempThreshold - 3) && 
           windowPosition > 0 && !isRaining) {
    addLog("Auto: Closing window (low temp)");
    setWindowPosition(0);
  }
  
  if (settings.autoBlinds) {
    if (lightLevel > settings.lightThreshold && blindPosition < 100) {
      addLog("Auto: Opening blinds (high light)");
      setBlindPosition(100);
    }
    else if (lightLevel < (settings.lightThreshold - 100) && blindPosition > 0) {
      addLog("Auto: Closing blinds (low light)");
      setBlindPosition(0);
    }
  }
}

void checkSchedules() {
  unsigned long uptime = (millis() - startTime) / 1000;
  int currentHour = (uptime / 3600) % 24;
  int currentMinute = (uptime / 60) % 60;
  
  for (int i = 0; i < 5; i++) {
    if (schedules[i].enabled && 
        schedules[i].hour == currentHour && 
        schedules[i].minute == currentMinute) {
      
      setWindowPosition(schedules[i].windowPos);
      setBlindPosition(schedules[i].blindPos);
      
      char msg[64];
      sprintf(msg, "Schedule %d executed", i + 1);
      addLog(msg);
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    addLog(("WebSocket connected: " + ip.toString()).c_str());
  }
  else if (type == WStype_DISCONNECTED) {
    addLog("WebSocket disconnected");
  }
}

// ============ API Routes ============
void setupRoutes() {
  // ✅ معالجة الصفحة الرئيسية وجميع الملفات
  server.on("/", HTTP_GET, []() {
    handleFileRead("/index.html");
  });
  
  // ✅ معالجة جميع الملفات الثابتة
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(200);
    } else if (server.uri().startsWith("/api/")) {
      server.send(404, "application/json", "{\"error\":\"API endpoint not found\"}");
    } else {
      // محاولة تحميل الملف من SPIFFS
      handleFileRead(server.uri());
    }
  });
  
  // API Endpoints
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.on("/api/window", HTTP_POST, handleWindow);
  server.on("/api/blind", HTTP_POST, handleBlinds);
  server.on("/api/auto", HTTP_POST, handleAutoMode);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handleSaveSettings);
  server.on("/api/schedules", HTTP_GET, handleGetSchedules);
  server.on("/api/schedules", HTTP_POST, handleSaveSchedules);
  server.on("/api/scenes", HTTP_GET, handleGetScenes);
  server.on("/api/scenes", HTTP_POST, handleActivateScene);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/stats", HTTP_GET, handleGetStats);
  server.on("/api/history", HTTP_GET, handleGetHistory);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
}

void handleStatus() {
  DynamicJsonDocument doc(512);
  doc["status"] = "online";
  doc["uptime"] = (millis() - startTime) / 1000;
  doc["deviceName"] = settings.deviceName;
  doc["windowPosition"] = windowPosition;
  doc["blindPosition"] = blindPosition;
  doc["autoMode"] = settings.autoMode;
  doc["ip"] = WiFi.localIP().toString();
  
  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleSensors() {
  DynamicJsonDocument doc(256);
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["lightLevel"] = lightLevel;
  doc["isRaining"] = isRaining;
  
  String json;
  serializeJson(doc, json);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleWindow() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(128);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("position")) {
    setWindowPosition(doc["position"]);
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing position\"}");
  }
}

void handleBlinds() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(128);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("position")) {
    setBlindPosition(doc["position"]);
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing position\"}");
  }
}

void handleAutoMode() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("autoMode")) settings.autoMode = doc["autoMode"];
  if (doc.containsKey("autoWindow")) settings.autoWindow = doc["autoWindow"];
  if (doc.containsKey("autoBlinds")) settings.autoBlinds = doc["autoBlinds"];
  
  saveSettings();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleGetSettings() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(512);
  doc["deviceName"] = settings.deviceName;
  doc["lightThreshold"] = settings.lightThreshold;
  doc["tempThreshold"] = settings.tempThreshold;
  doc["autoMode"] = settings.autoMode;
  doc["autoWindow"] = settings.autoWindow;
  doc["autoBlinds"] = settings.autoBlinds;
  doc["windowCalibration"] = settings.windowCalibration;
  doc["blindCalibration"] = settings.blindCalibration;
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSaveSettings() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(512);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("lightThreshold")) settings.lightThreshold = doc["lightThreshold"];
  if (doc.containsKey("tempThreshold")) settings.tempThreshold = doc["tempThreshold"];
  if (doc.containsKey("deviceName")) strncpy(settings.deviceName, doc["deviceName"], 31);
  
  saveSettings();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleGetSchedules() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.createNestedArray("schedules");
  
  for (int i = 0; i < 5; i++) {
    JsonObject obj = array.createNestedObject();
    obj["enabled"] = schedules[i].enabled;
    obj["hour"] = schedules[i].hour;
    obj["minute"] = schedules[i].minute;
    obj["windowPos"] = schedules[i].windowPos;
    obj["blindPos"] = schedules[i].blindPos;
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSaveSchedules() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, server.arg("plain"));
  
  JsonArray array = doc["schedules"];
  for (int i = 0; i < array.size() && i < 5; i++) {
    schedules[i].enabled = array[i]["enabled"];
    schedules[i].hour = array[i]["hour"];
    schedules[i].minute = array[i]["minute"];
    schedules[i].windowPos = array[i]["windowPos"];
    schedules[i].blindPos = array[i]["blindPos"];
  }
  
  saveSettings();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleGetScenes() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(1024);
  JsonArray array = doc.createNestedArray("scenes");
  
  for (int i = 0; i < 5; i++) {
    JsonObject obj = array.createNestedObject();
    obj["name"] = scenes[i].name;
    obj["windowPos"] = scenes[i].windowPos;
    obj["blindPos"] = scenes[i].blindPos;
    obj["enabled"] = scenes[i].enabled;
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleActivateScene() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(128);
  deserializeJson(doc, server.arg("plain"));
  
  int index = doc["index"];
  if (index >= 0 && index < 5 && scenes[index].enabled) {
    setWindowPosition(scenes[index].windowPos);
    setBlindPosition(scenes[index].blindPos);
    
    char msg[64];
    sprintf(msg, "Scene activated: %s", scenes[index].name);
    addLog(msg);
    
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid scene\"}");
  }
}

void handleGetLogs() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.createNestedArray("logs");
  
  for (int i = 0; i < 50; i++) {
    int idx = (logIndex + i) % 50;
    if (logs[idx].timestamp > 0) {
      JsonObject obj = array.createNestedObject();
      obj["timestamp"] = logs[idx].timestamp / 1000;
      obj["event"] = logs[idx].event;
      obj["level"] = logs[idx].level;
    }
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleGetStats() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(512);
  doc["uptime"] = (millis() - startTime) / 1000;
  doc["windowMovements"] = windowMovements;
  doc["blindMovements"] = blindMovements;
  doc["rainDetections"] = rainDetections;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["wifiSignal"] = WiFi.RSSI();
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleGetHistory() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  
  DynamicJsonDocument doc(2048);
  JsonArray tempArray = doc.createNestedArray("temperature");
  JsonArray humArray = doc.createNestedArray("humidity");
  JsonArray lightArray = doc.createNestedArray("light");
  
  for (int i = 0; i < 24; i++) {
    tempArray.add(tempHistory[i]);
    humArray.add(humidityHistory[i]);
    lightArray.add(lightHistory[i]);
  }
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleCalibrate() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }
  
  DynamicJsonDocument doc(256);
  deserializeJson(doc, server.arg("plain"));
  
  if (doc.containsKey("windowCalibration")) {
    settings.windowCalibration = doc["windowCalibration"];
  }
  if (doc.containsKey("blindCalibration")) {
    settings.blindCalibration = doc["blindCalibration"];
  }
  
  saveSettings();
  server.send(200, "application/json", "{\"success\":true}");
  addLog("Calibration updated");
}
