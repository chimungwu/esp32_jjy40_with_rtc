/*
  📡 JJY/BPC 時碼模擬器 with RTC 與 LCD 顯示
  ------------------------------------------
  裝置功能：
    - 使用 ESP32 模擬日本 JJY（40kHz）與中國 BPC（68.5kHz）電波時碼
    - 支援 WiFiManager 自動連線與參數設定
    - 可切換模式（JJY / BPC）並設定時區（例如 +8 代表台灣）
    - 成功連線 Wi-Fi 後會自動透過 NTP 對時並寫入 RTC（DS3231）
    - 若無法連線 Wi-Fi，則改由 RTC 提供時間來源
    - 使用 LCD1602 I2C 顯示時間與模式（NTP/RTC 與 JJY/BPC）
    - 每秒以中斷方式產生發波 pattern，輸出至線圈模擬實際電波訊號

  硬體需求：
    - ESP32（開發板：Node32s / DevKit v1 / ESP32-C3 均可）
    - RTC 實時時鐘模組（DS3231，建議使用 I2C 模組）
    - LCD1602 顯示器（I2C 模組，預設地址 0x27）
    - 發波線圈（建議使用 3~10 圈銅線 + 220Ω 阻值）
    - 蜂鳴器（選用）
    - 狀態 LED（建議使用 330Ω 限流電阻）

  腳位配置（可依需求調整）：
    - GPIO26：訊號輸出（40kHz or 68.5kHz），接至線圈
    - GPIO25：狀態燈（發波時亮）
    - GPIO27：蜂鳴器輸出（可選）
    - GPIO21 / GPIO22：LCD 顯示（I2C）
    - GPIO17 / GPIO16：RTC DS3231（I2C，使用 Wire1）

  注意事項：
    - LCD 與 RTC 必須分開使用兩組 I2C 腳位（避免衝突）
    - WiFiManager 啟動時如未連線，會開啟名為 "JJY_Config" 的熱點
    - 可透過手機連線設定 Wi-Fi、時區（+9, +8）與模式（JJY / BPC）
    - 每次開機會嘗試對時，失敗則使用 RTC 作為備援來源

  作者：chimungwu
  基礎參考：niseJJY 專案 by taroh（2021）
  修改日期：2025/05/10
  版本：ver.0.1
*/


// ========================================
// 📦 系統函式庫
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>              // WiFiManager 自動 WiFi 配對與參數設定
#include <Wire.h>
#include <RtcDS3231.h>                // RTC DS3231 函式庫
#include <LiquidCrystal_I2C.h>        // LCD 1602 顯示模組

// ========================================
// 📺 LCD 與 🕰️ RTC 初始化
LiquidCrystal_I2C lcd(0x27, 16, 2);   // LCD 使用預設地址 0x27
TwoWire I2C_RTC(1);                   // RTC 使用 Wire1 (獨立腳位)
RtcDS3231<TwoWire> rtc(I2C_RTC);

// ========================================
// 🌐 WiFiManager 參數
long timeZoneOffset = 9 * 3600L;      // 預設時區為 UTC+9（日本）
bool isBPC = false;                   // 模式旗標：預設為 JJY，使用者可選擇 BPC
bool ntpSuccess = false;             // 紀錄是否成功取得 NTP

// ========================================
// ⏱️ NTP 對時基準
uint64_t ntpSyncedMicros = 0;        // 微秒基準點（成功對時時的時間戳）
time_t ntpSyncedTime = 0;            // 成功對時的 UTC 秒
uint64_t microsBase = 0;      // 開機時的微秒基準（esp_timer）
time_t timeBaseSec = 0;       // 開機時的秒數（來自 RTC 或 NTP）

// ========================================
// 🧠 發波與狀態指示變數
int8_t *bits60 = nullptr;            // 發波資料陣列（來自 JJY 或 BPC）
int8_t *secpattern = nullptr;        // 每秒 0.1 秒切換對應的 pattern
int stationIndex = 0;                // 發波模式編號（0: JJY, 1: BPC）

