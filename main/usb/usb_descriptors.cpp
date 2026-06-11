// ============================================================================
// usb_descriptors.cpp - Composite USB: HID Gamepad (IF 0) + CDC Serial (IF 1/2)
// ============================================================================
// VID/PID: 0xCafe:0x4004 (faixa de exemplos do TinyUSB)
// HID:     Gamepad standard (32 botoes + eixos), report ID = 0 (sem ID)
// CDC:     Serial pra motion data (consumido pelo bridge.py no PC)
// ============================================================================

#include "tusb.h"
#include "pico/unique_id.h"

// ----------------------------------------------------------------------------
// Device descriptor
// ----------------------------------------------------------------------------
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use IAD (Interface Association Descriptor) por causa do composite
    // device. Microsoft Windows precisa disso pra reconhecer corretamente.
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,
    .idProduct          = 0x4004,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01,
};

extern "C" uint8_t const *tud_descriptor_device_cb(void)
{
    return reinterpret_cast<uint8_t const *>(&desc_device);
}

// ----------------------------------------------------------------------------
// HID Report Descriptor - Gamepad padrao do TinyUSB (32 botoes + eixos)
// ----------------------------------------------------------------------------
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

// ----------------------------------------------------------------------------
// Configuration descriptor
// ----------------------------------------------------------------------------
enum {
    ITF_NUM_HID = 0,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

// Endpoint numbers (bit 7 = direcao: 0 = OUT, 1 = IN)
#define EPNUM_HID         0x81  // HID IN (interrupt)
#define EPNUM_CDC_NOTIF   0x82  // CDC notify (interrupt)
#define EPNUM_CDC_OUT     0x03  // CDC OUT (bulk, host -> device)
#define EPNUM_CDC_IN      0x83  // CDC IN (bulk, device -> host)

uint8_t const desc_configuration[] = {
    // Config: 1 config, total length, attributes, max power 100 mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // HID: interface number, string index, protocol, report desc len,
    //      EP IN address, EP size, polling interval (ms)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4 /*string idx*/, HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 5),

    // CDC: notif interface, string index, EP notif, EP notif size,
    //      EP OUT, EP IN, EP data size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5 /*string idx*/, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
};

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

// ----------------------------------------------------------------------------
// String descriptors
// ----------------------------------------------------------------------------
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_HID_IFACE,
    STRID_CDC_IFACE,
};

static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },          // 0: supported language = English (0x0409)
    "Pico Bowling",                         // 1: Manufacturer
    "Pico 2 Wii Bowling Controller",        // 2: Product
    nullptr,                                // 3: Serial (preenchido dinamicamente do unique_id)
    "Bowling Gamepad",                      // 4: HID interface name
    "Bowling Motion CDC",                   // 5: CDC interface name
};

static uint16_t _desc_str[32];
static char serial_str[2 * 8 + 1] = { 0 };

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count = 0;

    if (index == STRID_LANGID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else if (index == STRID_SERIAL) {
        // Gera serial a partir do unique_id da flash do Pico (8 bytes -> 16 chars hex).
        if (serial_str[0] == 0) {
            pico_unique_board_id_t uid;
            pico_get_unique_board_id(&uid);
            for (int i = 0; i < 8; i++) {
                static const char hex[] = "0123456789ABCDEF";
                serial_str[i * 2 + 0] = hex[(uid.id[i] >> 4) & 0xF];
                serial_str[i * 2 + 1] = hex[uid.id[i] & 0xF];
            }
            serial_str[16] = 0;
        }
        chr_count = (uint8_t)strlen(serial_str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = serial_str[i];
        }
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return nullptr;
        const char *str = string_desc_arr[index];
        if (str == nullptr) return nullptr;
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // Header: bLength (bytes) | bDescriptorType (0x03 = STRING)
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// ----------------------------------------------------------------------------
// HID callbacks: nao usamos GET_REPORT/SET_REPORT, stubs vazios.
// ----------------------------------------------------------------------------
extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                         hid_report_type_t report_type,
                                         uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                     hid_report_type_t report_type,
                                     uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}
