#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <Adafruit_MLX90614.h>
#include <UniversalTelegramBot.h>
#include <Preferences.h>  // Thư viện để lưu trữ dữ liệu

Preferences wifiPreferences;   // Thư viện để lưu trữ WiFi
Preferences healthPreferences; // Thư viện để lưu trữ các ngưỡng sức khỏe

WebServer server(80);
IPAddress local_ip(192,168,4,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

const char* telegram_token = "7525930780:AAFrdIkQ8t3al2tELKBVzYb0tBG8CJnW9j0";
const char* chatID = "-1002168353254";
const char* ap_ssid = "ESP32-Access-Point";
const char* ap_password = "12345678";

WiFiClientSecure client;
UniversalTelegramBot bot(telegram_token, client);
// gy906
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

float spO2 = 97;
float bpm = 90;
// Cài đặt cho cảm biến GY-906 (MLX90614)
bool sensorConnected = false;  // Biến để theo dõi kết nối cảm biến
float Temp = NAN;  // Biến lưu giá trị nhiệt độ

// Biến để lưu trữ các giá trị người dùng
String userName;
bool isConfigured = false;
bool isMeasuring = true;
// Các ngưỡng giới hạn được nhập qua Telegram
float maxBpm = 0.0;
float minBpm = 0.0;
float minSpO2 = 0.0;
float maxTemp = 0.0;
float minTemp = 0.0;
//
bool isSportMode = false;
unsigned long sportDuration = 0;
unsigned long sportStartTime = 0;

// Các giá trị ngưỡng cho chế độ sport
float sportMaxBpm = 0.0;
float sportMinBpm = 0.0;
float sportMinSpO2 = 0.0;
float sportMaxTemp = 0.0;
float sportMinTemp = 0.0;
// Trạng thái yêu cầu nhập dữ liệu
bool waitingForSportData = false;
bool waitingForMaxBpm = false;
bool waitingForMinBpm = false;
bool waitingForMinSpO2 = false;
bool waitingForMaxTemp = false;
bool waitingForMinTemp = false;
#define MAX_READINGS 10 // Định nghĩa số lượng giá trị nhịp tim cần lưu
float heartRateReadings[MAX_READINGS];
int readingIndex = 0;
bool readingsFilled = false;

bool isAPMode = false;
bool isConnectedToWiFi = false;

void handleRoot() {
  server.send(200, "text/html",
    "<!DOCTYPE html><html lang=\"en\">"
    "<head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Wi-Fi Login</title><style>"
    "body { font-family: Arial; background-color: #f4f4f4; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; }"
    "form { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1); max-width: 400px; width: 100%; }"
    "input[type='text'], input[type='password'] { width: 100%; padding: 10px; margin: 8px 0; border: 1px solid #ccc; border-radius: 4px; }"
    "input[type='submit'] { background-color: #4CAF50; color: white; padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }"
    "input[type='submit']:hover { background-color: #45a049; }"
    "</style></head>"
    "<body><form action=\"/login\" method=\"POST\">"
    "<h2>Connect to Wi-Fi</h2>"
    "<label for=\"ssid\">Wi-Fi SSID:</label>"
    "<input type=\"text\" id=\"ssid\" name=\"ssid\" required><br><br>"
    "<label for=\"password\">Wi-Fi Password:</label>"
    "<input type=\"password\" id=\"password\" name=\"password\" required><br><br>"
    "<input type=\"submit\" value=\"Connect\"></form></body></html>"
  );
}

void handleLogin() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Wi-Fi Connection</title></head><body><p>Attempting to connect to " + ssid + "...</p></body></html>");

    WiFi.begin(ssid.c_str(), password.c_str());
    int timeout = 10;

    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(1000);
      timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
      server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Wi-Fi Connected</title></head><body><h3>Successfully connected to " + ssid + "!</h3></body></html>");
      Serial.println("Connected to " + ssid);
      WiFi.softAPdisconnect(true);
      saveWiFiCredentials(ssid, password);
    } else {
      server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Connection Failed</title></head><body><p>Failed to connect to " + ssid + ". Please try again.</p></body></html>");
      Serial.println("Failed to connect to " + ssid);
    }
  } else {
    server.send(400, "text/html", "<p>Invalid Request</p>");
  }
}

