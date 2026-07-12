# DEEP-DIVE CODE — Giải thích chi tiết 5 khối cốt lõi

> Dùng để "phòng thủ" khi hội đồng soi code. Mỗi khối: **ý nghĩa tổng thể → chú thích từng đoạn → cách modify → câu hỏi có thể gặp**.

---

# 1) `main.c` — Trái tim chu trình Node

## 1.1 `sensor_cycle_task` — vòng lặp một chu kỳ
Đây là task duy nhất chạy nghiệp vụ node, lặp vô hạn, mỗi vòng = 1 chu kỳ đo–xử lý–gửi–ngủ.

```c
static void sensor_cycle_task(void *arg) {
    esp_task_wdt_add(NULL);        // (1) đăng ký task này với Watchdog
    uint16_t seq = s_seq;          // (2) tiếp tục số thứ tự gói (giữ qua deep sleep bằng RTC)
    while (1) {
        int64_t t_start = esp_timer_get_time();  // (3) mốc đo thời gian xử lý
        esp_task_wdt_reset();      // (4) "vỗ về" watchdog: báo task còn sống
        wdt_test_check();          // (5) nếu chập GPIO0->GND thì treo giả lập để test WDT
```
- **(1)** `esp_task_wdt_add(NULL)`: task tự đăng ký với **Task Watchdog Timer**. Nếu sau đó không `reset` trong `NODE_WDT_TIMEOUT_S` giây → chip reset (tự phục hồi khi treo).
- **(2)** `seq` đọc từ `s_seq` (biến `RTC_DATA_ATTR`) → số gói **liên tục** kể cả sau deep sleep.
- **(3)** `esp_timer_get_time()` = bộ đếm **micro-giây** độ phân giải cao → dùng đo `proc_us`, `enc_us`, `rtt`, `decide_us`.
- **(4)** `esp_task_wdt_reset()` gọi đều mỗi vòng; nếu vòng lặp treo → watchdog can thiệp.
- **(5)** `wdt_test_check()` đọc GPIO0; nếu =0 (chập xuống GND) thì vào vòng lặp vô hạn **không** feed watchdog → chứng minh cơ chế WDT khi bảo vệ.

```c
        app_payload_t pl; memset(&pl, 0, sizeof(pl));
        pl.node_id = s_node_id;    // ID tự sinh từ MAC
        pl.seq = seq;
        pl.algo = CRYPTO_ALGO;

        float v1, v2;
        if (s_has_sht && sht3x_read(&s_sht, &v1, &v2) == ESP_OK) {
            pl.sht_temp_c = v1; pl.sht_hum_pct = v2; pl.flags |= PKT_F_SHT30_OK; }
        if (s_has_bmp && bmp180_read(&s_bmp, &v1, &v2) == ESP_OK) {
            pl.bmp_temp_c = v1; pl.bmp_press_hpa = v2; pl.flags |= PKT_F_BMP180_OK; }
        if (s_has_bh && bh1750_read(&s_bh, &v1) == ESP_OK) {
            pl.lux = v1; pl.flags |= PKT_F_BH1750_OK; }
```
- Chỉ đọc cảm biến **có mặt** (`s_has_*` xác định lúc `sensors_init` probe I2C). Đọc lỗi → **không set cờ** `PKT_F_*_OK` → gateway/app biết dữ liệu đó không hợp lệ (không gửi số rác).

```c
        #define CO2_EVERY 3
        static uint32_t s_cyc = 0; s_cyc++;
        if (s_has_mq) { uint16_t nh3,ch4; int mv;
            if (mq135_read(&s_mq,&nh3,&ch4,&mv)==ESP_OK){ s_nh3_ppm=nh3; s_ch4_ppm=ch4; pl.flags|=PKT_F_MQ135_OK; } }
        if (s_has_co2 && (s_cyc % CO2_EVERY == 1)) {          // CO2 UART chậm -> đọc 1/3 chu kỳ
            uint16_t co2; if (mhz19_read_co2(&s_co2,&co2)==ESP_OK){ s_co2_ppm=co2; pl.flags|=PKT_F_CO2_OK; } }
        else if (s_has_co2 && s_co2_ppm>0) pl.flags|=PKT_F_CO2_OK;  // giữ giá trị cũ
        pl.nh3_ppm=s_nh3_ppm; pl.co2_ppm=s_co2_ppm; pl.ch4_ppm=s_ch4_ppm;
        memcpy(pl.mac, s_mac, 6);                             // đính MAC (được xác thực)
        if (s_has_rtc) pl.flags |= PKT_F_RTC_OK;
```
- **Scheduling nguồn**: MQ135 (ADC nhanh) đọc mỗi chu kỳ; **CO₂ (UART ~200ms) đọc luân phiên mỗi 3 chu kỳ**, giữa các lần dùng giá trị cũ → giảm tải/dòng. Đổi `CO2_EVERY` để đọc thưa/dày.

