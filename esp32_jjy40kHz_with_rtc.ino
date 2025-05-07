// esp32_jjy40kHz_with_rtc.ino
//   Customized & enhanced by Chimung, based on ver.0.10 (c) 2024/05/16 by Nash Shuji009
//
// 📡 ESP32 JJY 40kHz Transmitter with RTC Backup & NTP Sync
//
// 🛠️ Features:
//   - Generate 40kHz PWM signal via GPIO26 to simulate JJY time code
//   - Automatically sync time via WiFi NTP on startup
//   - Includes DS1302 RTC support to maintain accurate time even without network
//   - Fallback to RTC time if NTP fails (offline mode)
//   - Expanded sg[] buffer to prevent overflow and added safe buffer operations
//   - Enhanced runtime logs and error messages for easier debugging
//
// 🧷 Hardware Wiring:
//   - Antenna (loop type): GPIO26 → 220Ω → Wire Loop → GND
//   - RTC DS1302: IO=GPIO13, SCLK=GPIO14, RST=GPIO15

#include <Arduino.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

// RTC 模組腳位 (依你實際接法調整)
ThreeWire myWire(13, 14, 15); // IO, SCLK, RST
RtcDS1302<ThreeWire> rtc(myWire);

// 硬體設定
const int ledChannel = 0;          // LEDC PWM 通道 0，用於產生 40kHz 輸出至 JJY 天線
const int ledPin = 26;             // 輸出 JJY 信號的 GPIO 腳位，接至天線（建議串接 220Ω）
const int wifiStatusLED = 2;       // WiFi 狀態指示燈，使用內建的 GPIO2，成功連線後點亮

char P0,P1,P2,P3,P4,P5;
const char M = P0 = P1 = P2 = P3 = P4 = P5 = -1;
char PA1,PA2, SU1,  LS1,LS2;

char  sg[62];
// sg[] 為 60 秒 JJY 時碼資料陣列（sg[0]~sg[59]），另含 sg[60], sg[61] 安全邊界

const char* ssid     = "SSID";  // 請填入WIFI名稱
const char* password = "PASSWORD";  // 請填入WIFI密碼

void setup() {
  pinMode(wifiStatusLED, OUTPUT);
  digitalWrite(wifiStatusLED, LOW);  // 預設熄滅
  Serial.begin(115200);
  delay(100);
  Serial.println("🟢 setup 開始");

  // 初始化 RTC
  rtc.Begin();

  // 確保 RTC 非寫入保護狀態，且正在走時
  if (rtc.GetIsWriteProtected()) {
    rtc.SetIsWriteProtected(false);
    Serial.println("🔓 RTC 寫入保護已解除");
  }
  if (!rtc.GetIsRunning()) {
    rtc.SetIsRunning(true);
    Serial.println("⏱️ RTC 已啟動");
  }

  // 嘗試透過 WiFi 同步 NTP
  WiFi.mode(WIFI_STA);
if (WiFi.status() != WL_CONNECTED) {
  Serial.println("⚠️ WiFi 連線失敗，跳過 NTP 同步");
}
  WiFi.begin(ssid, password);
  Serial.print("🌐 WiFi 連線中");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  bool ntpSuccess = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 已連線");
  //使用台灣伺服器標準時間，預設日本時區UTF-9
    configTime(9 * 3600L, 0, "time.stdtime.gov.tw", "time.google.com", "pool.ntp.org");

    struct tm timeInfo;
    for (int i = 0; i < 3; i++) {
if (getLocalTime(&timeInfo)) {
  if (timeInfo.tm_year >= 120) {
    // 等到下一秒邊界
    time_t rawtime;
    time(&rawtime);  // 取得當前時間（秒）
    while (time(nullptr) == rawtime) {
      delay(1);  // 等到進入下一秒
    }

    // 再取得一次正確對齊的 NTP 時間
    getLocalTime(&timeInfo);

    RtcDateTime ntpTime(
        timeInfo.tm_year + 1900,
        timeInfo.tm_mon + 1,
        timeInfo.tm_mday,
        timeInfo.tm_hour,
        timeInfo.tm_min,
        timeInfo.tm_sec);

    rtc.SetDateTime(ntpTime);
    digitalWrite(wifiStatusLED, HIGH); //成功取得NTP亮燈
    Serial.printf("📡 精準對齊整秒後寫入 RTC：%04d/%02d/%02d %02d:%02d:%02d\n",
                  ntpTime.Year(), ntpTime.Month(), ntpTime.Day(),
                  ntpTime.Hour(), ntpTime.Minute(), ntpTime.Second());
    ntpSuccess = true;
    break;
        } else {
          Serial.println("⚠️ NTP 時間無效（年份 < 2020）");
        }
      } else {
        Serial.printf("⌛ 第 %d 次取得 NTP 失敗，重試中...\n", i + 1);
        delay(2000);
      }
    }
  } else {
    Serial.println("❌ 無法連上 WiFi");
  }

  if (!ntpSuccess) {
    if (rtc.IsDateTimeValid()) {
      Serial.println("⚠️ 使用 RTC 內部時間：");
      RtcDateTime now = rtc.GetDateTime();
      char buf[64];
      sprintf(buf, "⏰ RTC 時間：%04u/%02u/%02u %02u:%02u:%02u",
              now.Year(), now.Month(), now.Day(),
              now.Hour(), now.Minute(), now.Second());
      Serial.println(buf);

       // 新增這段判斷
    if (now.Year() < 2020) {
      Serial.println("❗ RTC 時間異常（小於 2020），請確認 RTC 電池或是否已初始化");
    }
    
    } else {
      Serial.println("🛑 RTC 時間無效，無法取得正確時間！");
    }
  }

  // 初始化發波器
  set_fix();  // 訊號標記設置
  ledcSetup(ledChannel, 40000, 8);  // 40kHz PWM
  ledcAttachPin(ledPin, ledChannel);

  Serial.println("✅ 初始化完成，開始發射計時波訊號");
}

