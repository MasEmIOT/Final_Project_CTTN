# LoRa Gateway (ESP32-S3 + SX1278 → Firebase)

Chạy trên **cùng PCB Node Sensor** (chỉ cần gắn module LoRa Ra-02). Nhận gói từ node qua LoRa 433 MHz, kiểm tra CRC rồi đẩy lên **Firebase Realtime Database** qua WiFi (HTTPS).

Dữ liệu trên Firebase:

```
nodes/
  1/
    latest:  { node, seq, temp, hum, temp_bmp, press, lux, rssi, snr, ts }
    history/
      <push-id>: { ... }          # ts do server Firebase tự gán
```

Sensor nào node không đọc được sẽ là `null`.

## Cài đặt Firebase

1. Firebase console → tạo **Realtime Database**.
2. Lấy URL dạng `https://<project>-default-rtdb.<region>.firebasedatabase.app`.
3. Rules: để test nhanh cho phép ghi (`".read": true, ".write": true`), hoặc dùng **database secret** (Project settings → Service accounts → Database secrets) điền vào mục auth token bên dưới.

## Build & flash

```
idf.py set-target esp32s3
idf.py menuconfig     # "LoRa Gateway Configuration": WiFi SSID/pass, Firebase URL, auth token, tần số/SF LoRa
idf.py build flash monitor
```

Lưu ý:

- WiFi của ESP32-S3 chỉ bắt được mạng **2.4 GHz**.
- Tần số / SF / sync word LoRa **phải trùng với node** (mặc định 433 MHz, SF9, 0x12).
- Mở folder này trong VS Code: chọn ESP-IDF v6.0.1 + target esp32s3 khi extension hỏi (giống project node).

## LED

Xanh dương nháy = nhận được gói LoRa. Xanh lá = đẩy Firebase OK. Đỏ = lỗi WiFi/Firebase.
