#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ========== PHẦN 1: CẤU HÌNH HỆ THỐNG TƯỚI ==========

// Định nghĩa các chân (PIN)
#define SOIL_SENSOR_PIN   34  // Cảm biến độ ẩm đất (Analog)
#define LIGHT_SENSOR_PIN  35  // Cảm biến ánh sáng LDR (Analog)
#define DHT_PIN           4   // Cảm biến DHT11/22 (Digital)
#define PUMP_RELAY_PIN    5   // Pin điều khiển Relay máy bơm

// Loại cảm biến DHT
#define DHT_TYPE DHT11   // Có thể đổi thành DHT22

// Ngưỡng (Thresholds) - HÃY TỰ HIỆU CHỈNH
const int SOIL_DRY_THRESHOLD = 2500;  // Ngưỡng đất khô (giá trị Analog)
const int LIGHT_DARK_THRESHOLD = 1500; // Ngưỡng trời tối (giá trị Analog)

// Khởi tạo cảm biến DHT
DHT dht(DHT_PIN, DHT_TYPE);

// ========== PHẦN 2: CẤU HÌNH OTA (CẬP NHẬT TỪ XA) ==========

// Định nghĩa phiên bản hiện tại của firmware này
#define CURRENT_VERSION 1.0

// URL trỏ đến tệp version.json trên GitHub của bạn
// VÍ DỤ: "https://raw.githubusercontent.com/user/repo/main/version.json"
const char* version_json_url = "https://raw.githubusercontent.com/Vanh53/esp32-ota-firmware/main/version.json";

// Thông tin Wi-Fi
const char* ssid = "vanhvanh";
const char* password = "vanh123123";

// ========== PHẦN 3: CÁC HÀM OTA ==========

// Hàm thực hiện việc flash firmware
void performUpdate(String url) {
  Serial.println("Starting OTA update...");
  HTTPClient http_update;
  http_update.begin(url); // Dùng URL của tệp .bin

  int httpCode = http_update.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Download firmware failed, error: %s\n", http_update.errorToString(httpCode).c_str());
    http_update.end();
    return;
  }

  int contentLength = http_update.getSize();
  if (!Update.begin(contentLength)) {
    Serial.println("Not enough space for OTA update");
    return;
  }

  WiFiClient* stream = http_update.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  if (written == contentLength) {
    Serial.println("Update written successfully");
  } else {
    Serial.printf("Update failed, written %d of %d\n", written, contentLength);
    return;
  }

  if (Update.end()) {
    Serial.println("Update finished! Rebooting...");
    ESP.restart();
  } else {
    Serial.printf("Update error: %u\n", Update.getError());
  }
}

// Hàm kiểm tra phiên bản trên server
void checkForUpdates() {
  Serial.println("Checking for updates...");
  
  HTTPClient http;
  http.begin(version_json_url); // Dùng URL của tệp .json
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Failed to get version file, error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  // Phân tích tệp JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.println("Failed to parse JSON");
    return;
  }

  float newVersion = doc["latest_version"];
  String firmwareUrl = doc["firmware_url"];

  Serial.printf("Current version: %.1f\n", CURRENT_VERSION);
  Serial.printf("Server version: %.1f\n", newVersion);

  // So sánh phiên bản
  if (newVersion > CURRENT_VERSION) {
    Serial.println("New firmware available.");
    performUpdate(firmwareUrl);
  } else {
    Serial.println("No new update available.");
  }
}

// ========== PHẦN 4: HÀM LOGIC TƯỚI CÂY ==========

void runIrrigationLogic() {
  // 1. Đọc tất cả cảm biến
  int soilMoisture = analogRead(SOIL_SENSOR_PIN);
  int lightLevel = analogRead(LIGHT_SENSOR_PIN);
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // In thông số ra Serial
  Serial.println("--- Reading Sensors ---");
  Serial.printf("Soil Moisture: %d\n", soilMoisture);
  Serial.printf("Light Level: %d\n", lightLevel);
  Serial.printf("Air Humidity: %.1f %%\n", humidity);
  Serial.printf("Temperature: %.1f *C\n", temperature);

  // 2. Ra quyết định
  bool isSoilDry = soilMoisture > SOIL_DRY_THRESHOLD; // Giá trị analog CÀNG CAO = CÀNG KHÔ
  bool isDayTime = lightLevel > LIGHT_DARK_THRESHOLD; // Giả sử LDR cho giá trị cao khi sáng

  Serial.printf("Condition: Is Soil Dry? %s\n", isSoilDry ? "Yes" : "No");
  Serial.printf("Condition: Is Day Time? %s\n", isDayTime ? "Yes" : "No");

  // Logic: Chỉ tưới khi đất khô VÀ là ban ngày
  // (Tránh tưới ban đêm dễ gây nấm mốc)
  if (isSoilDry && isDayTime) {
    Serial.println("DECISION: Turning PUMP ON");
    digitalWrite(PUMP_RELAY_PIN, HIGH); // Bật máy bơm (Relay kích mức HIGH)
  } else {
    Serial.println("DECISION: Turning PUMP OFF");
    digitalWrite(PUMP_RELAY_PIN, LOW); // Tắt máy bơm
  }
}

// ========== PHẦN 5: SETUP VÀ LOOP ==========

void setup() {
  Serial.begin(115200);
  Serial.println("Booting Smart Irrigation System...");
  Serial.printf("Current Firmware Version: %.1f\n", CURRENT_VERSION);

  // Khởi tạo các chân
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, LOW); // Đảm bảo bơm tắt khi khởi động
  
  // Khởi tạo cảm biến
  dht.begin();
  
  // Kết nối Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.println(WiFi.localIP());

  // === KIỂM TRA CẬP NHẬT KHI KHỞI ĐỘNG ===
  // (Chúng ta chỉ làm 1 lần khi khởi động để hệ thống ổn định)
  checkForUpdates(); 
}

void loop() {
  // Chạy logic tưới cây
  runIrrigationLogic();

  // Chờ một khoảng thời gian trước khi kiểm tra lại
  // (Ví dụ: kiểm tra mỗi 10 phút)
  Serial.println("Sleeping for 10 minutes...");
  delay(10 * 60 * 1000); 
}