```c
        s_f_t = filter_apply(&s_filt_t, pl.sht_temp_c);   // EMA
        s_f_h = filter_apply(&s_filt_h, pl.sht_hum_pct);  // EMA
        s_f_press = filter_apply(&s_filt_press, pl.bmp_press_hpa); // Moving Average
        s_f_lux = filter_apply(&s_filt_lux, pl.lux);      // Median
```
- **Lọc tại biên**: mỗi tín hiệu một bộ lọc phù hợp đặc tính vật lý (trạng thái lọc giữ trong RTC để sống qua deep sleep). Đổi loại lọc ở `NODE_FILTER_*` đầu file.

```c
        pl.rtt_ms=s_last_rtt_ms; pl.dist_dm=s_last_dist_dm; pl.dl_rssi=s_last_dl_rssi; pl.enc_us=s_last_enc_us;
        s_tx_total++; pl.tx_total=s_tx_total; pl.ack_total=s_ack_total;
        uint32_t cyc_ms = s_last_cycle_ms ? s_last_cycle_ms : NODE_SEND_INTERVAL_S*1000;
        pl.pph = 3600000UL / cyc_ms;      // gói/giờ (băng thông)
        pl.sps_x100 = 100000UL / cyc_ms;  // sample/giây ×100
        pl.heap_kb = esp_get_free_heap_size()/1024;  // RAM trống -> theo dõi rò rỉ
```
- Node **tự tính pipeline** (edge): PDR = ack/tx, băng thông pph, tốc độ mẫu, heap. Các mốc "chu kỳ trước" (rtt/dist/dl_rssi/enc) đưa vào gói này để gateway/tool theo dõi liên tục.

```c
        actuator_tick();                  // manual hết 60s -> Auto
        fsm_input_t fin = { .temp_c=..., .hum_pct=..., .nh3_ppm=s_nh3_ppm, .co2_ppm=s_co2_ppm,
                            .have_th=..., .have_nh3=..., .have_co2=... };
        fsm_output_t fout;
        fsm_update(&fin, s_fsm_state, true, &fout);   // EDGE DECISION (đo decide_us bên trong)
        s_fsm_state=fout.state; s_act_state=fout.act_mask;
        pl.fsm=fout.state; pl.thi_x10=fout.thi_x10; pl.decide_us=fout.decide_us; pl.act_state=fout.act_mask;
        pl.act_mode = actuator_is_manual()?ACT_MODE_MANUAL:ACT_MODE_AUTO;
        pl.manual_left_s = actuator_manual_left_s();
        pl.fw_ver = NODE_FW_VERSION;
        if (s_ota_record_pending) { uint32_t now=node_now_epoch();
            if (now>1600000000){ s_ota_epoch=now; ota_epoch_save(now); s_ota_record_pending=false; } }
        pl.ota_epoch = s_ota_epoch;
```
- **Trung tâm edge computing**: `fsm_update` tính THI, so ngưỡng, điều khiển actuator ngay. `decide_us` = độ trễ ra quyết định (đo trong FSM).
- Ghi `ota_epoch` (thời điểm firmware này bắt đầu chạy) vào NVS lần đầu có giờ hợp lệ → hiển thị "OTA lúc nào" trên web.