void connectToSavedWiFi() {
  wifiPreferences.begin("wifi", true);
  int count = wifiPreferences.getInt("count", 0);

  if (count == 0) {
    Serial.println("No saved WiFi networks found.");
    return;
  }

  for (int i = 0; i < count; i++) {
    String ssid = wifiPreferences.getString(("ssid" + String(i)).c_str());
    String password = wifiPreferences.getString(("password" + String(i)).c_str());

    WiFi.begin(ssid.c_str(), password.c_str());

    int timeout = 10;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
      delay(1000);
      timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected to saved WiFi: " + ssid);
      wifiPreferences.end();
      return;
    }
  }

  wifiPreferences.end();
  Serial.println("No saved WiFi networks found.");
}

void saveWiFiCredentials(String ssid, String password) {
  if (!wifiPreferences.begin("wifi", false)) {
    Serial.println("Failed to open Preferences for writing");
    return;
  }

  int count = wifiPreferences.getInt("count", 0);

  if (count >= 4) {
    for (int i = 1; i < 4; i++) {
      String old_ssid = wifiPreferences.getString(("ssid" + String(i)).c_str());
      String old_password = wifiPreferences.getString(("password" + String(i)).c_str());
      wifiPreferences.putString(("ssid" + String(i - 1)).c_str(), old_ssid);
      wifiPreferences.putString(("password" + String(i - 1)).c_str(), old_password);
    }
    count = 3;
  }

  bool ssidSaved = wifiPreferences.putString(("ssid" + String(count)).c_str(), ssid);
  bool passwordSaved = wifiPreferences.putString(("password" + String(count)).c_str(), password);
  wifiPreferences.putInt("count", count + 1);

  if (ssidSaved && passwordSaved) {
    Serial.println("WiFi credentials saved successfully for SSID: " + ssid);
  } else {
    Serial.println("Failed to save WiFi credentials for SSID: " + ssid);
  }

  wifiPreferences.end();
}

void startAccessPoint() {
  if (!isAPMode) {
    WiFi.softAP(ap_ssid, ap_password);
    server.on("/", handleRoot);
    server.on("/login", handleLogin);
    server.begin();
    isAPMode = true;
    Serial.println("WiFi Access Point started with SSID: " + String(ap_ssid));
  }
}

void stopAccessPoint() {
  if (isAPMode) {
    WiFi.softAPdisconnect(true);
    server.close();
    isAPMode = false;
    Serial.println("WiFi Access Point stopped.");
  }
}

void showSavedWiFi() {
  wifiPreferences.begin("wifi", true);
  int count = wifiPreferences.getInt("count", 0);
  String wifiList = "Các mạng WiFi đã lưu:\n";
  
  if (count == 0) {
    wifiList = "Không có mạng WiFi nào đã được lưu.";
  } else {
    for (int i = 0; i < count; i++) {
      String ssid = wifiPreferences.getString(("ssid" + String(i)).c_str());
      wifiList += String(i + 1) + ". " + ssid + "\n";
    }
  }
  
  bot.sendMessage(chatID, wifiList, "");
  wifiPreferences.end();
  Serial.println(wifiList);
}

