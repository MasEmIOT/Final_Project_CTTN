# KỊCH BẢN THUYẾT TRÌNH BẢO VỆ ĐỒ ÁN TỐT NGHIỆP
**Đề tài:** Nghiên cứu thiết kế và chế tạo hệ thống IoT giám sát vi khí hậu chuồng nuôi gia cầm ứng dụng LoRa và Điện toán biên
**SVTH:** Nguyễn Tiến Đạt – 20223689 · **GVHD:** PGS.TS Trần Quang Vinh

> **Tổng thời lượng mục tiêu: ~13–14 phút.** Nói ~140 từ/phút. Các mốc [phút] là thời gian tích lũy gợi ý. Chỗ ⏩ là có thể lướt nhanh nếu quá giờ.

---

## MỞ ĐẦU — Slide 2 (Trang bìa) · [0:00 → 0:40]

Kính thưa các thầy cô trong hội đồng, kính thưa thầy giáo hướng dẫn. Em là Nguyễn Tiến Đạt, mã số sinh viên 20223689, ngành Kỹ thuật Điện tử Viễn thông. Hôm nay em xin trình bày đồ án tốt nghiệp: *"Nghiên cứu thiết kế và chế tạo hệ thống IoT giám sát vi khí hậu chuồng nuôi gia cầm ứng dụng LoRa và Điện toán biên"*, dưới sự hướng dẫn của thầy PGS.TS Trần Quang Vinh.

## Slide 3 (Nội dung) · [0:40 → 1:00]

Bài trình bày của em gồm bốn phần: giới thiệu đề tài; thiết kế và triển khai hệ thống; kết quả kiểm thử; và cuối cùng là kết luận, hướng phát triển.

---

## I. GIỚI THIỆU ĐỀ TÀI

### Slide 4 — Đặt vấn đề · [1:00 → 2:00]

Trước hết là bài toán thực tiễn. Trong chăn nuôi gia cầm tập trung mật độ cao, vật nuôi cực kỳ nhạy cảm với vi khí hậu. Gia cầm không có tuyến mồ hôi, tản nhiệt chủ yếu qua hô hấp, nên chỉ cần nhiệt độ và độ ẩm lệch nhẹ — thể hiện qua chỉ số nhiệt ẩm THI — là đàn đã bị stress nhiệt; cộng thêm khí độc NH₃ tích tụ sẽ ảnh hưởng trực tiếp đến tăng trưởng và tỷ lệ chết. Trong khi đó, giám sát thủ công thì chậm, tốn nhân công và **không thể cảnh báo, can thiệp kịp thời 24/7**. Đây chính là động lực để em xây dựng một hệ thống giám sát tự động, phản ứng tức thời.

### Slide 5 — "Nghịch lý đám mây" & giải pháp · [2:00 → 2:55]

Các hệ thống IoT truyền thống lại vướng một điểm yếu mà giới nghiên cứu gọi là *"nghịch lý đám mây"*: mọi quyết định điều khiển đều phải gửi lên máy chủ Internet rồi chờ lệnh về. Hệ quả là độ trễ khứ hồi lớn, và nghiêm trọng hơn — **mất kết nối là mất luôn khả năng giám sát và điều khiển**, đúng vào lúc đàn vật nuôi cần được bảo vệ nhất.

Giải pháp của em là đảo ngược tư duy đó: **đưa toàn bộ chức năng ra quyết định xuống ngay thiết bị Node — tức Điện toán biên** — và dùng truyền thông **LoRa** tầm xa, năng lượng thấp. Nhờ vậy thiết bị vẫn hoạt động tự chủ ngay cả khi mất mạng hoàn toàn.

### Slide 6 — Mục tiêu & đóng góp · [2:55 → 3:55]

Từ đó, đồ án đặt ra ba mục tiêu: một là chế tạo thiết bị Node tự chủ, vận hành ổn định trong môi trường chuồng trại khắc nghiệt; hai là để Điện toán biên xử lý **100% logic ra quyết định ngay tại Node**; và ba là truyền thông LoRa tầm xa, tiết kiệm năng lượng, có **bảo mật và chống tấn công phát lại**.

