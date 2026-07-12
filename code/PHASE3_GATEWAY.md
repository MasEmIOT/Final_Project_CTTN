# PHASE 3 — Hoàn thiện Gateway (ESP32 DevKit) ✅

## 1. File mới / thay đổi
| File | Vai trò |
|---|---|
| `main/board_pins.h` | **Pinout ESP32 DevKit**: LoRa VSPI (SCK18/MISO19/MOSI23/NSS5/RST27/DIO0 26), OLED I2C (SDA21/SCL22), LED GPIO2 |
| `main/ssd1306.[ch]` | **Driver OLED SSD1306** (framebuffer + font 5x7) |
| `main/node_store.[ch]` | **Kho dữ liệu RAM** thay Firebase: latest + history/node + **hàng đợi lệnh downlink** |
| `main/http_server.[ch]` | **Local HTTP Server** + REST API + dashboard nhúng + đăng nhập Admin/User |
| `main/main.c` | Viết lại: bỏ Firebase; thêm anti-replay, node_store, HTTP server, OLED task, gửi lệnh downlink |
| `main/gw_config.h` | Bỏ Firebase; thêm cấu hình HTTP + tài khoản Admin/User |
| `main/CMakeLists.txt` | Bỏ `firebase.c`; thêm node_store/http_server/ssd1306 + `esp_http_server`, `esp_driver_i2c` |
| `main/Kconfig.projbuild` | Bỏ config Firebase |
| `sdkconfig.defaults` | `CONFIG_IDF_TARGET="esp32"` (DevKit) |
| ~~`firebase.c/h`~~ | **ĐÃ XÓA** |

## 2. Tính năng (ánh xạ yêu cầu)
- **ESP32 DevKit + LoRa**: pinout VSPI đã gán (báo ở bảng trên).
- **Chỉ gateway có key mới giải mã**: giữ `packet_open` + `CRYPTO_KEY` (AES/ASCON). Gói giải mã sai → bỏ.
- **Chống replay (mới)**: gateway so `epoch` gói với giờ SNTP; lệch > 5s và **không phải** gói backlog → **bỏ, không ACK**, đếm `replay_drops`, phát sự kiện `{"type":"sec","event":"replay_drop"}` cho HIL.
- **OLED SSD1306**: hiển thị IP, số node online/tổng, WiFi, và tóm tắt từng node (id/nhiệt/ẩm/FSM).
- **Local Server (bỏ Firebase)**: REST API
  - `GET /api/nodes` — danh sách node + số liệu mới nhất
  - `GET /api/history?node=ID` — lịch sử 1 node (RAM ring 120 bản ghi)
  - `GET /api/status` — heap/uptime/wifi/IP/số node/replay_drops
  - `POST /api/login` — `{user,pass}` → `{role:"Admin"|"User"|"none"}`
  - `POST /api/cmd` — (cần header `X-Token`=admin pass) đặt lệnh downlink
  - `GET /` — dashboard nhúng (demo nhanh; App/Web đầy đủ ở Phase 4)
- **Downlink App→Gateway→Node→Actuator**: App gọi `/api/cmd` → gateway đưa vào hàng đợi → khi node đó uplink, gateway **gửi gói lệnh mã hóa TRƯỚC ACK** → node thực thi (bật/tắt Act1-3, AUTO, OTA, reboot).

## 3. Pinout đấu nối (ESP32 DevKit)
```
SX1278 (Ra-02)      ESP32 DevKit
  SCK   ----------- GPIO18
  MISO  ----------- GPIO19
  MOSI  ----------- GPIO23
  NSS   ----------- GPIO5
  RST   ----------- GPIO27
  DIO0  ----------- GPIO26   (tuỳ chọn, code polling)
  3V3/GND --------- 3V3 / GND

OLED SSD1306 (I2C)  ESP32 DevKit
  SDA   ----------- GPIO21
  SCL   ----------- GPIO22
  VCC/GND --------- 3V3 / GND
```

## 4. Cách dùng
```powershell
cd D:\2025.2\Final2\code\LoraGateway
idf.py set-target esp32          # BẮT BUỘC (đổi từ esp32s3 sang esp32)
idf.py menuconfig                # (tuỳ chọn) — WiFi/LoRa thực ra sửa trong main/gw_config.h
idf.py build flash monitor -p COMy
```
1. Sửa WiFi LAN trong `main/gw_config.h` (`GW_WIFI_SSID/PASS`) + tài khoản `GW_ADMIN_*`/`GW_USER_*`.
2. Gateway nối WiFi → log in ra **IP** (cũng hiển thị trên OLED).
3. Mở trình duyệt trong cùng mạng: `http://<IP gateway>/` để xem dashboard + điều khiển.
4. HIL tool vẫn cắm cổng COM của gateway đọc telemetry như cũ (giờ có thêm nh3/co2/thi/decide/act + sự kiện replay).

## 5. Lưu ý
- LoRa dùng `SPI2_HOST` + GPIO matrix (chân tuỳ ý) — LoRa tốc độ thấp nên không ảnh hưởng.
- Node & Gateway phải **cùng** `CRYPTO_ALGO` + `CRYPTO_KEY` + tần số/SF LoRa.
- Anti-replay cần **cả node có RTC (epoch>0)** và **gateway có SNTP** (cùng mạng có internet) — nếu node chưa có epoch, gateway bỏ qua kiểm tra (không chặn nhầm).
- Dashboard nhúng chỉ để test nhanh; **App/Web đầy đủ (RBAC, APK) sẽ làm ở Phase 4**.