```c
        pl.epoch = node_now_epoch();              // nhãn thời gian (từ RTC) -> chống replay + offline
        pl.buf_count=s_off_count; ...             // trạng thái buffer offline
        uint32_t proc_us = (esp_timer_get_time()-t_start);  // thời gian xử lý
        pl.proc_us = proc_us;

        uint8_t frame[ENV_MAX_LEN]; size_t flen=0;
        int64_t e0 = esp_timer_get_time();
        int sr = packet_seal(&pl, CRYPTO_ALGO, CRYPTO_KEY, frame, &flen);  // MÃ HÓA
        s_last_enc_us = esp_timer_get_time()-e0;   // enc_us: thời gian mã hóa (so AES vs ASCON)
```
- `proc_us` bọc từ đầu chu kỳ đến trước mã hóa. `enc_us` bọc riêng quanh `packet_seal` → **đo chi phí mã hóa** để so sánh AES/ASCON.

```c
        int retries = (s_offline_streak>0) ? 0 : CONFIG_NODE_ACK_RETRIES;  // offline: fail nhanh, đỡ tốn CPU
        bool acked = send_with_ack(frame, flen, seq, &rtt, &dl, &ul, &dl_snr, retries);
        if (acked) {
            s_ack_total++; s_offline_streak=0;
            s_last_rtt_ms=rtt; s_last_dl_rssi=dl; s_last_dist_dm=estimate_distance_dm(dl);  // RSSI->distance
            if (s_off_count>0) flush_backlog();    // nối lại -> gửi bù dữ liệu offline
        } else {
            s_offline_streak++;
            off_push(&pl, pl.epoch);               // MẤT KẾT NỐI -> lưu ring buffer + nhãn thời gian
        }
        seq++; s_seq=seq;
```
- **Store-and-forward**: có ACK → online, cập nhật RTT/khoảng cách, và **gửi bù** dữ liệu đã lưu. Không ACK → **lưu offline** (drop-oldest khi đầy). Khi offline **fail nhanh (0 retry)** để tiết kiệm điện/CPU.

```c
        uint32_t cycle_ms = NODE_SEND_INTERVAL_S*1000;
        uint32_t awake_ms = (esp_timer_get_time()-t_start)/1000;
        bool can_deep = acked && (s_off_count==0) && (s_act_state==0) && !actuator_is_manual();
        uint32_t sleep_ms = (awake_ms<cycle_ms) ? (cycle_ms-awake_ms) : MIN_BACKOFF_SLEEP_MS;
        s_cpu_pct = awake_ms*100/(awake_ms+sleep_ms);  // %CPU = thức/(thức+ngủ)
        node_emit_telemetry(&pl, acked, dl, dl_snr, rtt);  // JSON ra USB
        rtos_stats_emit_json(s_node_id);                   // task list + %CPU từng core
        node_sleep(sleep_ms, can_deep);                    // deep -> reset; light -> chạy tiếp
    }
}
```
- **Quyết định ngủ**: chỉ deep sleep khi online + buffer rỗng + không actuator + không manual (để giữ RAM buffer & relay). `sleep_ms = chu kỳ − thời gian thức` → giữ nhịp lấy mẫu đều.

**Modify:** thêm cảm biến → thêm khối đọc + field payload; đổi nhịp → `NODE_SEND_INTERVAL_S`/`CO2_EVERY`; đổi telemetry → sửa `node_emit_telemetry`.

## 1.2 `send_with_ack` — bắt tay 2 chiều + đo RTT + nhận lệnh
```c
for (attempt=0..max_retries) {
    int64_t t_tx = esp_timer_get_time();                    // (A) mốc trước khi phát
    sx127x_send(frame, flen, LORA_TX_TIMEOUT_MS);           // phát gói (blocking đến TxDone)
    sx127x_start_rx();                                      // chuyển sang THU chờ ACK
    while ((esp_timer_get_time()-t0) < ACK_TIMEOUT) {       // (B) cửa sổ chờ ACK
        sx127x_receive(buf,&len,&rssi,&snr);
        if (len != ack_size && len >= header+tag) handle_downlink_cmd(buf,len);  // (C) nhận LỆNH
        if (len == ack_size) {
            if (ack_valid && ack.node_id==s_node_id && ack.seq==seq) {           // (D) ACK đúng
                if (ack.epoch>0){ đồng bộ giờ; nếu RTC chưa set -> ds3231_set_epoch(ack.epoch); }
                rtt = (esp_timer_get_time()-t_tx)/1000;      // (E) RTT = khứ hồi (ms)
                return true;
            }
        }
        vTaskDelay(1); esp_task_wdt_reset();
    }
}
return false;   // hết retry -> mất kết nối
```
- **(A)+(E)** = cách đo **RTT**: hiệu 2 mốc `esp_timer` từ lúc phát đến lúc nhận ACK.
- **(C)** trong lúc chờ ACK, nếu nhận gói **không phải ACK** (kích thước khác) và đủ dài → thử giải mã như **lệnh downlink** → đây là cửa sổ node nhận lệnh điều khiển/OTA.
- **(D)** ACK phải hợp lệ CRC + đúng `node_id` + đúng `seq` mới chấp nhận (chống nhầm gói).
- Gateway nhét `epoch` (giờ SNTP) vào ACK → node **đồng bộ đồng hồ** (và nạp vào DS3231 nếu RTC chưa có giờ).
- Trả `dl_rssi` (RSSI của ACK, để tính khoảng cách), `ul_rssi` (RSSI gateway đo được).