Về đóng góp, em đã thực hiện trọn vẹn cả phần cứng lẫn phần mềm: thiết kế và chế tạo bo mạch PCB 2 lớp cùng vỏ hộp; xây dựng firmware đa nhiệm trên FreeRTOS gồm lọc số, máy trạng thái, mã hóa có xác thực, quản lý năng lượng ngủ lai, lưu trữ ngoại tuyến và cập nhật OTA; đồng thời tự phát triển một công cụ để đo kiểm định lượng toàn hệ thống.

---

## II. THIẾT KẾ – TRIỂN KHAI HỆ THỐNG

### Slide 8 — Kiến trúc tổng thể 3 tầng · [3:55 → 4:35]

Hệ thống được thiết kế theo kiến trúc ba tầng. **Tầng thiết bị** là Node ESP32-S3 kèm cảm biến và LoRa — đây là nơi tự ra quyết định tại biên. **Tầng Gateway** dùng ESP32 kèm một máy chủ cục bộ, cho phép hệ thống chạy **không cần Internet**. Và **tầng ứng dụng** là giao diện Web/App để giám sát, điều khiển và kích hoạt cập nhật OTA.

### Slides 9–10 — Sơ đồ khối & lựa chọn công nghệ · [4:35 → 5:25]

Đi vào thiết bị Node: trung tâm là vi điều khiển **ESP32-S3 hai lõi 240 MHz**. Khối nguồn gồm ngõ vào USB-C, mạch sạc TP4056, LDO dòng tĩnh thấp. Tổ hợp cảm biến giao tiếp I2C gồm **SHT31** đo nhiệt–ẩm, **BMP180** đo áp suất, **BH1750** đo ánh sáng, cùng **RTC DS3231** làm mốc thời gian. Truyền thông dùng module **LoRa SX1278** ở băng tần 433 MHz. Đầu ra là các rơ-le điều khiển quạt, sưởi và đèn báo trạng thái. Mỗi linh kiện đều được chọn có chủ đích để cân bằng giữa hiệu năng, năng lượng và độ tin cậy.

### Slides 11–12 — Kiến trúc phần mềm đa nhiệm FreeRTOS · [5:25 → 6:30]

Đây là phần lõi phần mềm. Firmware được tổ chức đa nhiệm trên FreeRTOS với chiến lược **ghim lõi**: **Core 0** chuyên xử lý các tác vụ "chặn" là thu/phát LoRa và mã hóa; **Core 1** được giữ riêng cho Điện toán biên. Dữ liệu chảy theo mô hình sản xuất–tiêu thụ: *Sensor Task* đọc cảm biến, đẩy vào hàng đợi; *Edge FSM Task* — ưu tiên cao nhất — lấy dữ liệu, ra quyết định và đóng rơ-le **trong dưới 50 ms**, rồi đóng gói cho *Crypto Task* mã hóa và *LoRa Task* phát đi.

Điểm mấu chốt về đồng bộ: các Hàng đợi làm băng chuyền dữ liệu, còn **bus I2C dùng chung được bảo vệ bằng Mutex có kế thừa ưu tiên** để chống hiện tượng đảo ngược ưu tiên. Nhờ tách lõi như vậy, dù Core 0 đang bận vô tuyến, tác vụ an toàn trên Core 1 vẫn được thực thi tức thời — đảm bảo tính thời gian thực.

### Slides 13–21 — Các thuật toán tại biên · [6:30 → 9:00]
> *(Lướt qua các slide thuật toán, mỗi ý 1–2 câu; nhấn mạnh FSM, bảo mật, độ tin cậy.)*

Toàn bộ trí tuệ của hệ thống nằm ở chuỗi thuật toán chạy tại Node. Mỗi chu kỳ, thiết bị đọc cảm biến — với cơ chế **tự dò địa chỉ trên hai bus I2C** nên vẫn chạy được dù thiếu hay đổi vị trí cảm biến, tuyệt đối không treo.

Dữ liệu thô được **lọc nhiễu riêng theo từng tín hiệu**: EMA cho nhiệt–ẩm, trung bình trượt cho áp suất, trung vị cho ánh sáng — để tránh rơ-le đóng cắt liên tục quanh ngưỡng.

