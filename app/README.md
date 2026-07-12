# LoRa Farm App (Web + APK)

Ứng dụng quản lý cho hệ thống giám sát vi khí hậu chăn nuôi LoRa.
Kết nối **Local Server** trên Gateway ESP32 (REST API), **không dùng Firebase**.

- **Web-based**: chạy `npm run dev` hoặc host thư mục `dist/` sau khi build.
- **APK Android**: đóng gói bằng Capacitor (một codebase → cả web lẫn app).
- **Phân quyền**: `Admin` (điều khiển Actuator/OTA) và `User` (chỉ xem).
- **Luồng điều khiển**: App → `/api/cmd` → Gateway → (LoRa mã hóa) → Node → Actuator.

## 0. Yêu cầu
- **Node.js ≥ 18** (tải tại https://nodejs.org). Kiểm tra: `node -v`, `npm -v`.
- Build APK cần thêm: **JDK 17** + **Android Studio** (hoặc Android SDK + Gradle).

## 1. Chạy bản Web (dev)
```powershell
cd D:\2025.2\Final2\app
npm install
npm run dev
```
Mở http://localhost:5173 → nhập **IP Gateway** (xem trên OLED / log serial), đăng nhập
`admin/admin123` (hoặc `user/user123`).

> Web dev khác origin với Gateway nên gọi API qua CORS — firmware Gateway đã bật sẵn
> CORS + preflight (`OPTIONS /api/*`) nên hoạt động ngay.

## 2. Build Web tĩnh (deploy)
```powershell
npm run build     # ket qua trong dist/
npm run preview   # xem thu ban build
```
Host thư mục `dist/` trên bất kỳ web server nào (hoặc chính máy tính trong LAN).

## 3. Build APK Android (Capacitor)
```powershell
npm install
npm run build
npx cap add android          # chi lan dau (tao thu muc android/)
npm run cap:sync             # copy dist/ + cau hinh sang android/
```
Sau đó chọn 1 trong 2:
- **Android Studio**: `npx cap open android` → Run/Build APK (Build ▸ Build Bundle(s)/APK(s) ▸ Build APK).
- **Dòng lệnh**: `cd android && .\gradlew.bat assembleDebug`
  → APK tại `android/app/build/outputs/apk/debug/app-debug.apk`.

### Cleartext HTTP (quan trọng cho APK)
Gateway phục vụ qua **http://** (không TLS). Capacitor đã bật cleartext trong
`capacitor.config.json` (`server.cleartext = true`, `androidScheme = "http"`).
Nếu Android vẫn chặn, thêm vào `android/app/src/main/AndroidManifest.xml` thẻ `<application ... android:usesCleartextTraffic="true">`.

## 4. Cấu trúc mã
```
src/
  api.js                 REST client (base URL, login, nodes, history, cmd) + mã lệnh
  App.jsx                khung app: polling, RBAC, toast, modal
  components/
    Login.jsx            đăng nhập + nhập IP Gateway
    StatusBar.jsx        trạng thái Gateway (IP, node online, heap, replay chặn…)
    NodeCard.jsx         thẻ 1 node + nút bật/tắt Actuator (Admin)
    NodeDetail.jsx       biểu đồ lịch sử (recharts) + điều khiển Act/AUTO/OTA/Reboot
  styles.css             giao diện tối
```

## 5. API Gateway sử dụng
| Method | Endpoint | Mô tả |
|---|---|---|
| GET | `/api/status` | Trạng thái gateway |
| GET | `/api/nodes` | Danh sách node + số liệu mới nhất |
| GET | `/api/history?node=ID` | Lịch sử 1 node |
| POST | `/api/login` | `{user,pass}` → `{role}` |
| POST | `/api/cmd` | (header `X-Token`=admin pass) `{node,cmd,act_mask,act_val,url}` |

Mã lệnh (`cmd`): `1`=đặt Actuator, `2`=AUTO(FSM), `3`=OTA, `4`=Reboot, `5`=Ping.
Bit Actuator: `1`=Quạt(Act1), `2`=Phun sương(Act2), `4`=Act3.
