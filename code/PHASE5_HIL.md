# PHASE 5 — Cập nhật HIL Tool (OTA test + Replay attack) + đóng gói .exe ✅

Công cụ **EdgeProfiler 2.1** (`HIL_Tool/edge_profiler.py`) — thêm 2 kịch bản kiểm thử mới
và giữ nguyên toàn bộ tính năng cũ.

## 1. Thay đổi
| Nơi | Thay đổi |
|---|---|
| `edge_profiler.py` | SerialWorker: thêm signal `ota`/`sec` + route `type:ota`, `type:sec`. MainWindow: `_on_ota`/`_on_sec`, lưu `ota_state`/`sec_events`. Thêm ô **OTA URL** + 2 kịch bản **"OTA Update Test"**, **"LoRa Replay Attack"**. TestSequencer: `_t_ota`, `_t_replay`. |
| `LoraGateway/main/main.c` | Thêm lệnh serial `replay_capture` / `replay_now` (bắt & phát lại gói qua đường giải mã) + `send_cmd` (đặt lệnh downlink từ HIL). Phát `{"type":"sec","event":"replay_test",...}`. |

## 2. Kịch bản kiểm thử mới

### 2.1 OTA Update Test
Kiểm chứng toàn bộ quy trình OTA (Ch4.4): trigger qua LoRa → tải HTTPS/HTTP → verify SHA-256 → rollback.
- **Chuẩn bị**: cắm **cả cổng NODE và GATEWAY** vào PC; dựng server firmware; đảm bảo node vào được WiFi (`NODE_OTA_WIFI_SSID/PASS`).
- Nhập **OTA URL** (vd `http://192.168.1.100:8000/node.bin`), chọn kịch bản **OTA Update Test** → **RUN**.
- Tool gửi lệnh OTA qua Gateway → Node, rồi **theo dõi tiến trình %** (`start → downloading → verifying → reboot/valid`). PASS nếu tới `reboot/valid`.

**Dựng server firmware nhanh** (từ thư mục build của node):
```powershell
cd D:\2025.2\Final2\code\NodeSensor_test\build
copy NodeSensor_test.bin node.bin
python -m http.server 8000        # URL = http://<IP-PC>:8000/node.bin
```

### 2.2 LoRa Replay Attack
Kiểm chứng chống tấn công phát lại theo nhãn thời gian (Ch4.3.4).
- **Chuẩn bị**: cắm cổng **GATEWAY** (có node đang phát).
- Chọn **LoRa Replay Attack** → **RUN**. Tool sẽ: bảo Gateway **bắt 1 gói hợp lệ** → chờ 6s (quá cửa sổ 5s) → **phát lại gói cũ** → kiểm tra Gateway **từ chối** (age > window).
- **PASS** = gói phát lại bị chặn (`result=blocked`) → chứng minh anti-replay hoạt động.
- Ngoài ra, gói replay thật (nếu có) cũng sinh sự kiện `replay_drop` hiển thị trong console + tile `Replay chặn`.

## 3. Đóng gói .exe (Windows)
```powershell
cd D:\2025.2\Final2\code\HIL_Tool
.\build_exe.bat
```
Kết quả: `dist\EdgeProfiler.exe` (chạy độc lập, không cần cài Python).
Thủ công tương đương:
```powershell
python -m venv venv; .\venv\Scripts\activate
pip install -r requirements.txt
pyinstaller --noconfirm --clean --onefile --noconsole --name EdgeProfiler ^
  --collect-submodules pyqtgraph --collect-submodules openpyxl ^
  --hidden-import serial.tools.list_ports --hidden-import openpyxl edge_profiler.py
```

## 4. Lưu ý
- `replay_now` dùng lại **đúng khung mã hóa** đã bắt (không giả mạo) — minh chứng tag AEAD vẫn hợp lệ nhưng bị loại **chỉ vì nhãn thời gian cũ**.
- Anti-replay cần Node có RTC (epoch>0) và Gateway có SNTP; nếu chưa đồng bộ giờ, gói sẽ "accepted" (không đủ dữ kiện để chặn) — hãy để hệ chạy vài giây cho SNTP/RTC ổn định trước khi test.
- OTA test: nếu chỉ cắm Gateway (không cắm Node), tool gửi được lệnh nhưng **không thấy tiến trình %** (progress phát ra trên USB của Node).