**Câu hỏi:** *"Vì sao chờ ACK bằng vòng lặp `esp_timer` chứ không ngắt?"* → Dùng timeout theo **thời gian tuyệt đối** `esp_timer` để không phụ thuộc tick, tránh treo; vẫn `vTaskDelay(1)` nhường CPU + feed watchdog.

## 1.3 `handle_downlink_cmd` — thực thi lệnh + chống replay lệnh
```c
if (cmd_open(buf,len,CRYPTO_KEY,&cp,&algo)!=0) return false;   // giải mã + xác thực lệnh
if (cp.node_id!=s_node_id && cp.node_id!=0xFF) return false;   // đúng node (0xFF=broadcast)
if (cp.cmd_seq!=0 && cp.cmd_seq<=s_last_cmd_seq) return true;  // CHỐNG REPLAY LỆNH: seq phải tăng
s_last_cmd_seq = cp.cmd_seq;
switch (cp.cmd) {
  case CMD_ACT_SET:  actuator_set_manual(true);                // vào manual 60s
                     mask = (cur & ~act_mask) | (act_val & act_mask); actuator_set_mask(mask); break;
  case CMD_ACT_AUTO: actuator_set_manual(false); break;        // về Auto (FSM)
  case CMD_OTA:      strncpy(s_ota_url,...); xSemaphoreGive(s_ota_sem); break;  // đánh thức Task_OTA
  case CMD_REBOOT:   esp_restart(); break;
}
```
- Lệnh **được mã hóa + xác thực** (`cmd_open`) → chỉ gateway có khóa mới ra lệnh được.
- **Chống replay lệnh** bằng `cmd_seq` đơn điệu tăng (`RTC_DATA_ATTR`): phát lại lệnh cũ bị bỏ.
- `CMD_OTA` chỉ `give` semaphore → **Task_OTA** (ưu tiên cao hơn) tự chạy, không làm nghẽn task cảm biến.

---

# 2) `fsm.c` (edge decision) + `actuator.c`

## 2.1 `fsm.c` — máy trạng thái 3 mức + hysteresis
```c
static float compute_thi(float t, float h){ return 0.8f*t + (h/100.0f)*(t-14.4f) + 46.4f; }  // THI
```
```c
void fsm_update(in, cur, apply_act, out){
  int64_t t0 = esp_timer_get_time();               // (1) bắt đầu đo decide_us
  float thi = in->have_th ? compute_thi(t,h) : 0;
  if ( (have_th && t>T_EMERG) || (have_nh3 && nh3>NH3_EMERG)
    || (have_co2 && co2>CO2_EMERG) || (have_th && thi>THI_EMERG) ) {
       state=EMERGENCY; mask = ACT1|ACT2|ACT3;      // (2) khẩn cấp: bật cả 3
  } else if (t>T_WARN || nh3>NH3_WARN || co2>CO2_WARN || thi>THI_WARN) {
       state=WARN; mask = ACT1;                      // (3) cảnh báo: chỉ quạt
  } else {
       bool below_clear = (t<T_CLEAR && thi<THI_CLEAR) && nh3<NH3_CLEAR && co2<CO2_CLEAR;
       if (cur!=SAFE && !below_clear) { state=cur; mask=...; }  // (4) HYSTERESIS: giữ trạng thái
       else { state=SAFE; mask=0; }                 // (5) an toàn: tắt hết
  }
  if (apply_act && !actuator_is_manual()) actuator_set_mask(mask);  // (6) chỉ ghi khi KHÔNG manual
  out->state=state; out->act_mask=...; out->thi_x10=thi*10;
  out->decide_us = esp_timer_get_time()-t0;         // (7) độ trễ ra quyết định
}
```
- **(1)+(7)** đo **`decide_us`** = độ trễ thuật toán edge (chứng minh <<50ms).
- **(2)(3)(5)** ánh xạ trạng thái → tổ hợp actuator: EMERGENCY bật hết, WARN chỉ quạt, SAFE tắt.
- **(4) Hysteresis**: đã ở WARN/EMERGENCY thì phải xuống **dưới ngưỡng `*_CLEAR`** (thấp hơn ngưỡng lên) mới hạ cấp → **chống relay đóng/cắt liên tục** khi giá trị dao động quanh ngưỡng.
- **(6)** Nếu App đang **manual override** thì FSM **không ghi đè** chân (chỉ tính state để báo cáo).
- Mỗi điều kiện có `have_*` bảo vệ: cảm biến nào **thiếu** thì bỏ qua khỏi so sánh (không kích nhầm).

