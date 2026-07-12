/*
 * serial_cmd.h - Kenh USB Serial (UART0, 115200) cho HIL tool tren PC.
 *   - Gui telemetry JSON ra PC (1 dong / ban tin).
 *   - Nhan lenh JSON tu PC (1 dong / lenh) va goi callback.
 *
 * Dung chung UART0 voi console log. Tool tren PC chi can loc cac dong bat dau '{'.
 */
#pragma once

/* Callback khi nhan 1 dong lenh tu PC (da bo '\n'). Hien thuc o main.c. */
typedef void (*serial_cmd_cb_t)(const char *line);

/* Khoi tao UART driver + tao task doc lenh. cb co the NULL. */
void serial_cmd_init(serial_cmd_cb_t cb);

/* Gui 1 dong ra PC (tu dong them '\n'). An toan goi tu nhieu task. */
void serial_cmd_send_line(const char *line);
