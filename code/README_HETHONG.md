# Hệ thống giám sát vi khí hậu chăn nuôi LoRa — Tổng quan & Hướng dẫn

Node ESP32-S3 (edge computing) → **LoRa SX1278 mã hóa** → Gateway ESP32 DevKit (Local Server) →
**App/Web** (React + APK). Kiểm thử bằng **EdgeProfiler** (HIL).

## Tài liệu theo phase
| Phase | Nội dung | File |
|---|---|---|
| 1 | Phân tích + bảng thiếu sót + kế hoạch + pinout | [PHASE1_PHANTICH_KEHOACH.md](PHASE1_PHANTICH_KEHOACH.md) |
| 2 | Node: cảm biến mới, MAC, anti-replay, actuator, OTA | [PHASE2_NODE.md](PHASE2_NODE.md) |
| 3 | Gateway: ESP32 DevKit, OLED, Local Server, bỏ Firebase | [PHASE3_GATEWAY.md](PHASE3_GATEWAY.md) |
| 4 | App/Web + điều khiển actuator (RBAC) | [PHASE4_APP.md](PHASE4_APP.md) |
| 5 | HIL: test OTA + Replay attack + đóng gói .exe | [PHASE5_HIL.md](PHASE5_HIL.md) |

## Sơ đồ đấu nối nhanh
**Node ESP32-S3**: I2C0 SDA8/SCL14 (SHT30, BMP180, BH1750, **DS3231**), MQ135→IO16(ADC),
CO2 UART RX=IO5/TX=IO4, **Act1=IO38 Act2=IO42 Act3=IO45**, LED 39/40/41, DS3231 SQW=IO1.
**Gateway ESP32 DevKit**: LoRa SCK18/MISO19/MOSI23/NSS5/RST27/DIO0 26; OLED SDA21/SCL22.

## Quy trình khởi động end-to-end
1. **Chọn mã hóa** giống nhau ở Node & Gateway: `main/crypto_cfg.h` (`CRYPTO_ALGO_AES` hoặc `CRYPTO_ALGO_ASCON`) + cùng `CRYPTO_KEY`. Cùng tần số/SF LoRa.
2. **Nạp Node**:
   ```powershell
   cd NodeSensor_test
   idf.py set-target esp32s3
   idf.py erase-flash            # lan dau (doi bang phan vung OTA)
   idf.py build flash monitor -p COM<node>
   ```
3. **Nạp Gateway** (sửa WiFi/LoRa trong `LoraGateway/main/gw_config.h`):
   ```powershell
   cd LoraGateway
   idf.py set-target esp32       # BAT BUOC (tu esp32s3 -> esp32)
   idf.py build flash monitor -p COM<gw>
   ```
   Gateway in ra **IP** (cũng trên OLED).
4. **App/Web**:
   ```powershell
   cd ..\app
   npm install
   npm run dev                   # web http://localhost:5173 (nhap IP gateway)
   ```
   Đăng nhập `admin/admin123` → xem dashboard, điều khiển actuator, kích OTA.
   Build APK: xem [app/README.md](../app/README.md).
5. **HIL Tool** (kiểm thử):
   ```powershell
   cd code\HIL_Tool
   .\build_exe.bat              # -> dist\EdgeProfiler.exe
   ```
   Cắm cổng Node/Gateway → chạy các kịch bản (Link/Latency/Crypto/OTA/Replay…).

## Bảo mật (tóm tắt)
- **AEAD** AES-128-GCM / ASCON-128: mã hóa + xác thực (tag 16B chống giả mạo).
- **Định danh MAC**: Node ID tự sinh từ MAC; MAC nằm trong payload được xác thực.
- **Chống replay**: nhãn thời gian RTC + cửa sổ Δt<5s; Gateway loại gói cũ (trừ backlog).
- **Lệnh downlink** (điều khiển/OTA) mã hóa + chống replay bằng `cmd_seq`.
- **OTA**: HTTPS (CA bundle) + verify SHA-256 + rollback tự động (watchdog).

## Điều kiện build (máy của bạn)
- ESP-IDF **v6.x** (dùng PSA crypto, esp_https_ota…). `idf.py` trong PATH.
- **Node.js ≥ 18** cho App; **JDK 17 + Android Studio** để build APK.
- **Python 3.9+** + `pip install -r HIL_Tool/requirements.txt` để chạy/đóng gói HIL.

> Ghi chú: môi trường soạn code không có sẵn idf.py/node nên **chưa build thử**; hãy build trên máy bạn.
> Cảm biến CO₂ mặc định là **MH-Z19** (UART) — đổi `mhz19.c` nếu bạn dùng model khác.