Trái tim ra quyết định là **máy trạng thái FSM**: hệ thống tính chỉ số THI rồi phân ba mức — AN TOÀN, CẢNH BÁO, KHẨN CẤP — và tự động bật quạt, phun sương tương ứng, có cơ chế **trễ hysteresis** để không dao động trạng thái. ⏩

Trước khi phát, gói tin được **mã hóa có xác thực AEAD** — chọn được AES-128-GCM tăng tốc phần cứng hoặc ASCON hạng nhẹ — kèm **nhãn thời gian để chống tấn công phát lại**.

Về độ tin cậy, hệ thống có **hai lớp phòng vệ**: khi mất Gateway, cơ chế **lưu trữ ngoại tuyến** ghi dữ liệu vào bộ đệm và gửi bù lại khi có mạng, không mất số liệu; và **watchdog tự phục hồi** sẽ reset thiết bị nếu bị treo, đảm bảo vận hành 24/7. ⏩

Cuối cùng, để bảo trì từ xa mà không cần tháo thiết bị, em xây dựng **cập nhật OTA an toàn**: tải firmware qua HTTPS, xác thực bằng SHA-256, và nếu bản mới lỗi thì **tự động rollback** về bản cũ. Song song, chế độ **ngủ lai Deep/Light Sleep** giúp tiết kiệm pin mà không mất dữ liệu trong bộ đệm.

### Slides 22–26 — Thiết kế PCB & sản phẩm · [9:00 → 9:50]

Về phần cứng, em đã thiết kế sơ đồ nguyên lý với chuỗi bảo vệ ngõ vào nhiều tầng chống quá dòng, cắm ngược và tĩnh điện, rồi quy hoạch **bo mạch in 2 lớp** tuân thủ các nguyên tắc chống nhiễu cao tần. Bo mạch đã được **gia công, hàn linh kiện và đo kiểm thực tế**, sau đó lắp vào **vỏ hộp in 3D** kích thước 13,5 × 10 × 5,5 cm — tạo thành một sản phẩm hoàn chỉnh, tiệm cận sản phẩm công nghiệp.

---

## III. KẾT QUẢ – KIỂM THỬ HỆ THỐNG

### Slide 28 — Đo đạc nguồn · [9:50 → 10:30]

Em đã kiểm thử định lượng toàn hệ thống. Trước hết là khối nguồn: đo bằng đồng hồ vạn năng ở nhiều kịch bản tải, tập trung vào lúc LoRa phát — thời điểm dễ sụt áp nhất. Kết quả, đường 3,3 V luôn giữ ổn định trong 3,2–3,3 V ngay cả khi LoRa rút dòng đỉnh. Điều này khẳng định khối nguồn đủ khỏe, **loại bỏ nguy cơ reset ngoài ý muốn do sụt áp**.

### Slide 29 — Tín hiệu bus I2C & SPI · [10:30 → 11:05]

Tiếp theo, em dùng **máy phân tích logic** bắt sóng trực tiếp các bus. Trên I2C, các điều kiện START/STOP, địa chỉ, bit ACK và dạng xung tới cả bốn cảm biến đều đúng giao thức. Trên SPI, tín hiệu điều khiển module LoRa SX1278 ổn định, ghi thanh ghi và nạp FIFO chính xác. Nghĩa là lớp vật lý giao tiếp hoàn toàn tin cậy.

### Slide 30 — Hiệu quả lọc nhiễu & độ trễ biên · [11:05 → 11:50]

Đây là hai kết quả em tâm đắc nhất. Thứ nhất, bộ lọc số giảm **độ lệch chuẩn nhiễu khoảng 67%** trên cả ba tín hiệu. Thứ hai, và quan trọng nhất — **độ trễ ra quyết định tại biên**, đo từ lúc cảm biến vượt ngưỡng đến khi đóng rơ-le bằng xung trigger, luôn **dưới 50 ms**; thực tế đo được chỉ ở mức **vài chục micro-giây**. Con số này chứng minh giá trị cốt lõi của Điện toán biên: quyết định an toàn được thực hiện **tức thời, ngay tại chỗ, không phụ thuộc Internet**.

