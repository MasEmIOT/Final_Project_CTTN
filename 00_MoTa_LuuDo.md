# Mô tả lưu đồ thuật toán — Firmware Edge-IoT (Node + Gateway)

**Hệ thống:** ESP32-S3 · LoRa SX1278 · FreeRTOS · Điện toán biên · AES-128-GCM / ASCON-128 · ESP-IDF v6.0
Mỗi mục dưới đây ứng với một file `.drawio` (01–14). *Mục đích* = phụ đề slide; *Các bước* = gạch đầu dòng copy thẳng vào slide; *Thông số chính* = hằng số/số liệu để trích dẫn.

---

## 1 · Khởi động Node (`app_main`)
**Mục đích:** Khối khởi tạo chạy một lần mỗi khi cấp nguồn hoặc thức dậy từ Deep Sleep, kết thúc bằng việc tạo hai tác vụ FreeRTOS.

- Suy ra **Node ID từ MAC efuse** của chip (định danh duy nhất); đọc lý do reset, nếu lần trước do watchdog thì tăng bộ đếm `wdt_resets`.
- **Lần cấp nguồn đầu:** khởi tạo 4 bộ lọc. **Thức từ Deep Sleep:** khôi phục trạng thái lọc từ RTC RAM (không khởi tạo lại).
- Cấu hình **Task Watchdog (10 s, panic)**, rồi init LED, I2C, cảm biến và **LoRa SX1278** (lặp lại tới khi thành công).
- **Tự kiểm tra OTA:** nếu firmware đang ở trạng thái `PENDING_VERIFY` và mọi ngoại vi đã lên OK → `ota_mark_valid()` hủy rollback.
- Tạo **Task OTA + Task chu trình cảm biến**, rồi vào vòng lặp chính.

**Thông số chính:** Node ID = f(MAC efuse); WDT = 10 s; bộ lọc/bộ đếm giữ trong RTC RAM.

---

## 2 · Chu trình chính của Node (`sensor_cycle_task`)
**Mục đích:** Vòng lặp "xương sống" chạy mãi: đọc → lọc → ra quyết định → mã hóa → truyền → đồng bộ/lưu → telemetry → ngủ.

- Feed watchdog, kiểm tra chân test WDT; **đọc toàn bộ cảm biến** (SHT30, BMP180, BH1750, MQ135, CO2).
- **Lọc nhiễu** từng tín hiệu, tính số liệu pipeline (heap, gói/giờ, mẫu/giây), rồi chạy **FSM ra quyết định và điều khiển rơ-le Act1..3** (đo `decide_us`).
- Gắn epoch + trạng thái buffer (đo `proc_us`), rồi **đóng gói và mã hóa AEAD** (đo `enc_us`).
- `send_with_ack`: **có ACK** → cập nhật thống kê, ước lượng khoảng cách, gửi lại dữ liệu offline; **mất ACK** → lưu bản ghi vào buffer.
- Phát telemetry JSON qua USB; chọn **Deep Sleep** chỉ khi đã ACK và buffer rỗng, ngược lại **Light Sleep** (giữ RAM).

**Điểm thiết kế cốt lõi:** nhánh an toàn (FSM + rơ-le) chạy **trước** nhánh truyền thông — vật nuôi được bảo vệ ngay cả khi mất sóng.

---

## 3 · Đọc cảm biến (tự dò, xử lý lỗi mềm)
**Mục đích:** Firmware chạy được dù cảm biến cắm ở bus nào hay thiếu cảm biến — tuyệt đối không treo.

- Với mỗi cảm biến, **dò địa chỉ trên Bus0 rồi Bus1** (SHT30 0x44/0x45, BMP180 0x77, BH1750 0x23/0x5C, DS3231 0x68).
- Dò OK → init driver, `present=true`, lưu bus + địa chỉ; dò thất bại → `present=false` kèm cảnh báo (**không treo**).
- **Mỗi chu kỳ** chỉ đọc cảm biến có `present=true`; đọc thành công (SHT30 kiểm **CRC**) → bật cờ hợp lệ, đọc lỗi → đánh dấu invalid và bỏ qua.

**Thông số chính:** tự dò 2 bus I2C; cờ hợp lệ theo từng lần đọc; SHT30 kiểm CRC.

---

## 4 · Bộ lọc số (`filter_apply`)
**Mục đích:** Làm sạch nhiễu trước khi vào FSM để rơ-le không đóng/cắt liên tục quanh ngưỡng; mỗi tín hiệu dùng bộ lọc hợp đặc tính vật lý.

- **EMA** (nhiệt độ, độ ẩm): `y = a·x + (1-a)·y_prev`, **a = 0.30** — mượt, chỉ tốn 1 ô RAM.
- **Moving Average** (áp suất): trung bình cửa sổ N mẫu — tối ưu khử nhiễu Gauss.
- **Median** (ánh sáng): sắp xếp cửa sổ, lấy phần tử giữa — miễn nhiễm đột biến xung.
- **Kalman 1D** (tùy chọn): `p+=q; K=p/(p+r); x+=K(z−x); p=(1−K)p`, **q = 0.01, r = 4**.