void startSportMode(int duration) {
    isMeasuring = false;
    isSportMode = true;
    sportDuration = duration * 60000;  // Chuyển phút sang mili giây
    sportStartTime = millis();
    waitingForSportData = true;
    waitingForMaxBpm = true;
    bot.sendMessage(chatID, "Nhập nhịp tim cao nhất /sportmaxbpm [Nhịp tim tối đa]", "");
}
void handleTelegramInput(String text) {
    if (waitingForSportData) {
        if (waitingForMaxBpm) {
          if (text.startsWith("/sportmaxbpm ")){
            sportMaxBpm = text.substring(13).toFloat();
            waitingForMaxBpm = false;
            bot.sendMessage(chatID, "Nhịp tim cao nhất đã được lưu: " + String(sportMaxBpm) + " bpm.", "");
            waitingForMinBpm = true;
            bot.sendMessage(chatID, " Nhập nhịp tim thấp nhất /sportminbpm [Nhịp tim tối thiểu]:" , "");
          }
        } else if (waitingForMinBpm) {
          if (text.startsWith("/sportminbpm ")){
            sportMinBpm = text.substring(13).toFloat();
            waitingForMinBpm = false;
            bot.sendMessage(chatID, "Nhịp tim thấp nhất đã được lưu: " + String(sportMinBpm) + " bpm.", "");
            waitingForMinSpO2 = true;
            bot.sendMessage(chatID, "Nhập ngưỡng SpO2 thấp nhất /sportminspo2 [SpO2 tối thiểu]:" , "");
          }
        } else if (waitingForMinSpO2) {
          if (text.startsWith("/sportminspo2")){
            sportMinSpO2 = text.substring(14).toFloat();
            waitingForMinSpO2 = false;
            bot.sendMessage(chatID, "Ngưỡng SpO2 thấp nhất đã được lưu: " + String(sportMinSpO2) + " %.", "");
            waitingForMaxTemp = true;
            bot.sendMessage(chatID, "Nhập nhiệt độ cao nhất /sportmaxtemp [Nhiệt độ cao nhất]:" , "");
          }
        } else if (waitingForMaxTemp) {
          if (text.startsWith("/sportmaxtemp ")){
            sportMaxTemp = text.substring(14).toFloat();
            waitingForMaxTemp = false;
            bot.sendMessage(chatID, "Nhiệt độ cao nhất đã được lưu: " + String(sportMaxTemp) + " °C.", "");
            waitingForMinTemp = true;
            bot.sendMessage(chatID, " Nhập nhiệt độ thấp nhất: /sportmintemp [Nhiệt độ cao nhất]:" , "");
          }
        } else if (waitingForMinTemp) {
          if (text.startsWith("/sportmintemp ")){
            sportMinTemp = text.substring(14).toFloat();
            waitingForMinTemp = false;
            waitingForSportData = false;
            String dulieu = "Dữ liệu đã lưu ở chế độ thể thao:\n";
            dulieu += "Nhịp tim cao nhất ở chế độ thể thao: " + String(sportMaxBpm) + " bpm\n";
            dulieu += "Nhịp tim thấp nhất ở chế độ thể thao: " + String(sportMinBpm) + " bpm\n";
            dulieu += "SpO2 thấp nhất ở chế độ thể thao: " + String(sportMinSpO2) + " %\n";
            dulieu += "Nhiệt độ cao nhất ở chế độ thể thao: " + String(sportMaxTemp) + " °C\n";
            dulieu += "Nhiệt độ thấp nhất ở chế độ thể thao: " + String(sportMinTemp) + " °C\n";
            bot.sendMessage(chatID, "Nhiệt độ thấp nhất đã được lưu: " + String(sportMinTemp) + " °C.", "");
            bot.sendMessage(chatID, "Chế độ thể thao đã kích hoạt." , "");
            bot.sendMessage(chatID, dulieu, "");
          }
        } else {
          bot.sendMessage(chatID, "Lệnh không hợp lệ. Vui lòng nhập lại.", "");
        }
    }
}
void checkSportMode() {
    if (isSportMode) {
        // Kiểm tra nếu đã hết thời gian chế độ sport
        if (millis() - sportStartTime > sportDuration) {
          isMeasuring = true;
          bot.sendMessage(chatID, "Chế độ thể thao đã kết thúc.", "");
          isSportMode = false;
          return;
        }
        if(sportMaxBpm != 0.0 && sportMinBpm != 0.0 && sportMinSpO2 != 0.0 && sportMaxTemp != 0.0 && sportMinTemp != 0.0){
          // So sánh các giá trị hiện tại với các giá trị ngưỡng
          if (bpm > sportMaxBpm) {
              bot.sendMessage(chatID, userName + " Cảnh báo: Nhịp tim của bạn vượt quá ngưỡng cho phép trong chế độ thể thao!", "");
          }
          if (bpm < sportMinBpm) {
              bot.sendMessage(chatID, userName + " Cảnh báo: Nhịp tim của bạn thấp hơn ngưỡng cho phép trong chế độ thể thao!", "");
          }
          if (spO2 < sportMinSpO2) {
              bot.sendMessage(chatID, userName + " Cảnh báo: SpO2 của bạn thấp hơn ngưỡng cho phép trong chế độ thể thao!", "");
          }
          if (Temp > sportMaxTemp) {
              bot.sendMessage(chatID, userName + " Cảnh báo: Nhiệt độ cơ thể của bạn vượt quá ngưỡng cho phép trong chế độ thể thao!", "");
          }
          if (Temp < sportMinTemp) {
              bot.sendMessage(chatID, userName + " Cảnh báo: Nhiệt độ cơ thể của bạn thấp hơn ngưỡng cho phép trong chế độ thể thao!", "");
          }
        }
    }
}
// Đọc nhiệt độ từ GY-906
void getTemperature() {
  if (sensorConnected) {
    Temp = mlx.readObjectTempC();  // Lưu nhiệt độ vào biến Temp
  } else {
    Temp = NAN;  // Nếu cảm biến không kết nối, giá trị là không hợp lệ
  }
}
void setup() {
  Serial.begin(115200);
  Wire.begin(11, 12);
  //cảm biến 
  mlx.begin(); 
  // Kiểm tra xem có dữ liệu WiFi đã lưu chưa
  connectToSavedWiFi();
  client.setInsecure();
  if (!isConnectedToWiFi) {
    startAccessPoint();  // Nếu không có kết nối WiFi, bật chế độ AP
  } 
  // Kiểm tra kết nối cảm biến
  if (!mlx.begin()) {
    Serial.println("Error: MLX90614 sensor not found.");
    bot.sendMessage(chatID, "Lỗi: Không thể kết nối cảm biến nhiệt độ.", "");
    sensorConnected = false;  // Cảm biến không kết nối
  } else {
    sensorConnected = true;   // Cảm biến đã kết nối
  }
 // Khởi tạo thư viện Preferences với namespace "health-monitor" cho ngưỡng sức khỏe
  healthPreferences.begin("health-monitor", false);

  // Kiểm tra nếu có dữ liệu đã được lưu trước đó
  userName = healthPreferences.getString("userName", "");
  maxBpm = healthPreferences.getFloat("maxBpm", 0);
  minBpm = healthPreferences.getFloat("minBpm", 0);
  minSpO2 = healthPreferences.getFloat("minSpO2", 0);
  maxTemp = healthPreferences.getFloat("maxTemp", 0);
  minTemp = healthPreferences.getFloat("minTemp", 0);

  if (userName != "" && maxBpm != 0 && minBpm != 0 && minSpO2 != 0 && maxTemp != 0 && minTemp != 0 ) {
    isConfigured = true;
    bot.sendMessage(chatID, "Thiết bị đã khởi động", "");
  } else {
    String codee = "Các lệnh có sẵn:\n";
    codee += "/name [Tên]: Nhập tên của bạn.\n";
    codee += "/maxbpm [Nhịp tim tối đa]: Nhập ngưỡng nhịp tim cao nhất.\n";
    codee += "/minbpm [Nhịp tim tối thiểu]: Nhập ngưỡng nhịp tim thấp nhất.\n";
    codee += "/minspo2 [SpO2 tối thiểu]: Nhập ngưỡng SpO2 thấp nhất.\n";
    codee += "/maxtemp [Nhiệt độ cao nhất]: Nhập ngưỡng nhiệt độ cao nhất.\n";
    codee += "/mintemp [Nhiệt độ thấp nhất]: Nhập ngưỡng nhiệt độ thấp nhất.\n";
    bot.sendMessage(chatID, "Vui lòng nhập tên và các ngưỡng giới hạn theo cú pháp dưới đây: ", "");
    bot.sendMessage(chatID, codee, "");
  }
  
}