// LED/信號腳位設定（你可根據開發板調整）
#define PIN_RADIO    26              // 發波腳位（接 220Ω → 線圈 → GND）
#define PIN_BUZZ     27              // 蜂鳴器（可選）
#define PIN_LED      25              // 狀態 LED（顯示當前發波振幅）
#define AMPDIV 10    // 0.1 秒切換間隔
#define SSECDIV 10   // 每秒 10 次（與 AMPDIV 相同）


// ========================================
// 發波中使用的變數（niseJJY 原始結構）
hw_timer_t *tm0 = NULL;
volatile SemaphoreHandle_t  timerSemaphore;
portMUX_TYPE  timerMux = portMUX_INITIALIZER_UNLOCKED;

volatile uint32_t buzzup = 0;
int istimerstarted = 0;

int radioc = 0;     // 計算 radio 中斷週期
int ampc = 0;       // 每秒的 0.1 秒切換計數器（0~9）
int tssec = 0;      // 秒切換計數器（0~9）

int radioout = 0;
int buzzout = 0;
int ampmod = 0;
int buzzsw = 1;

int tm0cycle = 1000;   // 中斷頻率，會根據發波頻率設定
int radiodiv = 80;     // 發波振幅變化間隔除數（ex: 40kHz 對應 80KHz/80MHz）

// ========================================
// 時間暫存區
time_t now;
struct tm nowtm;

// ========================================
// 發波模式表（外部函式將會指向這裡）
extern int8_t bits_jjy[], bits_bpc[];
extern int8_t sp_jjy[], sp_bpc[];
extern void mb_jjy(), mb_bpc();
int8_t* st_bits[] = { bits_jjy, bits_bpc };
int8_t* st_sp[]   = { sp_jjy,  sp_bpc };
void (*st_makebits[])(void) = { mb_jjy, mb_bpc };
bool rtcValid = true;  // 預設 RTC 有效，若初始化失敗將設為 false
// ========================================
// 📡 模式常數定義
#define SN_JJY_E  0   // 日本 JJY 東日本（40kHz）
#define SN_BPC    1   // 中國 BPC（68.5kHz）

// ========================================
// 🧠 發波編碼函式指標
void (*makebitpattern)(void);  // 指向 mb_jjy() 或 mb_bpc()
// 在 setstation() 中正確設定：

// 👉 如果還沒寫這些 function，可以先 forward 宣告（避免錯誤）
void stoptimer();
void IRAM_ATTR onTimer();
// === JJY / BPC 專用編碼樣板與模式 ===
#define SP_0 0
#define SP_1 1
#define SP_M 2
#define SP_2 2
#define SP_3 3
#define SP_M4 4

int8_t bits_jjy[60] = {
  SP_M, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_M
};

int8_t bits_bpc[60] = {
  SP_M4, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0,
  SP_M4, SP_1, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0,
  SP_M4, SP_2, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0,
  SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0, SP_0
};

int8_t sp_jjy[] = {
  1, 1, 1, 1, 1, 1, 1, 1, 0, 0,   // SP_0
  1, 1, 1, 1, 1, 0, 0, 0, 0, 0,   // SP_1
  1, 1, 0, 0, 0, 0, 0, 0, 0, 0    // SP_M
};

