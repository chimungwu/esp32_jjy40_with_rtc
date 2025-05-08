// esp32_jjy40kHz_with_rtc.ino
//   Customized & enhanced by Chimung, based on ver.0.12 (2025/05/08), forked from ver.0.11 (c) 2024/05/16 by Nash Shuji009
//
// 📡 ESP32 JJY 40kHz Transmitter with RTC Backup, NTP Sync, LCD Display & WiFiManager Timezone Setting
//
// 🛠️ Features:
//   - Generate 40kHz PWM signal via GPIO26 to simulate JJY time code
//   - Automatically sync time via WiFi NTP on startup with microsecond-level alignment
//   - Includes DS1302 RTC support to maintain accurate time without network
//   - Fallback to RTC time if NTP fails (offline mode)
//   - Integrated WiFiManager: auto-reconnect or enter config portal (SSID: JJY-config) if no known AP is available
//   - User-configurable time zone via WiFiManager (e.g., +8, +9, -5), stored in RAM
//   - Internal processing uses the configured timezone; LCD display always shows Taiwan time (UTC+8)
//   - LCD1602 I2C display shows current Taiwan time + NTP/RTC source and system time + timezone
//   - LCD displays setup prompt when WiFi connection fails: "Setup WiFi Via SSID: JJY-config"
//   - Clean debug output via Serial for setup, WiFi, RTC, and transmission status
//   - Expanded sg[] buffer with value safety checks to prevent overflow
//
// 🧷 Hardware Wiring:
//   - Antenna (loop type): GPIO26 → 220Ω → Wire Loop → GND
//   - RTC DS1302: IO=GPIO13, SCLK=GPIO14, RST=GPIO15
//   - WiFi status LED (optional): GPIO2 (lights up when WiFi is connected)
//   - LCD1602 (I2C address 0x27): SDA/SCL to default ESP32 I2C pins 21/22

#include <Arduino.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <WiFiManager.h>  // 加在最前面
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C 位址 0x27，16x2 螢幕

// RTC 模組腳位 (依你實際接法調整)
ThreeWire myWire(13, 14, 15); // IO, SCLK, RST
RtcDS1302<ThreeWire> rtc(myWire);

// UTC時區設定(日本+9，台灣+8)
long timeZoneOffset = 9 * 3600L;  

// 硬體設定
const int ledChannel = 0;          // LEDC PWM 通道 0，用於產生 40kHz 輸出至 JJY 天線
const int ledPin = 26;             // 輸出 JJY 信號的 GPIO 腳位，接至天線（建議串接 220Ω）
const int wifiStatusLED = 2;       // WiFi 狀態指示燈，使用內建的 GPIO2，成功連線後點亮

char P0,P1,P2,P3,P4,P5;
const char M = P0 = P1 = P2 = P3 = P4 = P5 = -1;
char PA1,PA2, SU1,  LS1,LS2;

char  sg[62];
// sg[] 為 60 秒 JJY 時碼資料陣列（sg[0]~sg[59]），另含 sg[60], sg[61] 安全邊界

const char* ssid     = "SSID";  // 請填入WIFI名稱(選填)
const char* password = "PASSWORD";  // 請填入WIFI密碼(選填)

uint64_t ntpSyncedMicros = 0;
time_t ntpSyncedTime = 0;

void setup() {
  lcd.begin();        // 初始化 LCD
  lcd.backlight();   // 開啟背光
  lcd.clear();       // 清除畫面
  lcd.setCursor(0, 0);
  lcd.print("JJY TX Booting...");

  pinMode(wifiStatusLED, OUTPUT);
  digitalWrite(wifiStatusLED, LOW);  // 預設熄滅
  Serial.begin(115200);
  delay(100);
  Serial.println("🟢 setup 開始");

  // 初始化 RTC
  rtc.Begin();
  if (rtc.GetIsWriteProtected()) {
    rtc.SetIsWriteProtected(false);
    Serial.println("🔓 RTC 寫入保護已解除");
  }
  if (!rtc.GetIsRunning()) {
    rtc.SetIsRunning(true);
    Serial.println("⏱️ RTC 已啟動");
  }

WiFi.begin(ssid, password);
Serial.print("📶 嘗試使用預設 SSID 連線中...");

unsigned long startAttemptTime = millis();
while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
  delay(500);
  Serial.print(".");
}