**Modify:** đổi số ở `#define *_EMERG/_WARN/_CLEAR`; đổi `mask=` để thay tổ hợp actuator; thêm biến vào `fsm_input_t` + điều kiện nếu có cảm biến mới.

## 2.2 `actuator.c` — điều khiển relay + chống nháy + manual có hạn
```c
static void apply_bit(uint8_t bit, bool on){
    gpio_num_t pin = pin_of(bit);
    gpio_hold_dis(pin);                               // (1) mở khóa để đổi mức
    gpio_set_level(pin, on ? ACT_ON_LEVEL : !ACT_ON_LEVEL);  // (2) ACT_ON_LEVEL=0 vì relay active-LOW
    gpio_hold_en(pin);                                // (3) KHÓA mức -> giữ qua light-sleep, không nháy
}
```
- **(1)(3) `gpio_hold`**: latch mức chân để **relay không nháy** khi node light-sleep. Đổi mức phải `hold_dis` trước, `hold_en` sau.
- **(2) `ACT_ON_LEVEL`**: đặt trong `board_pins.h`. **=0** cho module relay kích mức thấp (bật khi chân=0); =1 nếu active-high.

```c
void actuator_set_manual(bool manual){
    s_manual = manual;
    if (manual) s_manual_until_us = esp_timer_get_time() + MANUAL_TIMEOUT_S*1000000;  // hạn 60s
    else s_manual_until_us = 0;
}
void actuator_tick(void){                              // gọi mỗi chu kỳ
    if (s_manual && esp_timer_get_time() >= s_manual_until_us) s_manual = false;  // hết 60s -> Auto
}
```
- **Manual có hạn**: mỗi lệnh tay đặt lại hạn 60s; `actuator_tick()` (gọi đầu mỗi chu kỳ) tự **chuyển về Auto** khi hết hạn → đúng yêu cầu "manual chỉ 1 phút".

---

# 3) `packet.c` + `crypto.c` — đóng gói AEAD + chống replay

## 3.1 `packet.c` — dựng/mở "phong bì" mã hóa
Cấu trúc trên đường truyền: `[env_header (bản rõ)] [ciphertext] [tag 16B]`.
```c
static int env_seal(magic0,magic1, pl,plen, node_id, algo, key, out,out_len){
    env_header_t *h = out;
    h->magic0=magic0; h->magic1=magic1; h->version=PKT_VERSION; h->algo=algo;
    h->node_id=node_id; h->plen=plen;
    esp_fill_random(h->nonce, 16);                    // (1) nonce NGẪU NHIÊN mỗi gói
    const uint8_t *aad = out; size_t aad_len = offsetof(env_header_t,nonce);  // (2) AAD = 6 byte header đầu
    uint8_t *ct = out+sizeof(env_header_t), *tag = ct+plen;
    crypto_aead_encrypt(algo,key,h->nonce, aad,aad_len, pl,plen, ct,tag);     // (3) mã hóa + sinh tag
    *out_len = sizeof(env_header_t)+plen+ENV_TAG_LEN;
}
```
- **(1) nonce ngẫu nhiên** (`esp_fill_random`) → cùng dữ liệu vẫn ra ciphertext khác nhau (chống phân tích).
- **(2) AAD (Associated Data)**: header để **bản rõ** (gateway cần đọc node_id/algo TRƯỚC khi giải mã) nhưng được **đưa vào xác thực** → sửa header là tag hỏng.
- **(3) AEAD** vừa mã hóa vừa sinh **tag 16B** = "chữ ký" chống giả mạo.

