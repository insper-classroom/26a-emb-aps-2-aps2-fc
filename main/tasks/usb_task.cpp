// ============================================================================
// usb_task.cpp - TinyUSB host loop + HID gamepad helper + CDC write
// ============================================================================
// Affinity: Core 0
// Prioridade alta (timing critico do USB).
// ============================================================================

#include "usb_task.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "tusb.h"
#include "class/hid/hid_device.h"
#include "class/cdc/cdc_device.h"

#define USB_TASK_STACK          (configMINIMAL_STACK_SIZE * 4)
#define USB_TASK_CORE_AFFINITY  (1u << 0)  // Core 0

// ----------------------------------------------------------------------------
// Estado interno do gamepad
// ----------------------------------------------------------------------------
static SemaphoreHandle_t s_state_mutex = nullptr;
static volatile uint32_t s_buttons_mask = 0;
static volatile bool s_buttons_dirty = false;

// Pulses pendentes: bitmap de bits ainda em "press", e ticks de liberacao.
struct pulse_t {
    uint32_t release_tick;
};
static pulse_t s_pulses[32] = {};
static volatile uint32_t s_pulse_active_mask = 0;

// ----------------------------------------------------------------------------
// API publica
// ----------------------------------------------------------------------------
extern "C" void usb_hid_set_buttons(uint32_t buttons_mask)
{
    if (s_state_mutex == nullptr) return;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if ((s_buttons_mask & ~s_pulse_active_mask) != (buttons_mask & ~s_pulse_active_mask)) {
        s_buttons_mask = (s_buttons_mask & s_pulse_active_mask) |
                         (buttons_mask & ~s_pulse_active_mask);
        s_buttons_dirty = true;
    }
    xSemaphoreGive(s_state_mutex);
}

extern "C" void usb_hid_pulse_button(uint8_t bit_index, uint32_t press_ms)
{
    if (s_state_mutex == nullptr) return;
    if (bit_index >= 32) return;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint32_t bit = (1u << bit_index);
    s_pulses[bit_index].release_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(press_ms);
    s_pulse_active_mask |= bit;
    s_buttons_mask |= bit;
    s_buttons_dirty = true;
    xSemaphoreGive(s_state_mutex);
}

// Contadores de diagnostico do CDC TX.
static volatile uint32_t s_cdc_ok    = 0;
static volatile uint32_t s_cdc_drop  = 0;
static volatile uint32_t s_cdc_last_avail = 0;

extern "C" void usb_cdc_write(const void *data, uint32_t len)
{
    // NAO usamos tud_cdc_connected() de proposito: ele exige que o host
    // asserte DTR. O bridge.py (pyserial) asserta, mas o
    // edge-impulse-data-forwarder NAO - e ficaria esperando dados pra sempre.
    // Em vez disso so checamos se ha espaco no buffer de TX; se o host nao
    // estiver lendo, o buffer enche e a gente dropa (motion e disposable).
    uint32_t avail = tud_cdc_write_available();
    s_cdc_last_avail = avail;
    if (avail < len) {
        s_cdc_drop++;
        return;
    }
    uint32_t written = tud_cdc_write(data, len);
    tud_cdc_write_flush();
    if (written == len) s_cdc_ok++; else s_cdc_drop++;
}

extern "C" void usb_cdc_stats(uint32_t *ok, uint32_t *drop, uint32_t *last_avail)
{
    if (ok) *ok = s_cdc_ok;
    if (drop) *drop = s_cdc_drop;
    if (last_avail) *last_avail = s_cdc_last_avail;
}

// ----------------------------------------------------------------------------
// Envia o HID report atual se algo mudou.
// IMPORTANTE: o mutex e usado corretamente - se nao conseguir pegar agora,
// nao toca em nada e tenta no proximo tick (a dirty flag fica intacta).
// ----------------------------------------------------------------------------
static void hid_send_if_dirty(void)
{
    if (!tud_hid_ready()) return;

    // Espera ate 5 ticks pelo mutex. Se nao conseguir, deixa pro proximo loop
    // - flag dirty permanece, evento NAO e perdido.
    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    // Expira pulses cujo tempo de press venceu.
    if (s_pulse_active_mask) {
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < 32; i++) {
            uint32_t bit = (1u << i);
            if ((s_pulse_active_mask & bit) &&
                (int32_t)(now - s_pulses[i].release_tick) >= 0) {
                s_pulse_active_mask &= ~bit;
                s_buttons_mask &= ~bit;
                s_buttons_dirty = true;
            }
        }
    }

    bool dirty = s_buttons_dirty;
    uint32_t buttons = s_buttons_mask;
    if (dirty) s_buttons_dirty = false;

    xSemaphoreGive(s_state_mutex);

    if (!dirty) return;

    // tud_hid_gamepad_report(report_id, x, y, z, rz, rx, ry, hat, buttons)
    // Eixos zerados - motion vai via CDC, nao HID.
    bool sent = tud_hid_gamepad_report(0, 0, 0, 0, 0, 0, 0,
                                       GAMEPAD_HAT_CENTERED, buttons);

    // Se a chamada falhou (HID estava ocupado mesmo apos tud_hid_ready()),
    // re-marca dirty para tentar de novo no proximo loop. Sem isso o release
    // poderia ser perdido e o botao ficaria "travado" no Dolphin.
    if (!sent) {
        if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_buttons_dirty = true;
            xSemaphoreGive(s_state_mutex);
        }
    }
}

// ----------------------------------------------------------------------------
// Task principal
// ----------------------------------------------------------------------------
static void usb_task_fn(void *p)
{
    (void)p;
    tusb_init();

    while (true) {
        tud_task();
        hid_send_if_dirty();
        // Mesmo sem mudanca, ainda precisamos chamar tud_task frequentemente.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

extern "C" BaseType_t usb_task_start(UBaseType_t priority)
{
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == nullptr) return pdFAIL;

    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(usb_task_fn, "usb", USB_TASK_STACK,
                                nullptr, priority, &handle);
    if (rc != pdPASS) return rc;

    vTaskCoreAffinitySet(handle, USB_TASK_CORE_AFFINITY);
    return pdPASS;
}