if (WiFi.status() == WL_CONNECTED) {
  Serial.println("\n✅ 已成功連線到預設 Wi-Fi");
  digitalWrite(wifiStatusLED, HIGH);
} else {
  Serial.println("\n❌ 預設 Wi-Fi 連線失敗，啟用 WiFiManager");
// LCD 提示使用者連線 WiFi 設定頁
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Setup WiFi Via");
  lcd.setCursor(0, 1);
  lcd.print("SSID: JJY_config");

WiFiManagerParameter tzParam("tz", "Time Zone (e.g. +9 for JST, +8 for TWN)", "+9", 4);  // 預設 +9（JST）
WiFiManager wm;
wm.setDebugOutput(true);
wm.setTimeout(60);
wm.setConnectTimeout(20);
wm.addParameter(&tzParam);

bool res = wm.autoConnect("JJY_Config");
if (!res) {
  Serial.println("❌ WiFiManager 連線失敗，請檢查設定");
} else {
  Serial.println("✅ WiFiManager 成功連線");
  digitalWrite(wifiStatusLED, HIGH);

  // 🕒 讀取使用者設定的時區並轉為秒數
  const char* tzInput = tzParam.getValue();
  if (tzInput != nullptr && strlen(tzInput) > 0) {
    int tzHour = atoi(tzInput);
    timeZoneOffset = tzHour * 3600;
    Serial.printf("🌐 使用者設定時區為：UTC %+d（%d 秒）\n", tzHour, timeZoneOffset);
  }
}
}
  bool ntpSuccess = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 已連線");
    configTime(timeZoneOffset, 0, "time.stdtime.gov.tw", "time.google.com", "pool.ntp.org");

    struct tm timeInfo;
    for (int i = 0; i < 3; i++) {
      if (getLocalTime(&timeInfo)) {
        if (timeInfo.tm_year >= 120) {
          // 等待下一秒交界
          time_t rawtime;
          time(&rawtime);
          while (time(nullptr) == rawtime) {
            delay(1);
          }

          getLocalTime(&timeInfo);

          // ⏱ 記錄 esp_timer 與 NTP 對應秒基準
          time_t ntpEpoch;
          time(&ntpEpoch);
          ntpSyncedMicros = esp_timer_get_time();
          ntpSyncedTime = ntpEpoch;

          // 🔁 僅開機時進行一次「整分鐘」對齊，確保60秒循環乾淨
          delayUntilAlignedRTCWrite();  // 等待下一個整分鐘起點

          RtcDateTime ntpTime(
              timeInfo.tm_year + 1900,
              timeInfo.tm_mon + 1,
              timeInfo.tm_mday,
              timeInfo.tm_hour,
              timeInfo.tm_min,
              timeInfo.tm_sec);
          rtc.SetDateTime(ntpTime);

          digitalWrite(wifiStatusLED, HIGH);
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
  
    if (now.Year() < 2020) {
      Serial.println("❗ RTC 時間異常（小於 2020），請確認 RTC 電池或是否已初始化");
    }
  } else {
    Serial.println("🛑 RTC 時間無效，無法取得正確時間！");
  }
}


  // 初始化發波器
  set_fix();
  ledcSetup(ledChannel, 40000, 8);
  ledcAttachPin(ledPin, ledChannel);

  Serial.println("✅ 初始化完成，開始發射計時波訊號");
}

void delayUntilAlignedRTCWrite() {
  struct timeval tv;
  gettimeofday(&tv, NULL);  // 取得目前秒與微秒

  int64_t micros_now = tv.tv_sec * 1000000LL + tv.tv_usec;
  int64_t micros_next_minute = ((tv.tv_sec / 60) + 1) * 60 * 1000000LL;
  int64_t delay_us = micros_next_minute - micros_now;

  Serial.printf("⏱ 等待整分鐘對齊：%.3f 秒...\n", delay_us / 1000000.0);

  if (delay_us >= 1000) {
    delay(delay_us / 1000);                  // 毫秒部分
    delayMicroseconds(delay_us % 1000);      // 微秒部分
  } else {
    delayMicroseconds(delay_us);
  }

  Serial.println("🎯 已對齊整分鐘起點");
}

