# esp32_jjy40kHz_with_rtcDS3231.ino  
## ESP32 JJY 40kHz Transmitter with DS3231 RTC & LCD Display (ver.0.12)

> A robust and extended version of the original ESP32 JJY simulator, upgraded with DS3231 RTC backup, LCD1602 display, WiFiManager config UI, microsecond-aligned NTP time sync, and customizable auto-resync interval.

This project simulates a JJY 40kHz radio transmitter using ESP32, with precise time synchronization via NTP and fallback to DS3231 RTC. A 16x2 LCD shows current time and transmission status. Configuration is made easy using WiFiManager for timezone, WiFi credentials, and NTP resync interval.

---

## 🔧 Features

- 🛰️ **WiFi + NTP Time Synchronization**  
  On boot, the device attempts to connect to known WiFi and sync with NTP servers (Taiwan, Google, Pool). Microsecond-level precision is applied for second alignment.

- 🔁 **Auto NTP Resync with RTC Fallback**  
  A configurable interval (default 6 hours) triggers periodic resynchronization. If WiFi/NTP fails, the system automatically reverts to DS3231 RTC for stable timekeeping.

- 🕒 **RTC Fallback with DS3231**  
  Ensures continued operation even in offline environments using RTC hardware time.

- ⚙️ **WiFiManager Web Configuration**  
  Auto-starts "JJY_Config" AP if WiFi is not available. Users can set WiFi, timezone (+8, +9, etc.), and resync interval (in hours) via web page.

- 📶 **JJY 40kHz Signal Generation**  
  Uses GPIO26 with LEDC PWM output at 40kHz to transmit JJY standard time codes encoded in pulse width.

- 📺 **LCD1602 I2C Display Support**  
  - Top row: Taiwan time (fixed UTC+8) + sync source (NTP or RTC)  
  - Bottom row: Transmit time + current timezone setting

- 🧠 **Error Handling & Logging**  
  Serial monitor provides detailed debug output: NTP retries, RTC validity, sg[] integrity, fallback events, and more.

---

## 🖥️ Hardware Requirements

| Component        | Notes                                 |
|------------------|----------------------------------------|
| ESP32 Dev Board  | e.g., NodeMCU-32S or DevKit v1         |
| DS3231 RTC       | I²C on GPIO17 (SDA) and GPIO16 (SCL)   |
| LCD1602 Display  | I²C on GPIO21 (SDA) and GPIO22 (SCL)   |
| Loop Antenna     | Connected via 220Ω to GPIO26 and GND   |
| (Optional) LED   | GPIO2 lights up when WiFi is connected |

---

## 🗂️ Pin Assignments

| Purpose          | GPIO | Description                  |
|------------------|------|------------------------------|
| PWM Output (TX)  | 26   | 40kHz signal to antenna       |
| WiFi Status LED  | 2    | Lights when WiFi connected    |
| LCD I2C SDA/SCL  | 21/22| Default I2C port (Wire)       |
| RTC I2C SDA/SCL  | 17/16| Separate I2C port (I2C1)      |

---

## 🚀 How to Use

1. Connect DS3231 RTC and LCD1602 as described above  
2. Connect antenna (loop wire) to GPIO26 via 220Ω to GND  
3. Upload the sketch via Arduino IDE (baud rate: 115200)  
4. On first boot, the device will enter "JJY_Config" AP mode  
5. Use browser to connect, configure WiFi, timezone (e.g., +8 for Taiwan), and optional resync interval (in hours)  
6. Once configured, it will auto sync time and begin transmission  

---

## 📡 JJY Signal Format (Simplified)

| Type         | Pulse Width | Meaning        |
|--------------|-------------|----------------|
| `-1` / `255` | 0.2 sec     | Minute marker  |
| `0`          | 0.8 sec     | Bit 0          |
| `1`          | 0.5 sec     | Bit 1          |

---

## 📦 Future Plans

- ⏳ Minute offset support (e.g., +8:15)  
- 🔘 Button-controlled transmission mode  
- 🔁 BPC (China 68.5kHz) mode toggle

---

## 👨‍💻 Authors

- **Original version** by Nash (Shuji009), 2024/05/16  
  https://github.com/shuji009/ESP32_jjy_40kHz  
- **Modified and enhanced** by **chimungwu**, 2025/05/10  

---

## 📜 License

MIT License (unless otherwise noted)