void loop() {

  if (!rtc.IsDateTimeValid()) {
  Serial.println("🛑 RTC 時間無效，跳過此次發波");
  delay(1000);
  return;
}

  // 🕛 等待整秒交界：避免 sg[0] 發送不是在秒 0.000
RtcDateTime prev = rtc.GetDateTime();
while (true) {
  RtcDateTime curr = rtc.GetDateTime();
  if (curr.Second() != prev.Second() && curr.Second() != 0) break;
  delay(1);
}

  // 🎯 抓取秒 0 時的時間，作為發波基準
  RtcDateTime now = rtc.GetDateTime();
  if (!now.IsValid()) {
    Serial.println("⚠️ RTC 時間無效，請檢查模組或重新初始化");
    delay(2000);
    return;
  }

  // 顯示當前時間
  char buf[64];
  snprintf(buf, sizeof(buf), "⏰ %04u/%02u/%02u %02u:%02u:%02u",
           now.Year(), now.Month(), now.Day(),
           now.Hour(), now.Minute(), now.Second());
  Serial.println(buf);

  // 轉換成 struct tm
  struct tm timeInfo;
  timeInfo.tm_year = now.Year() - 1900;
  timeInfo.tm_mon  = now.Month() - 1;
  timeInfo.tm_mday = now.Day();
  timeInfo.tm_hour = now.Hour();
  timeInfo.tm_min  = now.Minute();
  timeInfo.tm_sec  = now.Second();
  timeInfo.tm_wday = now.DayOfWeek();

  // 計算每年的第幾天
  static const int daysInMonth[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  timeInfo.tm_yday = timeInfo.tm_mday - 1;
  for (int i = 0; i < timeInfo.tm_mon; ++i) {
    timeInfo.tm_yday += daysInMonth[i];
  }
  int year = timeInfo.tm_year + 1900;
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    if (timeInfo.tm_mon > 1) timeInfo.tm_yday += 1; // 閏年加一天
  }

  // 🧮 處理閏秒（如果有）
  int se = timeInfo.tm_sec, sh = 0;
  if (se == 60) {
    sg[53] = LS1 = 1; sg[54] = LS2 = 0; se = 59; sh = 1;
  } else if (se == 61) {
    sg[53] = LS1 = 1; sg[54] = LS2 = 1; se = 58; sh = 2;
  }

  // 編碼 sg[] 時碼資料
  set_fix();
  set_min(timeInfo.tm_min);
  set_hour(timeInfo.tm_hour);
  set_day(timeInfo.tm_yday + 1);
  set_wday(timeInfo.tm_wday);
  set_year(year - 2000);

  // 🔁 傳送 JJY 波形（sg[0] 到 sg[59]）
  Serial.printf("📡 開始發送時間碼：從 %02d 秒起，預計長度 %d 秒\n", se, 60 + sh - se);
  char t[64];
  for (int i = se; i < 60 + sh && i < 62; ++i) {
    // 安全值檢查
    if (sg[i] != -1 && sg[i] != 0 && sg[i] != 1 && sg[i] != 255) {
      Serial.printf("⚠️ sg[%d] 值異常：%d，自動修正為 0\n", i, sg[i]);
      sg[i] = 0;
    }

    snprintf(t, sizeof(t), "%02d ", sg[i]);
    Serial.print(t);

    // 發送對應波形
    switch (sg[i]) {
      case -1:
      case 255:
        mark(); break;
      case 0:
        zero(); break;
      case 1:
        one(); break;
    }
  }

  delay(5); // 稍微喘口氣，避免佔用過多資源
}


