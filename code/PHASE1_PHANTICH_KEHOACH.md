# PHASE 1 — Phân tích dự án, Bảng thiếu sót & Kế hoạch thực hiện

> Hệ thống giám sát vi khí hậu chăn nuôi: **Node ESP32-S3** (edge computing) → **LoRa SX1278** → **Gateway** → **App/Web**.
> Tài liệu này là kết quả đọc hiểu toàn bộ báo cáo LaTeX (`Bao_Cao/*.tex`) và mã nguồn (`code/`).

---

## 1. Hiện trạng — Code ĐÃ CÓ (khá hoàn thiện)

| Thành phần | Đã có trong code |
|---|---|
| **Node** | SHT30, BMP180, BH1750 (tự probe 2 bus I2C); LoRa SX1278 (SPI polling); AEAD **AES-128-GCM (PSA)** + **ASCON-128**; đóng gói `packet_seal/open` (header=AAD, nonce ngẫu nhiên, tag 16B); ACK 2 chiều + đo RTT + ước lượng khoảng cách từ RSSI; **hybrid light/deep sleep**; **watchdog** + chân test; offline **store-and-forward** (ring buffer RTC); bộ lọc riêng từng tín hiệu (EMA/MA/Median); telemetry JSON qua USB; FreeRTOS run-time stats. |
| **Gateway** | Nhận LoRa → giải mã → ACK (kèm RSSI/SNR + epoch SNTP) → forward JSON USB + **Firebase**; mô phỏng lỗi từ HIL (inject_fault/jam/force_fsm). |
| **HIL Tool** | EdgeProfiler 2.0 (PyQt5, 2208 dòng): đa cổng/đa node, đồ thị realtime, tiles, RTOS profiler, test sequencer (link/latency/crypto/bandwidth/sensor/memory/power/watchdog/offline), record/replay `.eplog`, xuất HTML/Excel/XML, đóng gói `.exe` (có sẵn `build_exe.bat` + `.spec`). |

---

## 2. BẢNG THIẾU SÓT — Báo cáo hứa vs. Code thực tế

### 2.1 NODE
| # | Báo cáo mô tả (chương/mục) | Code hiện tại | Trạng thái | Việc cần làm |
|---|---|---|---|---|
| N1 | Cảm biến khí **MQ135 → NH₃** (payload có `NH3`, FSM dùng `d->nh3`) — Ch3/Ch4.2.5 | Không có | ❌ Thiếu | Driver ADC oneshot trên **IO16** |
| N2 | **RTC DS3231** cấp nhãn thời gian cho anti-replay & store-and-forward — Ch4.2.5, 4.3.4 | Chỉ đồng bộ epoch mềm qua ACK | ❌ Thiếu phần cứng | Driver I2C DS3231 (SDA8/SCL14), dùng **SQW→IO1** làm nguồn wake |
| N3 | **Cảm biến CO₂ UART** (payload có `CO2`) | Không có | ❌ Thiếu | Driver UART (RX=IO5, TX=IO4), giả định **MH-Z19** |
| N4 | **Khối chấp hành Act1–Act3** điều khiển bởi FSM (quạt/phun sương), <50ms — Ch4.2.8, `lst:fsm` | FSM chỉ tính, **không** có GPIO actuator | ⚠️ Một nửa | GPIO **Act1=IO38, Act2=IO42, Act3=IO45** + FSM có hysteresis |
| N5 | **Đo độ trễ ra quyết định edge** (proc để quyết định) | Có `proc_us` (đọc+đóng gói) nhưng chưa tách riêng "thời gian FSM ra quyết định" | ⚠️ Một nửa | Thêm `decide_us` (thời gian FSM) + hiển thị lên HIL |
| N6 | **Scheduling nguồn** — đọc cảm biến luân phiên/deep-light hợp lý tránh sụt áp | Có sleep, nhưng đọc **tất cả** cảm biến mỗi chu kỳ | ⚠️ Cần cải thiện | Bộ lập lịch đọc luân phiên (cảm biến chậm/ngốn dòng đọc thưa hơn) + warm-up MQ135 |
| N7 | **Định danh bằng MAC** mỗi node | Dùng `CONFIG_NODE_ID` cứng | ❌ Thiếu | Đọc MAC efuse → `node_id` + **dẫn xuất khóa riêng theo MAC** |
| N8 | **Mã hóa + chữ ký chống giả mạo** | AEAD tag đã có (chống giả mạo). **Nonce ngẫu nhiên → KHÔNG chống replay** | ⚠️ Thiếu anti-replay | Anti-replay theo **timestamp** (Δt<5s) dùng field `epoch`; loại trừ gói `BACKLOG` |
| N9 | **OTA** Hybrid qua LoRa downlink + HTTPS + rollback — **Ch4.4** | Không có | ❌ Thiếu hoàn toàn | `partitions.csv` 2 slot + WiFi (bật khi OTA) + `esp_https_ota` + rollback state machine |