int8_t sp_bpc[] = {
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1,   // SP_0
  0, 0, 1, 1, 1, 1, 1, 1, 1, 1,   // SP_1
  0, 0, 0, 1, 1, 1, 1, 1, 1, 1,   // SP_2
  0, 0, 0, 0, 1, 1, 1, 1, 1, 1,   // SP_3
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0    // SP_M4
};

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("🔧 啟動中...");

  // 初始化 LCD 顯示器（I2C 0x27, 16x2）
  lcd.begin();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("JJY/BPC Transmit");

  // 初始化 RTC（使用 I2C_RTC, GPIO16=SCL, GPIO17=SDA）
  
  I2C_RTC.begin(17, 16);
  rtc.Begin();
  if (!rtc.IsDateTimeValid()) {
    Serial.println("❌ RTC 無效或未初始化");
    lcd.setCursor(0, 1);
    lcd.print("RTC Error");
    rtcValid = false;
  } else {
    Serial.println("✅ RTC 初始化完成");
    rtcValid = true;
  
  // ⏱️ 第一次嘗試 NTP 對時：
  // 若 RTC 有效，則先進行 NTP 對時並寫入 RTC，
  // 讓後續即使 WiFiManager 尚未設定，也有準確時間來源可用
  syncNTPtoRTC();  
    }

  // WiFiManager 設定（自訂時區與模式）
  WiFiManagerParameter tzParam("tz", "時區 (如+8)", "+9", 4);
  WiFiManagerParameter modeParam("mode", "模式 (JJY/BPC)", "JJY", 5);
  WiFiManager wm;
  wm.setTimeout(60);
  wm.setConnectTimeout(15);
  wm.addParameter(&tzParam);
  wm.addParameter(&modeParam);

  bool res = wm.autoConnect("JJY_Config");
  if (!res) {
    Serial.println("❌ WiFiManager 設定失敗");
    lcd.setCursor(0, 1);
    lcd.print("WiFi FAIL");
  } else {
    Serial.println("✅ WiFiManager 成功連線");

    // 解析時區
    const char* tzInput = tzParam.getValue();
    if (tzInput && strlen(tzInput) > 0) {
      timeZoneOffset = atoi(tzInput) * 3600;
      Serial.printf("🌐 使用者時區：UTC %+d\n", timeZoneOffset / 3600);
    }
  // 設定開機時基準秒與微秒時間
    RtcDateTime rtcNow = rtc.GetDateTime();
    timeBaseSec = rtcNow.Epoch32Time();    // 用 RTC 的 Epoch 秒作為起點
    microsBase = esp_timer_get_time();     // 用現在的微秒時間作為起點
  
  // 解析模式（JJY / BPC）
    const char* modeInput = modeParam.getValue();
    if (modeInput && strcmp(modeInput, "BPC") == 0) {
      isBPC = true;
    } else {
      isBPC = false;
    }
    Serial.printf("🛰️ 模式設定為：%s\n", isBPC ? "BPC（大陸碼）" : "JJY（日本碼）");
  }

  // 設定電台編碼模式與頻率
  setstation(isBPC ? SN_BPC : SN_JJY_E);

  // 顯示模式在 LCD 上
  lcd.setCursor(0, 1);
  lcd.print(isBPC ? "Mode: BPC " : "Mode: JJY ");

  // 🌐 第二次嘗試 NTP 對時：
// 當 WiFiManager 成功連線並取得最新設定（如時區、模式）後，
// 再次進行 NTP 對時，以反映最新時區並更新 RTC 時間
  syncNTPtoRTC();
starttimer();  // 啟動中斷發波
}

void loop() {
  if (!rtcValid) {
    lcd.setCursor(0, 1);
    lcd.print("RTC FAIL");
    delay(1000);
    return;
  }

  int buzzup2 = 0;
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE) {
    portENTER_CRITICAL(&timerMux);
    if (buzzup > 0) {
      buzzup2 = buzzup;
      buzzup = 0;
    }
    portEXIT_CRITICAL(&timerMux);
  }

  if (buzzup2 > 0) {
    ampc++;
    if (ampc >= AMPDIV) {
      ampc = 0;
      tssec++;
      if (tssec >= 10) {
        tssec = 0;

        uint64_t nowMicros = esp_timer_get_time();
        int64_t elapsedMicros = nowMicros - microsBase;

        if (elapsedMicros >= 1000000) {
          microsBase += 1000000;
          timeBaseSec += 1;

          localtime_r(&timeBaseSec, &nowtm);
          calculateDOY(nowtm);

          if (nowtm.tm_sec == 0) {
            makebitpattern();
            printbits60();
          }

          char buf1[17], buf2[17];
          snprintf(buf1, sizeof(buf1), "%02d:%02d:%02d %04d",
                   nowtm.tm_hour, nowtm.tm_min, nowtm.tm_sec, nowtm.tm_year + 1900);
          snprintf(buf2, sizeof(buf2), "%s %s UTC%+d",
                   isBPC ? "BPC" : "JJY",
                   ntpSuccess ? "NTP" : "RTC",
                   timeZoneOffset / 3600);

          lcd.setCursor(0, 0);
          lcd.print(buf1);
          lcd.setCursor(0, 1);
          lcd.print(buf2);
        }
      }
    }

    ampchange();
  }

  delay(1);  // 防止 Watchdog timeout
}