**Thông số chính:** EMA a = 0.30; cửa sổ N = 8; Kalman q = 0.01, r = 4.

---

## 5 · Máy trạng thái FSM ra quyết định biên (`fsm_update`)
**Mục đích:** Trái tim của điện toán biên — đối chiếu dữ liệu với ma trận ngưỡng an toàn sinh học và điều khiển rơ-le trong **dưới 50 ms**.

- Tính **THI = 0.8·T + (RH/100)(T−14.4) + 46.4**.
- **KHẨN CẤP** nếu T>35 / NH3>25 / CO2>5000 / THI>78 → bật toàn bộ (Act1+2+3).
- **CẢNH BÁO** nếu T>32 / NH3>20 / CO2>3000 / THI>72 → quạt mức thấp (Act1).
- Còn lại áp dụng **hysteresis**: chỉ hạ về **AN TOÀN** khi đã xuống dưới ngưỡng clear (T<30, NH3<18, THI<70); nếu chưa thì giữ trạng thái hiện tại.
- Tôn trọng **chế độ thủ công** (lệnh downlink override bỏ qua điều khiển tự động); trả về state, mask, THI×10, `decide_us`.

**Thông số chính:** ngưỡng T 35/32/30 °C, NH3 25/20/18 ppm, CO2 5000/3000/2500 ppm, THI 78/72/70; độ trễ < 50 ms.

---

## 6 · Mã hóa có xác thực AEAD (`packet_seal`)
**Mục đích:** Biến payload thành gói vừa **bảo mật vừa chống giả mạo** trước khi phát qua sóng mở.

- Dựng **header** bản rõ (magic, version=4, algo, node_id, plen); sinh **nonce ngẫu nhiên 16 byte**.
- Đặt **AAD = 6 byte header đầu** — header vẫn đọc được để định tuyến nhưng mọi sửa đổi đều làm hỏng thẻ xác thực.
- Mã hóa theo thuật toán được chọn: **AES-128-GCM** (engine phần cứng PSA) hoặc **ASCON-128** (phần mềm hạng nhẹ).
- Ghép frame: **header + ciphertext + thẻ xác thực 16 byte** → sẵn sàng phát LoRa.

**Thông số chính:** version 4; nonce 16 B; tag 16 B; AAD 6 B; chọn AES-128-GCM hoặc ASCON-128.

---

## 7 · Truyền LoRa có ACK + thử lại (`send_with_ack`)
**Mục đích:** Biến truyền một chiều thành bắt tay hai chiều, đồng thời tận dụng ACK để đo chất lượng đường truyền và đồng bộ giờ.

- Phát frame; TX lỗi → kiểm tra số lượt thử. TX OK → chuyển sang thu và chờ trong **cửa sổ ACK 800 ms**.
- Gói đến: nếu là **lệnh downlink** → xử lý rồi tiếp tục chờ; nếu là **ACK khớp node+seq** → thành công.
- Khi thành công: **đồng bộ giờ từ `ACK.epoch`** (set RTC nếu chưa), tính **RTT**, đọc RSSI/SNR → trả TRUE.
- Khi hết giờ: còn lượt → phát lại; hết lượt → trả **FALSE (mất kết nối)**.

**Thông số chính:** cửa sổ ACK 800 ms; 2 lần thử lại; chờ bằng ngắt phần cứng (không busy-wait).

---

## 8 · Ước lượng khoảng cách từ RSSI (`estimate_distance_dm`)
**Mục đích:** Suy ra khoảng cách Node–Gateway tương đối từ cường độ tín hiệu ACK theo mô hình suy hao log-distance.

- Tính số mũ **e = (RSSI_1m − rssi) / (10·n)** với **RSSI_1m = −43 dBm, n = 2.7**.
- Khoảng cách **d = 10^e** (mét); **giới hạn 0…6000 m**; trả về đơn vị decimet (số nguyên gọn).

**Thông số chính:** RSSI tham chiếu tại 1 m = −43 dBm; hệ số suy hao n = 2.7.

---

## 9 · Xử lý lệnh Downlink (`handle_downlink_cmd`)
**Mục đích:** Cho phép điều khiển từ xa an toàn — đảo actuator thủ công, kích hoạt OTA, reboot, ping.

- `cmd_open` **giải mã + xác thực AEAD**; chỉ nhận khi hợp lệ và đúng Node (hoặc broadcast).
- **Chống replay lệnh:** chỉ chấp nhận `cmd_seq` **tăng dần**; ngược lại bỏ qua.
- Phân loại theo lệnh: **ACT_SET** (mask thủ công), **ACT_AUTO** (về FSM tự động), **OTA** (lưu URL + đánh thức Task OTA), **REBOOT**, **PING**.

