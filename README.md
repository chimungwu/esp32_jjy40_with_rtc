# ESP32 JJY 40kHz Transmitter with RTC Backup (Modified Version)

> A modified and extended version of the original ESP32 JJY simulator by Nash (Shuji009), now with DS1302 RTC fallback and future expandability.

This project simulates a JJY (Japanese Standard Time) 40kHz radio wave transmitter using an ESP32. It extends the original implementation by adding a DS1302 Real-Time Clock (RTC) module to maintain accurate time even when no internet connection is available.

---

## 🔧 Features

- 🛰️ **WiFi + NTP Time Synchronization**  
  On startup, the ESP32 tries up to 30s to connect to WiFi and synchronize time via NTP.

- 🕒 **RTC Fallback with DS1302**  
  If NTP sync fails, the system falls back to the onboard DS1302 RTC to provide time continuity.

- 📶 **JJY 40kHz Signal Generation**  
  Uses GPIO26 to generate JJY signals via PWM at 40kHz, with pulse width encoding to transmit binary time codes.

- 🔘 **Planned: Button Control for TX**  
  - Short press: Start transmission for 5 minutes  
  - Long press: Cancel transmission

- 📺 **Planned: LCD1602 Display**  
  - Top row: Current time  
  - Bottom row: TX status and sync source (NTP or RTC)

---

## 🖥️ Hardware Requirements

- ESP32-WROOM board  
- DS1302 RTC module with battery  
- 220Ω resistor + 40cm loop antenna  
- (Optional) LCD1602 with I2C  
- (Optional) Momentary push button

---

## 📂 File Structure

esp32-jjy-rtc/
├── esp32_jjy_rtc.ino # Main source file (modified from original ver.0.10)
├── README.md # This documentation


---

## 🧑‍💻 Authors

- **Original version** by Nash (Shuji009), 2024/05/16  
- **Modified and extended** by **chimung**

---

## 📜 License

MIT License (if applicable)

---

## 📡 JJY Signal Format (Simplified)

| Type     | Pulse Width | Meaning       |
|----------|-------------|----------------|
| `-1` / `255` | 0.2 sec     | Mark (minute start) |
| `0`        | 0.8 sec     | Bit value 0   |
| `1`        | 0.5 sec     | Bit value 1   |

---

## 🚀 How to Use

1. Connect DS1302 RTC to GPIO 13 (IO), 14 (SCLK), 15 (RST)  
2. Connect loop antenna to GPIO 26 via 220Ω resistor to GND  
3. Edit `ssid` and `password` in the sketch to match your WiFi credentials  
4. Upload the sketch using Arduino IDE or PlatformIO  
5. Monitor output via Serial Monitor at 115200 baud  

---

> This project provides a reliable JJY simulator platform for experiments and timekeeping, with planned expansion for user interface and manual control.
