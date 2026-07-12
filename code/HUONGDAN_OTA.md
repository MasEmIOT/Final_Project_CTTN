# Hướng dẫn dùng OTA (cập nhật firmware Node từ xa)

OTA theo mô hình **Hybrid** (Ch4.4): App/Gateway gửi lệnh **qua LoRa** → Node bật WiFi →
tải firmware qua **HTTP/HTTPS** → verify **SHA-256** → khởi động lại → tự kiểm tra ngoại vi →
**giữ bản mới** hoặc **rollback** bản cũ nếu lỗi.

```
App (hoặc HIL)  --/api/cmd (OTA,url)-->  Gateway  --LoRa (lệnh mã hóa)-->  Node
                                                                            │ bật WiFi
                                                          HTTP server  <----┘ tải fw.bin
                                                                            │ verify SHA-256
                                                                            └ reboot → mark_valid / rollback
```

---

## 0. Điều kiện (chỉ làm 1 lần)
- **Node đã có bảng phân vùng OTA 2 slot** — đã cấu hình sẵn (`partitions_ota.csv`), không cần làm gì.
- **Khai báo WiFi cho OTA** (Node chỉ bật WiFi khi nạp OTA):
  - Cách 1 (khuyên dùng): `idf.py menuconfig` → **Node Sensor Configuration** →
    - `OTA WiFi SSID (chi dung khi nap OTA)` = tên WiFi của bạn
    - `OTA WiFi password` = mật khẩu
  - Cách 2: sửa nhanh fallback trong `NodeSensor_test/main/ota_update.c` (dòng `CONFIG_NODE_OTA_WIFI_SSID/PASS`).
  - ⚠️ WiFi này phải là mạng mà **máy chủ chứa firmware cũng nằm trong đó** (để Node tải được).

> Sau khi đổi WiFi phải `idf.py build flash` lại Node một lần (để firmware ĐANG CHẠY biết WiFi).

---

## 1. Tạo firmware mới + đưa lên server

### 1a. Build firmware mới
Sửa gì đó cho khác bản cũ (ví dụ đổi 1 dòng log để nhận biết), rồi:
```
cd D:\2025.2\Final2\code\NodeSensor_test
idf.py build
```
File cần nạp OTA: `build\NodeSensor_test.bin`.

### 1b. Host firmware qua HTTP (đơn giản nhất)
Mở **ESP-IDF Terminal** (có sẵn python), rồi:
```
cd D:\2025.2\Final2\code\NodeSensor_test\build
copy NodeSensor_test.bin fw.bin
python -m http.server 8000
```
→ URL firmware = `http://<IP-máy-tính>:8000/fw.bin`
(xem IP máy bằng `ipconfig`; ví dụ `http://192.168.1.50:8000/fw.bin`)

> **URL tối đa 64 ký tự** → đặt tên file ngắn (`fw.bin`), đừng để tên dài.
> Dùng HTTPS công khai cũng được (`https://...`) — Node xác thực bằng CA bundle chống MITM.

---

## 2. Kích hoạt OTA (chọn 1 trong 3 cách)

### Cách A — Từ App/Web (Admin)
1. Đăng nhập **admin**, mở thẻ Node → bấm **"⤓ OTA Update"**.
2. Dán URL `http://192.168.1.50:8000/fw.bin` → OK.

### Cách B — Từ HIL EdgeProfiler (có theo dõi % + tự chấm PASS/FAIL)
1. Cắm **cả cổng Node và Gateway** vào máy.
2. Ô **"OTA URL:"** (trên nút RUN) → dán URL.
3. Chọn kịch bản **"OTA Update Test"** → **RUN**. Tool sẽ gửi lệnh và vẽ tiến trình.

### Cách C — Gõ thẳng lệnh vào Gateway (serial)
Gửi 1 dòng JSON tới cổng COM của Gateway (qua HIL hoặc serial monitor):
```json
{"cmd":"send_cmd","node":12,"op":3,"url":"http://192.168.1.50:8000/fw.bin"}
```
(`node` = Node ID, `op`:3 = OTA. Node ID xem trong log node: `NODE SENSOR (id=..)`).

> Lệnh được Gateway phát xuống Node **ngay khi Node gửi gói kế tiếp** (vài giây), nên OTA
> khởi động sau 1 chu kỳ. Lệnh có chống replay bằng `cmd_seq`.

---

## 3. Theo dõi tiến trình
Node phát JSON tiến trình ra **cổng USB của chính nó**:
```json
{"type":"ota","node":12,"state":"downloading","pct":42,"msg":""}
```
Các trạng thái: `start → wifi_ok → downloading (0–95%) → verifying → reboot → valid`.
- Trên **HIL**: hiện ở console + kịch bản OTA chấm PASS khi tới `reboot/valid`.
- Trên **serial monitor**: đọc log `ota:` trực tiếp.

Khi thành công: Node `esp_restart()` vào firmware mới, tự kiểm tra I2C/cảm biến/LoRa,
rồi in `mark_valid (huy rollback)` → cập nhật hoàn tất.

---

## 4. Cơ chế Rollback (an toàn — tự động)
- Firmware mới khởi động ở trạng thái **PENDING_VERIFY**.
- Nếu khởi tạo ngoại vi **OK** → `esp_ota_mark_app_valid_cancel_rollback()` → giữ bản mới.
- Nếu bản mới **treo/lỗi** (không kịp confirm) → **Watchdog reset** → Bootloader **tự quay về bản cũ**.
→ Bạn không bao giờ "chết" thiết bị vì nạp nhầm firmware lỗi.

**Muốn xem rollback hoạt động:** cố tình làm bản mới treo (ví dụ thêm `while(1){}` ở đầu
`app_main`), host bản đó, OTA → Node nạp, treo, watchdog reset, quay lại bản cũ.

---

## 5. Xử lý sự cố
| Hiện tượng | Nguyên nhân / cách xử lý |
|---|---|
| `state:error, msg:wifi_fail` | Sai SSID/PASS OTA, hoặc WiFi ngoài tầm. Kiểm tra menuconfig + nạp lại Node. |
| `state:error, msg:begin_fail`/`download_incomplete` | URL sai/không tới được server, hoặc server tắt. Kiểm tra `ipconfig`, firewall, `python -m http.server` còn chạy. |
| `state:error, msg:validate_failed` | Ảnh firmware hỏng (không phải .bin hợp lệ / tải lỗi). Build lại, host lại. |
| Không thấy `type:ota` | Chưa cắm cổng **Node** (tiến trình phát ở USB Node, không phải Gateway). |
| Node không bắt đầu OTA | Sai Node ID trong lệnh, hoặc Node đang offline (chưa gửi gói để Gateway gửi kèm lệnh). |
| Tải chậm | Bình thường (chunked ~10KB RAM). Firmware ~1.1MB, qua WiFi vài chục giây. |

---

## 6. Lưu ý kỹ thuật
- Khi OTA: Node **tạm dừng đọc cảm biến** và **tắt actuator an toàn**, gỡ task cảm biến khỏi
  Watchdog để không bị reset giữa chừng; xong sẽ chạy lại bình thường.
- **ADC2 (MQ135)** dùng chung tài nguyên WiFi — nhưng lúc OTA cảm biến đã tạm dừng nên không xung đột.
- Firmware **Gateway** không cần OTA (nó là trạm cố định); OTA chỉ áp cho **Node**.
- Máy chủ firmware và Node phải **cùng mạng WiFi** (hoặc URL trỏ tới server Node truy cập được).
