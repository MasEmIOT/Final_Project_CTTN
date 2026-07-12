# PHASE 2 — Hoàn thiện Node (ESP32-S3) ✅

Tài liệu này mô tả những gì đã thêm vào firmware Node và cách dùng.

## 1. File mới / thay đổi
| File | Vai trò |
|---|---|
| `main/ds3231.[ch]` | Driver RTC DS3231 (I2C0, 0x68) → nhãn thời gian thực cho anti-replay & offline |
| `main/mq135.[ch]` | Driver MQ135 (ADC2_CH5/IO16) → NH₃, CH₄ (ppm), có hiệu chuẩn Ro |
| `main/mhz19.[ch]` | Driver CO₂ UART (MH-Z19, UART1 RX=IO5/TX=IO4) |
| `main/actuator.[ch]` | Điều khiển 3 kênh Act1=IO38, Act2=IO42, Act3=IO45 (relay/MOSFET) |
| `main/fsm.[ch]` | Máy trạng thái SAFE/WARN/EMERGENCY (THI + nhiệt/NH₃/CO₂ + hysteresis), đo `decide_us` |
| `main/wifi_node.[ch]` | WiFi STA — **chỉ bật khi OTA** |
| `main/ota_update.[ch]` | OTA `esp_https_ota` + báo tiến trình JSON + rollback |
| `main/packet.[ch]` | **Mở rộng v4**: thêm nh3/co2/ch4/thi/decide/act/mac; thêm **gói lệnh downlink** `cmd_payload_t` (điều khiển actuator + OTA) |
| `main/crypto.[ch]` | Thêm `crypto_get_mac`, `crypto_node_id_from_mac`, `crypto_derive_key` |
| `main/board_pins.h` | Thêm chân MQ135/DS3231-SQW/CO2-UART/Act1-3 |
| `partitions_ota.csv` | Bảng phân vùng 2 slot (ota_0/ota_1)+otadata cho OTA |
| `sdkconfig.defaults` | Bật custom partition + rollback + OTA HTTP/HTTPS + CA bundle |
| `main/Kconfig.projbuild` | Thêm `NODE_ID_FROM_MAC`, `NODE_OTA_WIFI_SSID/PASS` |
| `main/CMakeLists.txt` | Thêm source + components (esp_adc, esp_wifi, esp_https_ota, app_update…) |

## 2. Tính năng đã thêm (ánh xạ yêu cầu)
- **Cảm biến mới**: MQ135 (NH₃/CH₄), CO₂ UART (MH-Z19), DS3231 RTC — tự probe, có cờ hợp lệ trong payload.
- **Định danh MAC**: `NODE_ID_FROM_MAC=y` → mỗi mạch tự có Node ID (1..254) từ MAC efuse; MAC 6 byte nằm trong payload **được xác thực** (AEAD) → chống giả mạo định danh.
- **Mã hóa + chống giả mạo**: giữ AES-128-GCM / ASCON-128 (tag 16B = chữ ký xác thực).
- **Chống replay**: gói mang `epoch` từ RTC; **Gateway (Phase 3)** sẽ loại gói nếu `|gw_epoch - epoch| > 5s` (trừ gói `BACKLOG`). Lệnh downlink chống replay bằng `cmd_seq` tăng dần.
- **Actuator + FSM biên**: FSM tính THI, so ngưỡng nhiệt/NH₃/CO₂ (hysteresis chống chattering) → bật Act1-3; đo **`decide_us`** (độ trễ ra quyết định) gửi kèm telemetry.
- **Scheduling nguồn**: MQ135 đọc mỗi chu kỳ (ADC nhanh), CO₂ (UART chậm) đọc luân phiên mỗi 3 chu kỳ; giữ nhịp ngủ light/deep để giảm dòng đỉnh; OTA tắt cảm biến + actuator an toàn.
- **OTA (Ch4.4)**: lệnh LoRa `CMD_OTA + URL` → tạm dừng cảm biến → WiFi → `esp_https_ota` (chunked ~10KB RAM, verify SHA-256) → `esp_restart` → self-check ngoại vi → `mark_valid` hoặc watchdog **rollback**.

## 3. Cách dùng
### Build & nạp
```powershell
cd D:\2025.2\Final2\code\NodeSensor_test
idf.py set-target esp32s3
idf.py menuconfig   # Node Sensor Configuration: NODE_ID_FROM_MAC, OTA WiFi SSID/PASS, chu ky, SF...
idf.py build flash monitor -p COMx
```
> Lần đầu chuyển sang bảng phân vùng OTA nên `idf.py erase-flash` một lần trước khi flash.

### Đọc dữ liệu mới trên HIL
Telemetry JSON của node giờ có thêm:
`sensor.nh3`, `sensor.co2`, `sensor.ch4`, `metrics.thi`, `metrics.decide` (µs), `metrics.act` (bitmask Act1-3).
(EdgeProfiler sẽ hiển thị các trường này sau khi cập nhật ở Phase 5.)

### Hiệu chuẩn MQ135
Để MQ135 warm-up ≥30s ở **không khí sạch**, sau đó (tuỳ chọn) gọi `mq135_calibrate()` để chốt Ro. Có thể thêm lệnh serial để calibrate — sẽ bổ sung nếu bạn cần.

### Điều khiển actuator / OTA (từ Gateway — Phase 3)
Node đã **sẵn sàng nhận** gói lệnh downlink mã hóa:
- `CMD_ACT_SET` (mask+val): bật/tắt tay Act1-3 (chuyển sang manual, FSM tạm ngưng ghi đè).
- `CMD_ACT_AUTO`: trả về điều khiển tự động FSM.
- `CMD_OTA` (ota_url): kích hoạt OTA.
- `CMD_REBOOT`, `CMD_PING`.
Gateway/App sẽ phát các lệnh này ở Phase 3 & 4.

## 4. Lưu ý
- **ADC2 (MQ135)** dùng chung với WiFi → chỉ đọc khi không OTA (đã xử lý: OTA suspend task cảm biến).
- **Gateway hiện tại (cũ) chưa giải mã được payload v4** — sẽ đồng bộ `packet.*`/`crypto.*` sang Gateway trong Phase 3.
- Nếu CO₂ không phải MH-Z19, báo model để đổi `mhz19.c`.