```c
static int env_open(magic0,magic1, buf,len, plen_expect, key, pl_out, algo_out){
    if (len < header+tag) return -1;
    if (h->magic0!=magic0 || h->magic1!=magic1 || h->version!=PKT_VERSION) return -2;  // lọc nhanh
    if (h->plen != plen_expect) return -3;
    if (len != header+plen+tag) return -4;
    if (crypto_aead_decrypt(h->algo,key,h->nonce, aad,aad_len, ct,plen, tag, pl_out) != 0)
        return -5;   // TAG SAI -> giả mạo/sai khóa -> BỎ
}
```
- Giải mã **kiểm tag**: sai khóa hoặc bị sửa 1 bit → trả lỗi → gateway bỏ gói. Đây là lý do **chỉ gateway có đúng `CRYPTO_KEY` mới đọc được**.
- `packet_seal/open` (uplink, magic `A5 5A`) và `cmd_seal/open` (downlink, magic `C3 3C`) đều gọi lõi chung `env_seal/open` — chỉ khác magic + kích thước payload.

> **Anti-replay KHÔNG nằm ở đây** (AEAD chỉ chống giả mạo). Chống phát lại thực hiện ở **Gateway** bằng nhãn thời gian `epoch` (mục 5).

## 3.2 `crypto.c` — lớp AEAD thống nhất + định danh MAC
```c
const char *crypto_algo_name(algo){ AES->"AES-128-GCM"; ASCON->"ASCON-128"; }
void crypto_get_mac(mac){ esp_read_mac(mac, ESP_MAC_WIFI_STA); }        // MAC eFuse
uint8_t crypto_node_id_from_mac(){ x=XOR 6 byte MAC; return x%254 + 1; } // ID 1..254
```
```c
static int aes_gcm_enc(key,nonce,aad,aad_len,pt,pt_len,ct,tag){
    psa_import_key(...AES 128, GCM...);                 // dùng PSA Crypto (ESP-IDF v6) -> tăng tốc phần cứng
    psa_aead_encrypt(...);  // PSA gộp ct+tag -> tách ra 2 con trỏ ct, tag
}
int crypto_aead_encrypt(algo, ...){ switch(algo){
    case AES:  return aes_gcm_enc(...);                 // AES-128-GCM (phần cứng)
    case ASCON:ascon128_encrypt(...); return 0;         // ASCON-128 (phần mềm, hạng nhẹ NIST)
    default:   memcpy(ct,pt); memset(tag,0);            // NONE (chỉ debug)
}}
```
- **Trừu tượng hóa**: nghiệp vụ chỉ chọn `CRYPTO_ALGO` (trong `crypto_cfg.h`), lõi tự định tuyến AES/ASCON. Cho phép **so sánh định lượng** 2 thuật toán (đo `enc_us`).
- AES-128-GCM qua **PSA Crypto** → dùng bộ tăng tốc mã hóa phần cứng của ESP32-S3.

**Modify gói tin:** sửa `app_payload_t` trong `packet.h`, **bump `PKT_VERSION`**, copy `packet.*`+`crypto.*`+`ascon.*` sang cả 2 project, kiểm tổng ≤128 byte.

---

# 4) `ota_update.c` — OTA theo khối + rollback