void loop() {
  server.handleClient();
  getTemperature();  // Gọi hàm để đo nhiệt độ
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    for (int i = 0; i < numNewMessages; i++) {
        String text = bot.messages[i].text;
        String fromName = bot.messages[i].from_name;

       if (text.startsWith("/name ") && !isConfigured) {
          if(userName != ""){
            bot.sendMessage(chatID, "Vui lòng xóa tên cũ, tên cũ :" + userName, "");
          } else {
            userName = text.substring(6);
            healthPreferences.putString("userName", userName);
            bot.sendMessage(chatID, "Tên của bạn đã được lưu: " + userName, "");
            bot.sendMessage(chatID,"/maxbpm [Nhịp tim tối đa]: Nhập ngưỡng nhịp tim cao nhất.","");
          }
      } else if (text.startsWith("/maxbpm ") && !isConfigured) {
          if(maxBpm != 0){
            bot.sendMessage(chatID, "Vui lòng xóa nhịp tim cao nhất cũ, nhịp tim cao nhất cũ :" + String(maxBpm), "");
          } else {
            maxBpm = text.substring(8).toFloat();
            healthPreferences.putFloat("maxBpm", maxBpm);
            bot.sendMessage(chatID, "Ngưỡng nhịp tim cao nhất đã được lưu: " + String(maxBpm) + " bpm", "");
            bot.sendMessage(chatID,"/minbpm [Nhịp tim tối thiểu]: Nhập ngưỡng nhịp tim thấp nhất.","");
          }
      } else if (text.startsWith("/minbpm ") && !isConfigured) {
          if(minBpm != 0) {
            bot.sendMessage(chatID, "Vui lòng xóa nhịp tim thấp nhất cũ, nhịp tim thấp nhất cũ :" + String(minBpm), "");
          } else {
            minBpm = text.substring(8).toFloat();
            healthPreferences.putFloat("minBpm", minBpm);
            bot.sendMessage(chatID, "Ngưỡng nhịp tim thấp nhất đã được lưu: " + String(minBpm) + " bpm", "");
            bot.sendMessage(chatID,"/minspo2 [SpO2 tối thiểu]: Nhập ngưỡng SpO2 thấp nhất.","");
          }
      } else if (text.startsWith("/minspo2 ") && !isConfigured) {
         if(minSpO2 != 0) {
            bot.sendMessage(chatID, "Vui lòng xóa SP02 thấp nhất cũ, SP02 thấp nhất cũ :" + String(minSpO2), "");
         } else {
            minSpO2 = text.substring(9).toFloat();
            healthPreferences.putFloat("minSpO2", minSpO2);
            bot.sendMessage(chatID, "Ngưỡng SpO2 thấp nhất đã được lưu: " + String(minSpO2) + " %", "");
            bot.sendMessage(chatID,"/maxtemp [Nhiệt độ cao nhất]: Nhập ngưỡng nhiệt độ cao nhất.","");
         }
      } else if (text.startsWith("/maxtemp ") && !isConfigured) {
          if(maxTemp != 0) {
            bot.sendMessage(chatID, "Vui lòng xóa nhiệt độ cao nhất cũ, nhiệt độ cao nhất cũ :" + String(maxTemp), "");
          } else {
            maxTemp = text.substring(9).toFloat();
            healthPreferences.putFloat("maxTemp", maxTemp);
            bot.sendMessage(chatID, "Ngưỡng nhiệt độ cao nhất đã được lưu: " + String(maxTemp) + " °C", "");
            bot.sendMessage(chatID,"/mintemp [Nhiệt độ thấp nhất]: Nhập ngưỡng nhiệt độ thấp nhất.","");
          }
      } else if (text.startsWith("/mintemp ") && !isConfigured) {
          if(minTemp != 0){
            bot.sendMessage(chatID, "Vui lòng xóa nhiệt độ thấp nhất cũ, nhiệt độ thấp nhất cũ :" + String(minTemp), "");
          } else {
            minTemp = text.substring(9).toFloat();
            healthPreferences.putFloat("minTemp", minTemp);
            bot.sendMessage(chatID, "Ngưỡng nhiệt độ thấp nhất đã được lưu: " + String(minTemp) + " °C", "");
            bot.sendMessage(chatID, "Cập nhật dữ liệu hoàn tất. Vui lòng nhấn /start để bắt đầu.", "");
          }
      } else if (waitingForMaxBpm || waitingForMinBpm || waitingForMinSpO2 || waitingForMaxTemp || waitingForMinTemp) {
        handleTelegramInput(text);  // Xử lý từng giá trị theo trạng thái hiện tại
      } else if (text.startsWith("/sport ")) {
        if (isSportMode == true){
          bot.sendMessage(chatID, "Đã kích hoạt chế độ sport.", "");
        } else if(isSportMode == false) {
          int duration = text.substring(7).toInt(); // Lấy thời gian chế độ sport
          startSportMode(duration);
        }
      } else if (waitingForSportData) {
        handleTelegramInput(text);  // Xử lý nhập liệu cho chế độ thể thao
      } else if (text == "/start") {
        if (isSportMode == true){
          isMeasuring = false;
        } else if (isSportMode == false){
          isMeasuring = true;
          if (userName != "" && maxBpm != 0 && minBpm != 0 && minSpO2 != 0 && maxTemp != 0 && minTemp != 0) {
            bot.sendMessage(chatID, "Bắt đầu đo lường. Tên: " + userName, "");
            isMeasuring = true;
          } else {
            String codee = "Vui lòng nhập tên và các ngưỡng giới hạn theo cú pháp dưới đây:";
            codee += "/name [Tên]: Nhập tên của bạn.\n";
            codee += "/maxbpm [Nhịp tim tối đa]: Nhập ngưỡng nhịp tim cao nhất.\n";
            codee += "/minbpm [Nhịp tim tối thiểu]: Nhập ngưỡng nhịp tim thấp nhất.\n";
            codee += "/minspo2 [SpO2 tối thiểu]: Nhập ngưỡng SpO2 thấp nhất.\n";
            codee += "/maxtemp [Nhiệt độ cao nhất]: Nhập ngưỡng nhiệt độ cao nhất.\n";
            codee += "/mintemp [Nhiệt độ thấp nhất]: Nhập ngưỡng nhiệt độ thấp nhất.\n";
            bot.sendMessage(chatID, codee, "");
          }
        }
      } else if (text == "/scan") {
        if (userName != "" && maxBpm != 0 && minBpm != 0 && minSpO2 != 0 && maxTemp != 0 && minTemp != 0) {
          getTemperature();  // Gọi hàm để đo nhiệt độ
          String status = userName + " Ơi, đây là dữ liệu đo được của bạn:\n";
          status += "Nhịp tim : " + String(bpm) + " bpm\n";
          status += "SpO2 : " + String(spO2) + " %\n";
          status += "Nhiệt độ : " + String(Temp) + " °C\n";
          bot.sendMessage(chatID, status, "");
        } else {
          String codee = "Vui lòng nhập tên và các ngưỡng giới hạn theo cú pháp dưới đây:";
          codee += "/name [Tên]: Nhập tên của bạn.\n";
          codee += "/maxbpm [Nhịp tim tối đa]: Nhập ngưỡng nhịp tim cao nhất.\n";
          codee += "/minbpm [Nhịp tim tối thiểu]: Nhập ngưỡng nhịp tim thấp nhất.\n";
          codee += "/minspo2 [SpO2 tối thiểu]: Nhập ngưỡng SpO2 thấp nhất.\n";
          codee += "/maxtemp [Nhiệt độ cao nhất]: Nhập ngưỡng nhiệt độ cao nhất.\n";
          codee += "/mintemp [Nhiệt độ thấp nhất]: Nhập ngưỡng nhiệt độ thấp nhất.\n";
          bot.sendMessage(chatID, codee, "");
        }
      } else if (text == "/reset") {
        bot.sendMessage(chatID, "Đã xóa tất cả dữ liệu.", "");
        bot.sendMessage(chatID, "Vui lòng nhập lại dữ liệu để đo và gửi cảnh báo.", "");
        userName = "";
        maxBpm = 0.0;
        minBpm = 0.0;
        minSpO2 = 0.0;
        maxTemp = 0.0;
        minTemp = 0.0;
        isConfigured = false;
        isMeasuring = false;
        healthPreferences.clear();  // Xóa tất cả dữ liệu trong namespace "health-monitor"
      } else if (text == "/savedhealth") {
        // Hiển thị dữ liệu hiện tại đã lưu
        String status = "Dữ liệu hiện tại đã lưu:\n";
        status += "Tên người dùng: " + userName + "\n";
        status += "Nhịp tim cao nhất: " + String(maxBpm) + " bpm\n";
        status += "Nhịp tim thấp nhất: " + String(minBpm) + " bpm\n";
        status += "SpO2 thấp nhất: " + String(minSpO2) + " %\n";
        status += "Nhiệt độ cao nhất: " + String(maxTemp) + " °C\n";
        status += "Nhiệt độ thấp nhất: " + String(minTemp) + " °C\n";
        bot.sendMessage(chatID, status, "");
      // Lệnh để hiển thị các mạng WiFi đã lưu
      } else if (text == "/showwifi") {
        showSavedWiFi();  // Hiển thị danh sách WiFi đã lưu
      } else if (text == "/code") {
        String codeee = "Các lệnh có sẵn:\n";
        codeee += "/start : Để bắt đầu sử dụng thiết bị.\n";
        codeee += "/showwifi : Hiện các wifi đã kết nối.\n";
        codeee += "/reset : Xóa tất cả dữ liệu.\n";
        codeee += "/scan : Để quét nhịp tim, sp02, nhiệt độ cơ thể.\n";
        codeee += "/savedhealth : Để hiển thị các chỉ số đã lưu.\n";
        codeee += "/sport [thời gian (phút)] : Khởi tạo chế độ sport.\n";
        codeee += "/gps : xem tọa độ GPS.\n";
        codeee += "/code : Để hiển thị tất cả các lệnh.\n";
        bot.sendMessage(chatID, codeee, "");
      } else {
        bot.sendMessage(chatID, "Lệnh không hợp lệ. Vui lòng nhập lại.", "");
      }
    }
  if (isMeasuring) {
    if(userName != "" && maxBpm != 0 && minBpm != 0 && minSpO2 != 0 && maxTemp != 0 && minTemp != 0){
      // Đo nhịp tim, nhiệt độ và SpO2
      // ...
      // Cảnh báo nếu vượt ngưỡng giới hạn
      if (bpm > maxBpm) {
        bot.sendMessage(chatID, userName + " Cảnh báo: Nhịp tim vượt quá ngưỡng cho phép!", "");
      }
      if (bpm < minBpm) {
        bot.sendMessage(chatID, userName + " Cảnh báo: Nhịp tim thấp hơn ngưỡng cho phép!", "");
      }
      if (spO2 < minSpO2) {
        bot.sendMessage(chatID, userName +  " Cảnh báo: Mức SpO2 thấp hơn ngưỡng cho phép!", "");
      }
      if (Temp > maxTemp) {
        bot.sendMessage(chatID, userName + " Cảnh báo: Nhiệt độ cơ thể cao hơn ngưỡng cho phép!", "");
      }
      if (Temp < minTemp) {
        bot.sendMessage(chatID, userName + " Cảnh báo: Nhiệt độ cơ thể thấp hơn ngưỡng cho phép!", "");
      }
    }
  }
  if (WiFi.status() == WL_CONNECTED && isAPMode) {
    stopAccessPoint();
  }
  if(sportMaxBpm = !0.0 && sportMinBpm != 0.0 && sportMinSpO2 != 0.0 && sportMaxTemp != 0.0 && sportMinTemp != 0.0) {
    // Kiểm tra các giá trị trong chế độ thể thao và cảnh báo nếu cần
    checkSportMode();
  }
}