### Slide 31 — Độ tin cậy · [11:50 → 12:25]

Về độ tin cậy, bằng công cụ kiểm thử tự phát triển, em đã kịch bản hóa các sự cố. Khi rút Gateway, bộ đệm ngoại tuyến tăng dần và được đồng bộ đầy đủ khi kết nối phục hồi — **không mất một bản ghi nào**. Khi ép thiết bị treo, **watchdog tự động reset** và hệ thống khởi động lại bình thường — đảm bảo vận hành liên tục không cần người can thiệp.

### Slides 32–33 — OTA & ra quyết định biên · [12:25 → 13:05]

Em cũng thử nghiệm thành công **nạp firmware từ xa qua OTA** — cập nhật từ phiên bản 5 lên 6 qua giao diện web. Và về chức năng cốt lõi, ba trạng thái AN TOÀN, CẢNH BÁO, KHẨN CẤP đều được kích hoạt đúng theo ngưỡng nhiệt độ, tự động điều khiển quạt và phun sương — đúng như thiết kế.

### Slide 34 — Bảng tổng hợp · [13:05 → 13:30]

Tổng hợp lại, cả **chín hạng mục kiểm thử đều ĐẠT**: từ nguồn, tín hiệu bus, lọc nhiễu, độ trễ biên, máy trạng thái, bảo mật, lưu trữ ngoại tuyến, watchdog cho đến OTA. Nghĩa là **100% chỉ tiêu định lượng đề ra đều được kiểm chứng bằng thực nghiệm**.

---

## IV. KẾT LUẬN & HƯỚNG PHÁT TRIỂN — Slide 36 · [13:30 → 14:20]

Kính thưa hội đồng, đồ án đã hoàn thành trọn vẹn các mục tiêu: em đã **chế tạo thành công một thiết bị Node Edge–LoRa hoàn chỉnh** có phần cứng và vỏ hộp thực tế; xây dựng kiến trúc đa nhiệm ghim lõi với máy trạng thái ra quyết định tại biên; tích hợp bảo mật AEAD kèm chống Replay, OTA an toàn có rollback, cùng cơ chế lưu trữ ngoại tuyến và tự phục hồi. Toàn bộ được kiểm chứng định lượng và đạt 100% chỉ tiêu.

Trong tương lai, hệ thống có thể phát triển theo bốn hướng: tích hợp **trí tuệ nhân tạo tại biên (TinyML)** để dự báo stress nhiệt; mở rộng thành **mạng LoRaWAN đa Node** cho trang trại lớn; tối ưu năng lượng cảm biến khí bằng kỹ thuật **Pulsed Heating**; và hoàn thiện App/Web để tiến tới thương mại hóa.

## Slides 37–38 — Demo & Cảm ơn · [14:20 → 14:40]

Sau đây em xin trình chiếu một video demo ngắn về sản phẩm hoạt động thực tế. *(bật video)*

Phần trình bày của em đến đây là kết thúc. **Em xin chân thành cảm ơn quý thầy cô đã lắng nghe, và rất mong nhận được các ý kiến đóng góp của hội đồng ạ.**

---

## GHI CHÚ KHI TRÌNH BÀY
- **Nếu dư giờ:** nói kỹ hơn slide 15 (bộ lọc) và slide 16 (FSM), hoặc thêm số liệu link LoRa (RSSI ≈ −41…−50 dBm, PDR cao) từ giao diện web.
- **Nếu thiếu giờ:** ở đoạn thuật toán (slide 13–21) chỉ giữ 4 ý: đọc–lọc, FSM, bảo mật, độ tin cậy; các slide OTA/Sleep lướt nhanh.
- **Câu chốt cần nhấn mạnh (nói chậm, rõ):** *"độ trễ ra quyết định chỉ vài chục micro-giây, dưới 50 ms"* và *"100% chỉ tiêu đều ĐẠT"*.
- Nhìn hội đồng khi nói câu kết mỗi phần; hạn chế đọc slide.