```c
esp_err_t ota_do_update(const char *url){
    emit_ota("start",0,url);                            // (1) báo tiến trình JSON ra USB
    if (wifi_node_connect(SSID,PASS,20000)!=OK){ emit_ota("error","wifi_fail"); return; }  // (2) bật WiFi
    esp_http_client_config_t http = { .url=url, .timeout_ms=15000 };
    if (url bắt đầu "https://") http.crt_bundle_attach = esp_crt_bundle_attach;  // (3) HTTPS: CA bundle chống MITM
    esp_https_ota_begin(&ota_cfg, &h);                  // (4) quét partition -> chọn slot dự phòng, erase
    int total = esp_https_ota_get_image_size(h);
    while ((err=esp_https_ota_perform(h)) == IN_PROGRESS) {   // (5) TẢI THEO KHỐI
        int pct = 5 + done*90/total; emit_ota("downloading",pct); // ~10KB RAM tĩnh suốt quá trình
    }
    if (!complete_data_received(h)){ emit_ota("error","download_incomplete"); abort(); return; }
    emit_ota("verifying",96,"SHA-256");
    err = esp_https_ota_finish(h);                      // (6) VERIFY SHA-256 + set boot partition
    if (err==ESP_ERR_OTA_VALIDATE_FAILED){ emit_ota("error","validate_failed"); return; }  // sai 1 bit -> hủy
    emit_ota("reboot",100); esp_restart();              // (7) reboot vào firmware mới (PENDING_VERIFY)
}
```
- **(2)** WiFi **chỉ bật khi OTA** (bình thường node tắt WiFi để tiết kiệm + tránh xung đột ADC2).
- **(3)** HTTPS dùng **CA bundle** → chống Man-In-The-Middle; HTTP local thì bỏ cert.
- **(4)(5)** `esp_https_ota` ghi vào **slot OTA dự phòng** (`ota_0`↔`ota_1`), **tải theo khối** (chunked) nên RAM tĩnh chỉ ~10KB dù firmware 1.5MB.
- **(6)** `esp_https_ota_finish` **băm SHA-256** ảnh vừa ghi, sai 1 bit (nhiễu RF) → `VALIDATE_FAILED` → hủy, giữ firmware cũ.

**Rollback state machine** (trong `app_main`):
```c
if (ota_image_pending()) {          // firmware mới đang PENDING_VERIFY
    // ... đã init I2C/cảm biến/LoRa thành công tới đây ...
    ota_mark_valid();               // (8) TỰ XÁC NHẬN -> hủy rollback, giữ bản mới
    s_ota_record_pending = true;    // ghi thời điểm OTA
}
// Nếu bản mới TREO trước khi tới ota_mark_valid() -> Watchdog reset
//   -> bootloader thấy vẫn PENDING + vừa reset bất thường -> ROLLBACK bản cũ tự động
```
- **(8)** Chìa khóa an toàn: firmware mới **phải tự chứng minh** ngoại vi OK rồi mới `esp_ota_mark_app_valid_cancel_rollback()`. Nếu lỗi/treo → không kịp gọi → WDT reset → **bootloader tự quay về bản cũ** → không bao giờ "chết" thiết bị.

**Câu hỏi:** *"Node ở xa làm sao biết OTA xong?"* → Node gửi `fw_ver` mới qua LoRa; gateway/web thấy version đổi = thành công (không cần cắm node).

---

# 5) Gateway `lora_rx_task` — giải mã → chống replay → downlink → ACK

