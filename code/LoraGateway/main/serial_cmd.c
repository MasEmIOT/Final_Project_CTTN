#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "serial_cmd.h"

/* UART console (USB Serial). Mac dinh la UART0. */
#ifdef CONFIG_ESP_CONSOLE_UART_NUM
#define CONSOLE_UART   CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CONSOLE_UART   UART_NUM_0
#endif

#define RX_LINE_MAX    256

static serial_cmd_cb_t s_cb;
static SemaphoreHandle_t s_tx_lock;

void serial_cmd_send_line(const char *line)
{
    if (s_tx_lock) xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    /* printf di ra UART0 - cung kenh voi log. Them \n de tach dong. */
    printf("%s\n", line);
    fflush(stdout);
    if (s_tx_lock) xSemaphoreGive(s_tx_lock);
}

static void rx_task(void *arg)
{
    (void)arg;
    char line[RX_LINE_MAX];
    int idx = 0;

    while (1) {
        uint8_t ch = 0;
        int n = uart_read_bytes(CONSOLE_UART, &ch, 1, pdMS_TO_TICKS(200));
        if (n <= 0) {
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[idx] = '\0';
            if (idx > 0 && s_cb) {
                s_cb(line);
            }
            idx = 0;
            continue;
        }
        if (idx < RX_LINE_MAX - 1) {
            line[idx++] = (char)ch;
        } else {
            idx = 0;   /* dong qua dai -> bo */
        }
    }
}

void serial_cmd_init(serial_cmd_cb_t cb)
{
    s_cb = cb;
    s_tx_lock = xSemaphoreCreateMutex();

    /* Cai UART driver de doc duoc byte tu PC (TX van do printf lo). */
    if (!uart_is_driver_installed(CONSOLE_UART)) {
        uart_driver_install(CONSOLE_UART, 512, 0, 0, NULL, 0);
    }

    xTaskCreate(rx_task, "serial_rx", 4096, NULL, 6, NULL);
}