**Thông số chính:** xác thực AEAD từng lệnh; chống replay bằng `cmd_seq` tăng dần.

---

## 10 · Lưu trữ ngoại tuyến (Store-and-Forward)
**Mục đích:** Không mất dữ liệu khi mất Gateway, và gửi lại toàn bộ khi có kết nối trở lại.

- **Mất ACK** → `off_push` ghi bản ghi + epoch vào **ring buffer (256)**; nếu đầy thì **bỏ bản ghi cũ nhất** (`dropped++`), ngược lại `stored++`.
- **Có ACK** → nếu còn bản ghi tồn đọng, `flush_backlog` gửi tối đa **10 bản ghi/chu kỳ**, mỗi bản gắn cờ **BACKLOG** kèm epoch gốc.
- Mỗi bản ghi được ACK → giải phóng (`flushed++`) và lặp; nếu thất bại → dừng, để chu kỳ sau gửi tiếp.

**Thông số chính:** ring buffer 256; flush ≤ 10/chu kỳ; chính sách drop-oldest; giữ nguyên epoch gốc.

---

## 11 · Quản lý năng lượng Hybrid Sleep (`node_sleep`)
**Mục đích:** Tiết kiệm pin tối đa mà không mất dữ liệu trong buffer.

- **can_deep** (đã ACK *và* buffer rỗng) → arm radio, thức bằng **timer + ext1(DIO0)**, `esp_deep_sleep_start()` → **chip reset** khi thức (trạng thái giữ trong RTC RAM).
- Ngược lại → **Light Sleep** (`esp_light_sleep_start`), **giữ RAM và RTOS** để buffer offline không mất.
- Khi thức, phân biệt **sự kiện GPIO/DIO0** (log + nháy LED) với thức do **timer**, rồi trở lại chu trình chính.

**Thông số chính:** Deep Sleep = reset + trạng thái trong RTC RAM; Light Sleep = giữ RAM; thức 2 nguồn (timer hoặc sự kiện DIO0).

---

## 12 · Watchdog tự phục hồi
**Mục đích:** Giữ thiết bị 24/7 không kẹt treo (khóa bus I2C, vòng lặp vô hạn).

- Mỗi chu kỳ **feed watchdog** (`esp_task_wdt_reset`).
- **Đường kiểm thử:** chập **GPIO0 xuống đất** ép vào vòng lặp vô hạn không feed → sau **10 s Task WDT panic và reset** chip (chứng minh cơ chế).
- Sau reboot, lý do reset là `TASK_WDT`; **`wdt_resets++`** (RTC RAM) và báo cáo trong telemetry.

**Thông số chính:** WDT timeout 10 s, panic reset; chân test GPIO0; bộ đếm giữ trong RTC RAM.

---

## 13 · Cập nhật OTA + Rollback (`ota_do_update`)
**Mục đích:** Cập nhật firmware từ xa với rủi ro bằng không — bản lỗi tự động quay về bản cũ.

- Kích hoạt bằng **CMD_OTA**; Task OTA **tạm dừng task cảm biến, gỡ khỏi WDT, tắt actuator**.
- Kết nối **WiFi 2.4 GHz**, `esp_https_ota_begin` (**HTTPS + CA bundle, chống MITM**), tải **khối (chunk) vào vùng APP nhàn rỗi**.
- Dữ liệu thiếu → hủy; đủ → `esp_https_ota_finish` **xác thực SHA-256 + set boot**; hash sai → hủy.
- Khởi động lại vào **firmware mới (PENDING_VERIFY)**; nếu mọi ngoại vi init OK → `ota_mark_valid`; nếu treo → **WDT reset → Bootloader rollback** về bản cũ.

**Thông số chính:** vùng APP kép (dual-bank); xác thực SHA-256; HTTPS/CA-bundle; auto-rollback qua PENDING_VERIFY + WDT.

---

## 14 · Gateway (`lora_rx_task`)
**Mục đích:** Gateway "nhẹ" — mọi tính toán ở Node; Gateway chỉ giải mã, chặn replay, lưu kho phục vụ App/Web và trả ACK.

- `sx127x_receive` → kiểm tra **độ dài envelope**; `packet_open` **giải mã + xác thực** (sai xác thực → bỏ gói, chống giả mạo).
- **Chống replay theo timestamp:** nếu **|gw_epoch − pkt.epoch| > 5 s** và không phải gói backlog → **bỏ gói, không ACK**, `replay_drops++`.
- Ngược lại `node_store_update` (phục vụ App/Web), **gửi downlink nếu có + trả ACK** (kèm RSSI/SNR uplink + epoch để đồng bộ giờ), rồi forward JSON ra USB cho công cụ HIL.

**Thông số chính:** cửa sổ replay 5 s; gói backlog được miễn; ACK mang RSSI/SNR + epoch.