```c
while (1) {
    sx127x_receive(buf,&len,&rssi,&snr);                 // (1) thu gói LoRa
    if (len >= header+tag) {
        if (packet_open(buf,len,CRYPTO_KEY,&pl,&algo) != 0) { continue; }  // (2) GIẢI MÃ + XÁC THỰC
        if (s_replay_capture) { lưu buf để test replay; }                  // (phục vụ HIL)

        // (3) ===== CHỐNG REPLAY theo timestamp =====
        uint32_t gwep = gw_epoch_now();                  // giờ SNTP của gateway
        bool is_backlog = (pl.flags & PKT_F_BACKLOG);
        if (!is_backlog && gwep>0 && pl.epoch>0) {
            uint32_t diff = |gwep - pl.epoch|;
            if (diff > REPLAY_WINDOW_S) {                // lệch > 5s
                s_replay_drops++; sec_emit_replay(...);  // BỎ GÓI, KHÔNG ACK
                continue;
            }
        }
        node_store_update(&pl, rssi, snr);               // (4) lưu kho RAM (phục vụ App/Web)
        bool sent_cmd = send_downlink_if_any(pl.node_id);// (5) GỬI LỆNH downlink TRƯỚC ACK
        send_ack(pl.node_id, pl.seq, rssi, snr, sent_cmd?ACK_STATUS_CMD:ACK_STATUS_OK);  // (6) ACK
        emit_node_line(&pl, rssi, snr);                  // (7) forward JSON ra USB cho HIL
    }
}
```
- **(2)** `packet_open` giải mã + kiểm tag; **sai khóa/giả mạo → bỏ**. Đây là "chỉ gateway được chỉ định mới đọc được".
- **(3) Chống replay**: so `epoch` trong gói với **giờ SNTP**; lệch > **5s** (`REPLAY_WINDOW_S`) và **không phải backlog** → **loại gói + không ACK** (kẻ tấn công phát lại gói cũ sẽ mang timestamp cũ → bị chặn). Gói backlog (offline gửi bù) **miễn trừ** vì mang timestamp cũ hợp lệ.
- **(5) Downlink TRƯỚC ACK**: node đang ở cửa sổ RX chờ ACK; gateway gửi **lệnh đã mã hóa** trước, rồi mới ACK → node xử lý lệnh xong mới kết thúc chu kỳ (đây là đường App→Gateway→Node→Actuator/OTA).

```c
static void send_ack(node_id, seq, ul_rssi, ul_snr, status){
    ack_pkt_seal(&ack, node_id, seq, ul_rssi, snr8, status, gw_epoch_now());  // ACK kèm RSSI + epoch
    sx127x_send(&ack, sizeof(ack), 1000); sx127x_start_rx();
}
```
- ACK không mã hóa (chỉ **CRC8**) nhưng mang: RSSI/SNR uplink (để node tính khoảng cách), `status`, và **epoch** (đồng bộ giờ cho node).

```c
static bool send_downlink_if_any(node_id){
    if (!node_store_take_cmd(node_id,&cp)) return false;   // lấy lệnh từ hàng đợi (App đặt qua /api/cmd)
    cp.epoch = gw_epoch_now();
    cmd_seal(&cp, CRYPTO_ALGO, CRYPTO_KEY, frame, &flen);  // MÃ HÓA lệnh
    sx127x_send(frame, flen, 1000);
    if (cp.cmd == CMD_OTA) node_store_mark_ota(node_id, gw_epoch_now());  // đánh dấu để web theo dõi OTA
    return true;
}
```
- Lệnh từ App vào **hàng đợi** (`node_store_queue_cmd` trong `/api/cmd`), đến khi node uplink thì gateway **gửi kèm** (vì LoRa half-duplex, node chỉ nghe trong cửa sổ RX).

**Modify:** đổi cửa sổ chống replay → `REPLAY_WINDOW_S` trong `packet.h`; thêm trường forward → sửa `emit_node_line` (HIL) + `node_store_json_list` (web).

---

## PHỤ LỤC — Bảng "modify nhanh"
| Muốn đổi | File | Chỗ |
|---|---|---|
| Ngưỡng FSM | `fsm.c` | `#define *_EMERG/_WARN/_CLEAR` |
| Tổ hợp actuator theo trạng thái | `fsm.c` | `mask = ACT1_BIT \| ...` |
| Mức kích relay (active low/high) | `board_pins.h` | `ACT_ON_LEVEL` |
| Thời gian manual | `packet.h` | `MANUAL_TIMEOUT_S` |
| Chu kỳ / nhịp lấy mẫu | menuconfig | `NODE_SEND_INTERVAL_S`, `CO2_EVERY` (main.c) |
| Thuật toán / khóa mã hóa | `crypto_cfg.h` | `CRYPTO_ALGO`, `CRYPTO_KEY` |
| Cửa sổ chống replay | `packet.h` | `REPLAY_WINDOW_S` |
| Trường gói tin | `packet.h` | `app_payload_t` + bump `PKT_VERSION` |
| Chân phần cứng | `board_pins.h` | `PIN_*` |
| Phiên bản firmware (OTA) | `app_version.h` | `NODE_FW_VERSION` |
| WiFi OTA của node | menuconfig | `NODE_OTA_WIFI_SSID/PASS` |
| Mô hình RSSI→khoảng cách | `main.c` | `RSSI_REF_1M`, `PATH_LOSS_N` |
