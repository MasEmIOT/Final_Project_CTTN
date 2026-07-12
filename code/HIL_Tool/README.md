# EdgeProfiler 2.0 — HIL Testing & Multi-Node Monitoring

QA dashboard cho hệ ESP32-S3 LoRa (nhiều **Node**: SHT30 + BH1750 + BMP180 → **Gateway**, gói tin mã hóa AES/ASCON).

## Chạy nhanh
```bat
cd HIL_Tool
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
python edge_profiler.py
```
Yêu cầu Python 3.9+.

## Build EdgeProfiler.exe
```bat
cd HIL_Tool
build_exe.bat
```
→ `dist\EdgeProfiler.exe` (xem chi tiết lệnh PyInstaller trong file build_exe.bat).

## QUAN TRỌNG — cắm đúng cổng
- **Telemetry/biểu đồ chỉ có khi nối tới cổng COM của GATEWAY** (cổng phát JSON, ví dụ COM9).
  Gateway mới là nơi tổng hợp dữ liệu của tất cả node và in JSON.
- Nối tới cổng của **Node** (ví dụ COM12) chỉ thấy log thô (text) — đó là lý do trước đây
  biểu đồ trống.
- Có thể **nối nhiều cổng cùng lúc**: bấm "+ Connect Port" cho từng COM (gateway + các node console).

## Bảng điều khiển
1. **Connection bar** — chọn Port + Baud → "+ Connect Port" (thêm được nhiều cổng). Nút Freeze/Resume để dừng/tiếp tục vẽ live. Disconnect All.
2. **View node** — chọn node để xem chi tiết (tự phát hiện node từ telemetry).
3. **Metric tiles** (bấm vào để mở popup chi tiết + min/max/avg): Response/RTT, Distance, PDR, Bandwidth (pkt/h), Proc time, Encrypt time, RSSI, Node heap.
4. **Tabs biểu đồ**: Climate (T/H), Environment (áp suất/ánh sáng), Network (RSSI/SNR), Performance (RTT/khoảng cách), **Distance↔Loss** (tương quan khoảng cách vs mất gói, mỗi node một màu).
5. **Gateway Deep Profiler** — heap (leak watch), CPU core 0/1, uptime, nodes online, WiFi, Firebase OK/FAIL.
6. **Nodes Overview** — bảng tất cả node (T, H, RSSI, PDR, RTT, khoảng cách, FSM, online).
7. **Test Sequencer** — nhiều kịch bản:
   - HW Fault: I2C Lockup (SHT30)
   - SW Fault: LoRa Jamming
   - Logic Override: Force EMERGENCY
   - Resilience: Gateway Offline (tắt nguồn gateway để đo khả năng hồi phục)
   - Bandwidth Check (pkt/h)
   - Encryption Verify (AES/ASCON)
   - Latency / Response Time
   - Auto-Run Full Suite
8. **Reports** — Export HTML (đa node) / XML (kèm testsuite JUnit + độ chính xác phát hiện bất thường).

## Thông số theo dõi
| Nhóm | Thông số |
|---|---|
| Thời gian phản ứng | RTT (ms) node↔gateway, gateway proc time |
| Phát hiện bất thường | FSM/alert + "Anomaly Detection Accuracy" trong báo cáo (tỉ lệ test fault được phát hiện) |
| Tối ưu băng thông | Bandwidth (pkt/h) mỗi node |
| Tài nguyên ESP32 | Heap + CPU core 0/1 (node trong telemetry, gateway ở panel riêng) |
| Mất kết nối gateway | Kịch bản "Resilience: Gateway Offline" đo gián đoạn + hồi phục |
| Khoảng cách | Ước lượng từ RSSI (node tự tính) + biểu đồ tương quan với mất gói |
| Bảo mật | Thuật toán mã hóa (AES-128-GCM / ASCON-128) + thời gian mã hóa |

## Lệnh điều khiển gửi xuống gateway (tự động bởi test, hoặc gửi tay)
`{"cmd":"inject_fault","target":"i2c"}` · `{"cmd":"jam_lora"}` · `{"cmd":"force_fsm","state":"EMERGENCY"}` · `{"cmd":"clear"}`