void mb_jjy() {
  Serial.print("🧭 編碼格式：JJY\n");

  // 分鐘（MIN）
  binarize(nowtm.tm_min / 10, 1, 3);
  binarize(nowtm.tm_min % 10, 5, 4);

  // 小時（HOUR）
  binarize(nowtm.tm_hour / 10, 12, 2);
  binarize(nowtm.tm_hour % 10, 15, 4);

  // 年初至今的天數（DOY）
  int yday100 = nowtm.tm_yday / 100;
  int yday10 = (nowtm.tm_yday % 100) / 10;
  int yday1 = nowtm.tm_yday % 10;
  binarize(yday100, 22, 2);
  binarize(yday10, 25, 4);
  binarize(yday1, 30, 4);

  // 奇偶檢查位（PA1、PA2）
  bits60[36] = parity(12, 7);  // 小時奇偶
  bits60[37] = parity(1, 8);   // 分鐘奇偶

  // 年份（僅兩位）
  int y = nowtm.tm_year - 100;
  binarize(y / 10, 41, 4);
  binarize(y % 10, 45, 4);

  // 星期
  binarize(nowtm.tm_wday, 50, 3);
}
void mb_bpc() {
  Serial.print("🧭 編碼格式：BPC\n");

  // 時（0~11）與 AM/PM（放在第 10 位）
  quadize(nowtm.tm_hour % 12, 3, 2);

  // 分
  quadize(nowtm.tm_min, 5, 3);

  // 星期
  quadize(nowtm.tm_wday, 8, 2);

  // P3：AM/PM + 奇偶校驗
  bits60[10] = (nowtm.tm_hour >= 12 ? 2 : 0) + qparity(3, 7);

  // 日、月、年
  quadize(nowtm.tm_mday, 11, 3);
  quadize(nowtm.tm_mon + 1, 14, 2);
  quadize(nowtm.tm_year % 100, 16, 3);

  // P4：DMY 奇偶校驗
  bits60[19] = qparity(11, 8);

  // 整體時間碼複製（2~19）重複兩次
  for (int i = 2; i < 20; i++) {
    bits60[20 + i] = bits60[i];
    bits60[40 + i] = bits60[i];
  }
}
void setstation(int station) {
  Serial.printf("📡 切換模式為：%s\n", station == 1 ? "BPC" : "JJY");

  bits60      = st_bits[station];
  secpattern  = st_sp[station];
  makebitpattern = st_makebits[station];
  stationIndex = station;

  // 頻率設定（影響中斷計時器與後續波形控制）
  if (station == 1) { // BPC 使用 68.5kHz
    tm0cycle = 80000 / 137;
    radiodiv = 137;
  } else {            // JJY 使用 40kHz
    tm0cycle = 80000 / 80;
    radiodiv = 80;
  }

  Serial.printf("⚙️ 發波頻率設定完成：%.1fkHz\n", radiodiv / 2.0);
}
void starttimer() {
  stoptimer();  // 先確保舊的 timer 終止

  ampc = 0;
  radioc = 0;
  timerSemaphore = xSemaphoreCreateBinary();  // 建立 semaphore

  tm0 = timerBegin(0, tm0cycle, true);  // 設定 timer 頻率除數
  timerAttachInterrupt(tm0, &onTimer, true);  // 附加中斷處理函式
  timerAlarmWrite(tm0, 1, true);  // 每次中斷時間（單位與 prescaler 有關）
  timerAlarmEnable(tm0);         // 啟用中斷
  istimerstarted = 1;
  Serial.println("✅ 中斷計時器已啟用");
}
void stoptimer() {
  if (istimerstarted) {
    timerEnd(tm0);
    istimerstarted = 0;
    Serial.println("🛑 中斷計時器已停止");
  }
}

