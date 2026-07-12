# PHASE 4 — App/Web quản lý + điều khiển Actuator ✅

Ứng dụng ở `D:\2025.2\Final2\app` — **React + Vite + Capacitor** (1 codebase → Web + APK Android),
kết nối **Local Server** của Gateway (đã bỏ Firebase).

## 1. Tính năng (ánh xạ yêu cầu)
- **Web-based + build APK**: cùng một mã nguồn; `npm run dev`/`build` (web) và Capacitor (APK).
- **UI/UX**: giao diện tối, thẻ node realtime, badge FSM (SAFE/WARN/EMERGENCY), biểu đồ lịch sử (recharts), status bar Gateway.
- **Phân quyền Admin/User**: đăng nhập qua `/api/login`; `Admin` thấy nút điều khiển, `User` chỉ xem. Nút điều khiển gửi kèm `X-Token` (mật khẩu admin) — Gateway chặn nếu không phải Admin.
- **Điều khiển Actuator từ xa**: bật/tắt Quạt (Act1), Phun sương (Act2), Act3; nút **AUTO** trả về điều khiển FSM; **OTA** (nhập URL firmware); **Reboot**. Đúng luồng **App → Gateway → Node → Actuator**.

## 2. Cấu trúc
```
app/
  package.json  vite.config.js  capacitor.config.json  index.html
  src/api.js                 REST client + mã lệnh (CMD/ACT)
  src/App.jsx                polling 2s, RBAC, toast, modal
  src/components/Login.jsx   đăng nhập + IP Gateway
  src/components/StatusBar.jsx
  src/components/NodeCard.jsx     thẻ node + toggle actuator
  src/components/NodeDetail.jsx   biểu đồ + điều khiển Act/AUTO/OTA/Reboot
  src/styles.css
  README.md                  hướng dẫn build web + APK chi tiết
```

## 3. Chạy nhanh
```powershell
cd D:\2025.2\Final2\app
npm install
npm run dev            # web: http://localhost:5173  (nhap IP Gateway, admin/admin123)
# APK:
npm run build && npx cap add android && npm run cap:sync
cd android && .\gradlew.bat assembleDebug   # -> app-debug.apk
```

## 4. Kết nối
- App gọi REST của Gateway: `/api/status`, `/api/nodes`, `/api/history?node=`, `/api/login`, `/api/cmd`.
- Gateway đã bật **CORS + preflight OPTIONS** nên web khác origin gọi được.
- APK dùng **cleartext HTTP** (đã cấu hình trong `capacitor.config.json`).

## 5. Ghi chú
- 2 tài khoản demo trong `LoraGateway/main/gw_config.h`: `admin/admin123`, `user/user123` (đổi được).
- Máy build cần **Node.js ≥ 18**; build APK cần **JDK 17 + Android Studio/SDK**.
- Ngoài app này, Gateway còn 1 **dashboard nhúng** tại `http://<IP>/` để test nhanh không cần build.