void loop() {
  struct tm timeInfo;

  // 判斷是否成功同步過 NTP（有微秒基準）
  if (ntpSyncedMicros > 0) {
    // NTP 模式：用 esp_timer + UTC 秒推算
    while (true) {
      uint64_t nowMicros = esp_timer_get_time();
      uint32_t offset = (nowMicros - ntpSyncedMicros) % 1000000UL;
      if (offset < 1000) break;
      delayMicroseconds(100);
    }

    uint64_t nowMicros = esp_timer_get_time();
    time_t currentSecond = ntpSyncedTime + ((nowMicros - ntpSyncedMicros) / 1000000ULL);
    localtime_r(&currentSecond, &timeInfo);
  } else {
    // RTC fallback 模式：直接使用 RTC 模組時間
    RtcDateTime now = rtc.GetDateTime();
    if (!now.IsValid()) {
      Serial.println("🛑 RTC 時間無效，跳過此次發波");
      delay(1000);
      return;
    }

    // 手動轉換成 struct tm 結構（避免 localtime_r 時區誤導）
    timeInfo.tm_year = now.Year() - 1900;
    timeInfo.tm_mon  = now.Month() - 1;
    timeInfo.tm_mday = now.Day();
    timeInfo.tm_hour = now.Hour();
    timeInfo.tm_min  = now.Minute();
    timeInfo.tm_sec  = now.Second();
    timeInfo.tm_wday = now.DayOfWeek();
  }

// 時區小時（例如 +9、+8）要從 timeZoneOffset 計算
int tzHour = timeZoneOffset / 3600;
// 轉換顯示用的台灣時間（+8）
time_t baseTime = mktime(&timeInfo);  // 系統時區時間 → UTC
time_t taiwanTime = baseTime - timeZoneOffset + (8 * 3600);
struct tm fixedTaiwanTime;
localtime_r(&taiwanTime, &fixedTaiwanTime);

// 顯示：台灣時間、原始發送時間、來源字樣、時區偏移
displayTimeOnLCD(fixedTaiwanTime, timeInfo, ntpSyncedMicros > 0 ? "NTP" : "RTC", tzHour);


  // 執行發波
  printAndSendJJY(timeInfo);
  delay(5);
}


void printAndSendJJY(struct tm &timeInfo) {
  // 顯示目前時間
  char buf[64];
  snprintf(buf, sizeof(buf), "⏰ %04d/%02d/%02d %02d:%02d:%02d",
           timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
           timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  Serial.println(buf);

  // 計算 tm_yday
  static const int daysInMonth[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  timeInfo.tm_yday = timeInfo.tm_mday - 1;
  for (int i = 0; i < timeInfo.tm_mon; ++i) {
    timeInfo.tm_yday += daysInMonth[i];
  }
  int year = timeInfo.tm_year + 1900;
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    if (timeInfo.tm_mon > 1) timeInfo.tm_yday += 1;
  }

  // 處理閏秒
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

  // 發送 JJY 時碼
  Serial.printf("📡 開始發送時間碼：從 %02d 秒起，預計長度 %d 秒\n", se, 60 + sh - se);
  char t[64];
  for (int i = se; i < 60 + sh && i < 62; ++i) {
    if (sg[i] != -1 && sg[i] != 0 && sg[i] != 1 && sg[i] != 255) {
      Serial.printf("⚠️ sg[%d] 值異常：%d，自動修正為 0\n", i, sg[i]);
      sg[i] = 0;
    }

    snprintf(t, sizeof(t), "%02d ", sg[i]);
    Serial.print(t);

      // 每 10 個一換行
    if ((i - se + 1) % 10 == 0) {
    Serial.println();
  }

    switch (sg[i]) {
      case -1:
      case 255: mark(); break;
      case 0: zero(); break;
      case 1: one(); break;
    }
  }
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

void displayTimeOnLCD(const struct tm &fixedTaiwanTime, const struct tm &rawTimeInfo, const char* sourceLabel, int tzHour) {
  char line1[17];  // 第 1 行：NTP 23:59 TAIWAN
  char line2[17];  // 第 2 行：Send 22:59 TZ+9

  snprintf(line1, sizeof(line1), "%s %02d:%02d TAIWAN",
           sourceLabel,
           fixedTaiwanTime.tm_hour, fixedTaiwanTime.tm_min);

  snprintf(line2, sizeof(line2), "Send %02d:%02d TZ%+d",
           rawTimeInfo.tm_hour, rawTimeInfo.tm_min,
           tzHour);

  lcd.clear();
  lcd.setCursor((16 - strlen(line1)) / 2, 0);
  lcd.print(line1);

  lcd.setCursor((16 - strlen(line2)) / 2, 1);
  lcd.print(line2);
}

