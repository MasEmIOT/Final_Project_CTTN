# Node Sensor (ESP32-S3 + LoRa SX1278)

Đọc **SHT30 + BMP280 + BH1750** rồi gửi gói 28 byte qua **LoRa 433 MHz** về gateway.
Không cần component ngoài, chạy trên ESP-IDF v6.0.

## Chân (theo schematic Node Sensor Rev 1.0)

| Chức năng | GPIO |
|---|---|
| I2C bus 0 (SDA / SCL) | 8 / 14 |
| I2C bus 1 (I2C_SDA1 / I2C_SCL1) | 6 / 7 |
| LoRa SCK / MISO / MOSI / NSS | 10 / 11 / 12 / 13 |
| LoRa RST | 9 |
| LED B / G / R | 39 / 40 / 41 |

Code **tự dò sensor trên cả 2 bus** (cắm J14–J19 bus nào cũng nhận), in kết quả dò lúc khởi động.

CH4 trên J11 (GPIO21) tạm bỏ — GPIO21 không có ADC trên ESP32-S3. Khi nào cần thì cắm sang J9 (A0 → GPIO16, ADC2_CH5).

## Build & flash

```
idf.py set-target esp32s3
idf.py menuconfig        # "Node Sensor Configuration": Node ID, chu kỳ gửi, tần số/SF LoRa
idf.py build flash monitor
```

Mặc định: node id 1, gửi mỗi 30 s, 433 MHz, SF9, BW 125 kHz, CR 4/5, sync word 0x12, 17 dBm.
**Tần số / SF / sync word phải trùng với gateway.**

## Định dạng gói tin

Xem `main/packet.h` (file này phải giống hệt bên gateway). LED xanh lá nháy = gửi OK, đỏ = lỗi.