### 2.2 GATEWAY
| # | Báo cáo / yêu cầu | Code hiện tại | Trạng thái | Việc cần làm |
|---|---|---|---|---|
| G1 | Chuyển sang **ESP32 DevKit** | Đang là ESP32-S3, pin trùng node | ❌ | `board_pins.h` mới cho DevKit (xem mục 4) |
| G2 | Tích hợp LoRa SX1278 trên DevKit | Có driver, nhưng pin S3 | ⚠️ | Gán lại chân VSPI |
| G3 | Chỉ gateway có key mới giải mã được | ĐÃ có (dùng `CRYPTO_KEY`) | ✅ | Nâng lên khóa dẫn xuất theo MAC (đồng bộ N7) |
| G4 | **OLED I2C** hiển thị số node + thông số | Không có | ❌ | Driver SSD1306 (SDA21/SCL22) |
| G5 | **Local Server trên ESP32** lưu dữ liệu, **BỎ Firebase** | Dùng Firebase | ❌ | HTTP server + JSON store (RAM/NVS/SPIFFS), gỡ `firebase.*`, `wifi_sta` thành AP+STA |
| G6 | Downlink điều khiển actuator + trigger OTA từ App | Chỉ ACK | ❌ | Lệnh downlink có ký/mã hóa → node |

### 2.3 APP / WEB (Phase 4 — chờ OK)
| # | Yêu cầu | Trạng thái |
|---|---|---|
| A1 | Web-based dashboard + source build **APK** | ❌ Chưa có |
| A2 | Phân quyền **Admin/User** | ❌ |
| A3 | Điều khiển actuator thủ công **App→Gateway→Node→Actuator** | ❌ |

### 2.4 HIL TOOL (Phase 5)
| # | Yêu cầu | Trạng thái | Việc cần làm |
|---|---|---|---|
| H1 | Kịch bản test **OTA** (theo dõi tiến trình) | ❌ | Thêm test OTA + parse tiến trình `%` |
| H2 | Giả lập **tấn công LoRa / Replay attack** | Chỉ có jam mô phỏng | ⚠️ | Test bắt gói + phát lại → kiểm chứng node/gateway loại bỏ |
| H3 | Đóng gói **.exe** | Có sẵn script | ✅ | Cập nhật cho tính năng mới |

---

## 3. KẾ HOẠCH THỰC HIỆN (thứ tự)

- **PHASE 2 — Node** (tự động làm ngay sau Phase 1):
  1. `board_pins.h`: thêm MQ135/DS3231/CO2-UART/Act1-3.
  2. Driver mới: `mq135.[ch]`, `ds3231.[ch]`, `mhz19.[ch]` (CO2), `actuator.[ch]`.
  3. Mở rộng `packet.h/packet.c`: thêm `nh3_ppm`, `co2_ppm`, `ch4_ppm`, trạng thái actuator, `decide_us`; **giữ đồng bộ với gateway**.
  4. `fsm.[ch]`: THI + ngưỡng nhiệt/NH₃/CO₂ + hysteresis + điều khiển Act1-3, đo `decide_us`.
  5. Định danh MAC + `crypto_derive_node_key()` (HKDF từ master key + MAC).
  6. Anti-replay theo `epoch`.
  7. Scheduling: bộ đọc cảm biến luân phiên + warm-up MQ135.
  8. OTA: `partitions_ota.csv`, `sdkconfig` (OTA + WiFi), `ota_update.[ch]`, `wifi_node.[ch]`, rollback trong `app_main`.
- **PHASE 3 — Gateway**: pinout DevKit, OLED SSD1306, HTTP local server + store, gỡ Firebase, downlink điều khiển/OTA. Đồng bộ `packet.*`, `crypto.*`.
- **PHASE 4 — App/Web** (CHỜ BẠN "OK"): React/Vite dashboard + Capacitor build APK, RBAC, điều khiển actuator.
- **PHASE 5 — HIL**: test OTA, test replay attack, cập nhật `.exe`.

---

## 4. GÁN CHÂN (PINOUT) — Thông báo & chốt

### 4.1 Node ESP32-S3 (bổ sung mới)
| Chức năng | Chân | Ghi chú |
|---|---|---|
| MQ135 A0 | **IO16** (ADC2_CH5) | Node không dùng WiFi lúc thường → ADC2 OK; khi OTA sẽ tạm dừng đọc |
| DS3231 SDA/SCL | **IO8 / IO14** | Dùng chung I2C0 sẵn có |
| DS3231 SQW | **IO1** | Nguồn wake sự kiện (báo thức/alarm) |
| CO2 UART (MH-Z19) | ESP **RX=IO5**, **TX=IO4** | Sensor TX→IO5, sensor RX→IO4, 9600 8N1 |
| Act1 / Act2 / Act3 | **IO38 / IO42 / IO45** | Relay/MOSFET, mặc định active-HIGH (đổi được) |
| LED B/G/R (giữ) | IO39 / IO40 / IO41 | Không đổi |