void ampchange() {
  int currentSecond = nowtm.tm_sec;
  if (currentSecond < 0 || currentSecond >= 60) return;

  int8_t symbol = bits60[currentSecond];
  ampmod = secpattern[symbol * 10 + tssec];

  if (ampmod) {
    digitalWrite(PIN_RADIO, HIGH);
    digitalWrite(PIN_LED, HIGH);  // 狀態燈亮
  } else {
    digitalWrite(PIN_RADIO, LOW);
    digitalWrite(PIN_LED, LOW);   // 狀態燈滅
  }
}
    
void IRAM_ATTR onTimer() {
  // 將 PIN_RADIO 切換 ON / OFF（依據 ampmod）
  if (!radioout && ampmod) {
    radioout = 1;
    digitalWrite(PIN_RADIO, HIGH);
  } else {
    radioout = 0;
    digitalWrite(PIN_RADIO, LOW);
  }

  radioc++;
  if (radioc >= radiodiv) {
    radioc = 0;

    portENTER_CRITICAL_ISR(&timerMux);
    buzzup++;  // 通知主程式進行下一個 0.1 秒步進
    portEXIT_CRITICAL_ISR(&timerMux);

    xSemaphoreGiveFromISR(timerSemaphore, NULL);
  }
}

void binarize(int value, int pos, int len) {
  for (int i = pos + len - 1; i >= pos; i--) {
    bits60[i] = value & 1;
    value >>= 1;
  }
}

void quadize(int value, int pos, int len) {
  for (int i = pos + len - 1; i >= pos; i--) {
    bits60[i] = value & 3;
    value >>= 2;
  }
}

int parity(int pos, int len) {
  int sum = 0;
  for (int i = pos; i < pos + len; i++) sum += bits60[i];
  return sum % 2;
}

int qparity(int pos, int len) {
  int sum = 0;
  for (int i = pos; i < pos + len; i++) {
    sum += (bits60[i] & 1) + ((bits60[i] & 2) >> 1);
  }
  return sum % 2;
}

void calculateDOY(struct tm &t) {
  static const int daysInMonth[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  int yday = t.tm_mday - 1;
  for (int i = 0; i < t.tm_mon; ++i) yday += daysInMonth[i];

  // 若為閏年且超過2月，加一天
  int year = t.tm_year + 1900;
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    if (t.tm_mon > 1) yday += 1;
  }

  t.tm_yday = yday;
}


void syncNTPtoRTC() {
  Serial.println("🌐 嘗試透過 NTP 同步時間...");

  configTime(timeZoneOffset, 0, "pool.ntp.org", "time.google.com", "time.stdtime.gov.tw");

  struct tm timeInfo;
  bool synced = false;

  for (int i = 0; i < 5; i++) {
    if (getLocalTime(&timeInfo)) {
      if (timeInfo.tm_year > 120) {
        synced = true;
        break;
      }
    }
    Serial.printf("⌛ 第 %d 次 NTP 嘗試失敗，重試中...\n", i + 1);
    delay(1000);
  }

  if (!synced) {
    Serial.println("❌ NTP 同步失敗，使用 RTC");
    ntpSuccess = false;
    return;
  }

  // 等待整秒交界點，提升 RTC 對時精度
  time_t rawTime;
  time(&rawTime);
  while (time(nullptr) == rawTime) {
    delay(1);
  }

  getLocalTime(&timeInfo);  // 再次抓整秒的時間

  // 寫入 RTC
  RtcDateTime ntpTime(
    timeInfo.tm_year + 1900,
    timeInfo.tm_mon + 1,
    timeInfo.tm_mday,
    timeInfo.tm_hour,
    timeInfo.tm_min,
    timeInfo.tm_sec
  );
  rtc.SetDateTime(ntpTime);
  ntpSuccess = true;

  Serial.printf("✅ NTP 同步完成，寫入 RTC：%04d-%02d-%02d %02d:%02d:%02d\n",
    ntpTime.Year(), ntpTime.Month(), ntpTime.Day(),
    ntpTime.Hour(), ntpTime.Minute(), ntpTime.Second()
  );
}

void printbits60() {
  Serial.println("🧾 時碼 bits60[]：");
  for (int i = 0; i < 60; i++) {
    Serial.printf("%02d:%d  ", i, bits60[i]);
    if ((i + 1) % 10 == 0) Serial.println();
  }
}