void set_year(int n){
  
  int m = dec2BCD(n);

  sg[48] = m % 2; m = m >> 1;
  sg[47] = m % 2; m = m >> 1;
  sg[46] = m % 2; m = m >> 1;
  sg[45] = m % 2; m = m >> 1;

  sg[44] = m % 2; m = m >> 1;
  sg[43] = m % 2; m = m >> 1;
  sg[42] = m % 2; m = m >> 1;
  sg[41] = m % 2;
}

void set_day(int n){
  
  int m = dec2BCD(n);

  sg[33] = m % 2; m = m >> 1;
  sg[32] = m % 2; m = m >> 1;
  sg[31] = m % 2; m = m >> 1;
  sg[30] = m % 2; m = m >> 1;

  sg[28] = m % 2; m = m >> 1;
  sg[27] = m % 2; m = m >> 1;
  sg[26] = m % 2; m = m >> 1;
  sg[25] = m % 2; m = m >> 1;

  sg[23] = m % 2; m = m >> 1;
  sg[22] = m % 2;
}

void set_wday(int m){

  sg[52] = m % 2; m = m >> 1;
  sg[51] = m % 2; m = m >> 1;
  sg[50] = m % 2;
}

void set_hour(int n){
  
  int m = dec2BCD(n);

  sg[18] = m % 2; m = m >> 1;
  sg[17] = m % 2; m = m >> 1;
  sg[16] = m % 2; m = m >> 1;
  sg[15] = m % 2; m = m >> 1;
  
  sg[13] = m % 2; m = m >> 1;
  sg[12] = m % 2;
  
  char PA1 = sg[18] ^ sg[17] ^ sg[16] ^ sg[15] ^ sg[13] ^ sg[12]; //PA1 = (20h+10h+8h+4h+2h+1h) mod 2
  sg[36] = PA1;
}

void set_min(int n){
  
  int m = dec2BCD(n);


  sg[8] = m % 2; m = m >> 1;
  sg[7] = m % 2; m = m >> 1;
  sg[6] = m % 2; m = m >> 1;
  sg[5] = m % 2; m = m >> 1;

  sg[3] = m % 2; m = m >> 1;
  sg[2] = m % 2; m = m >> 1;
  sg[1] = m % 2;

  char PA2 = sg[8] ^ sg[7] ^ sg[6] ^ sg[5] ^ sg[3] ^ sg[2] ^ sg[1]; //PA2 = (40m+20m+10m+8m+4m+2m+1m) mod 2
  sg[37] = PA2;
}

void set_fix(){
  sg[0] = sg[9] = sg[19] = sg[29] = sg[39] = sg[49] = sg[59] = M;
  sg[4] = sg[10] = sg[11] = sg[14] = sg[20] = sg[21] = sg[24] = sg[34] = sg[35] = sg[55] = sg[56] = sg[57] = sg[58] = 0;
  sg[38] = sg[40] = 0;
  sg[53] = sg[54] = 0;
  
}

int dec2BCD(int decimal) {
    int bcd = 0;
    int multiplier = 1;

    while (decimal > 0) {
        int digit = decimal % 10;
        bcd += digit * multiplier;
        multiplier *= 16; 
        decimal /= 10;
    }

    return bcd;
    
    ///printf("BCD: %X\n", bcd);
}

void mark() {   // 0.2sec
  ledcWrite(ledChannel, 127);
  delay(200);
  ledcWrite(ledChannel, 0);
  delay(800);
}

void zero() {  // 0.8sec
  ledcWrite(ledChannel, 127);
  delay(800);
  ledcWrite(ledChannel, 0);
  delay(200);
}

void one() {    // 0.5sec
  ledcWrite(ledChannel, 127);
  delay(500);
  ledcWrite(ledChannel, 0);
  delay(500);
}