### 4.2 Gateway ESP32 DevKit (mới) — *đề xuất, bạn có thể chỉnh*
| Chức năng | Chân DevKit | Ghi chú |
|---|---|---|
| LoRa SCK | **GPIO18** | VSPI CLK |
| LoRa MISO | **GPIO19** | VSPI MISO |
| LoRa MOSI | **GPIO23** | VSPI MOSI |
| LoRa NSS/CS | **GPIO5** | |
| LoRa RST | **GPIO27** | |
| LoRa DIO0 | **GPIO26** | Ngắt RxDone |
| OLED SSD1306 SDA | **GPIO21** | I2C mặc định |
| OLED SSD1306 SCL | **GPIO22** | I2C mặc định |
| LED trạng thái | **GPIO2** | LED onboard |

> Lưu ý DevKit: tránh GPIO34–39 (chỉ input), GPIO6–11 (nối flash). Bộ chân trên đều an toàn.

---

## 5. HƯỚNG DẪN SỬ DỤNG HỆ THỐNG (bản hiện tại — sẽ mở rộng theo từng phase)

### 5.1 Build & nạp Node
```powershell
# Mở "ESP-IDF PowerShell" (đã export idf.py)
cd D:\2025.2\Final2\code\NodeSensor_test
idf.py set-target esp32s3
idf.py menuconfig      # Node Sensor Configuration: Node ID, chu kỳ, chế độ ngủ, LoRa SF/freq
idf.py build flash monitor -p COMx
```
Chọn thuật toán mã hóa trong `main/crypto_cfg.h` (`CRYPTO_ALGO_AES` hoặc `CRYPTO_ALGO_ASCON`) — **Node và Gateway phải giống nhau và cùng `CRYPTO_KEY`**.

### 5.2 Đọc dữ liệu Node
- **Cách 1 (khuyên dùng): cắm thẳng Node vào máy** → Node tự phát telemetry JSON mỗi chu kỳ, không cần gateway. Mở **EdgeProfiler** (`HIL_Tool`), chọn Source = *Node (direct)*.
- **Cách 2: qua Gateway** → cắm Gateway, chọn Source = *Gateway (relayed)*; Gateway forward dữ liệu node nhận được.
- Mỗi dòng JSON gồm: `sensor` (t/h/lux/press/…), `lora` (rssi/snr/rtt), `metrics` (proc/enc/dist/pph/tx/ack), `buf` (offline), `power` (sleep), `fsm`, `online`.

### 5.3 Gateway
```powershell
cd D:\2025.2\Final2\code\LoraGateway
# (Phase 3) sẽ đổi sang: idf.py set-target esp32
idf.py build flash monitor -p COMy
```
Hiện tại cấu hình WiFi/Firebase ở `main/gw_config.h` — **Phase 3 sẽ thay bằng Local Server** (OLED hiển thị số node + thông số; App kết nối qua IP gateway).

### 5.4 OTA (sẽ có sau Phase 2)
Quy trình Hybrid OTA theo báo cáo (Ch4.4):
1. App/Gateway gửi **downlink LoRa**: `OTA_FLAG + URL firmware (HTTPS)`.
2. Node đánh thức **Task_OTA**, tạm dừng đọc cảm biến, bật WiFi.
3. `esp_https_ota` tải firmware theo khối (chunked, ~10KB RAM) vào slot dự phòng, verify **SHA-256**.
4. `esp_restart()` → chạy firmware mới ở trạng thái `PENDING_VERIFY`.
5. Node tự kiểm tra ngoại vi (I2C/MQ135/SHT/LoRa) → gọi `esp_ota_mark_app_valid_cancel_rollback()`. Nếu treo → watchdog reset → **rollback** về bản cũ.

### 5.5 HIL Test
Chạy `EdgeProfiler` → chọn cổng → "+ Connect Port". Chạy kịch bản test trong panel Test. Xuất `.exe`:
```powershell
cd D:\2025.2\Final2\code\HIL_Tool
.\build_exe.bat   # ra dist\EdgeProfiler.exe
```

---

## 6. Rủi ro & Lưu ý kỹ thuật
- **OTA cần đổi partition table** (`SINGLE_APP_LARGE` hiện tại không có 2 slot). Sẽ thêm `partitions_ota.csv` + `otadata`. Flash 4MB đủ 2 app ~1.5MB.
- **ADC2 (IO16)** dùng chung tài nguyên với WiFi → chỉ đọc MQ135 khi **không** OTA (lúc OTA đã tạm dừng cảm biến).
- **CO2 UART**: mặc định theo giao thức **MH-Z19**; nếu bạn dùng cảm biến khác (SenseAir S8, Cubic…) báo lại để đổi driver.
- `packet.h`, `packet.c`, `crypto.*`, `ascon.*` **phải giống hệt** giữa Node & Gateway — mọi thay đổi struct sẽ đồng bộ cả hai.
