#include "byk916_bk3632_link.h"
#include "delay.h"
#include "gpio.h"
#include "host.h"
#include "kbdef.h"
#include "keyboard.h"
#include "layout.h"
#include "settings.h"
#include "usb.h"
#include "watchdog.h"
#include "indicators.h"
#include "report.h"
#include <stdint.h>
#include <string.h>

#ifdef DYNAMIC_MACRO_ENABLE
#    include "dynamic_macro.h"
#else
#    define dynamic_macro_abort() ((void)0)
#    define dynamic_macro_active() false
#    define static_macro_active() false
#endif

#ifdef DYNAMIC_KEYMAP_ENABLE
#    include "dynamic_keymap.h"
#else
#    define DYNAMIC_KEYMAP_LAYER_COUNT 2
#    define keymap_get_keycode(layer, row, col) (keymaps[(layer)][(row)][(col)])
#endif

#ifndef usb_host_active
#    define usb_host_active() (usb_is_configured() && !usb_suspended)
#endif
#ifndef usb_host_present
#    define usb_host_present() (usb_is_configured())
#endif
#ifndef usb_host_forget
#    define usb_host_forget() ((void)0)
#endif
#ifndef usb_hw_ep1_in_free
#    define usb_hw_ep1_in_free() (!(EP1CON & _IEP1RDY))
#endif
#ifndef NKRO_USAGE_MIN
#    define NKRO_USAGE_MIN 0x00
#endif
#ifndef NKRO_USAGE_MAX
#    define NKRO_USAGE_MAX 0x9f
#endif

extern void indicators_render_request(void);

#ifndef REPORT_TRANSPORT_RESET_DEFINED
static void report_transport_reset(void)
{
    extern report_keyboard_t keyboard_report;
    extern report_keyboard_t last_report;
    memset(&keyboard_report, 0, sizeof(keyboard_report));
    memset(&last_report, 0, sizeof(last_report));
#ifdef NKRO_ENABLE
    extern report_nkro_t nkro_report;
    extern report_nkro_t last_nkro_report;
    memset(&nkro_report, 0, sizeof(nkro_report));
    memset(&last_nkro_report, 0, sizeof(last_nkro_report));
#endif
}
#endif

/* BYK916 master -> BK3632 slave: mode 3, MSB first. */
#define LINK_SYNC0          0xd6u
#define LINK_SYNC1          0x39u
#define LINK_VERSION        0x01u
#define LINK_RAW_SIZE       12u
#define LINK_REPORT_SIZE    8u
#define LINK_NIBBLE_BASE    0x40u
#define LINK_FLAG_CONNECTED 0x01u
#define LINK_FLAG_CONTROL   0x40u
#define LINK_CMD_BLE_ON     0x01u
#define LINK_CMD_BLE_PAIR   0x02u
#define LINK_CMD_BLE_OFF    0x03u

/* BK3632 -> BYK916 status reply. All variable bytes remain in 0x40..0x6f,
 * keeping the custom wire protocol clear of the reserved A5/C3 values. */
#define REPLY_SYNC             0xb4u
#define REPLY_ACK_BASE         0x40u
#define REPLY_SWITCH_ON       0x10u
#define REPLY_HID_READY       0x20u
#define REPLY_STATUS_BASE      0x40u
#define REPLY_CHECK_BASE       0x60u
#define REPLY_LED_MASK         0x07u
#define REPLY_STATE_SHIFT      3u
#define REPLY_STATE_MASK       0x18u
#define REPLY_SLOT2            0x20u
#define REMOTE_BLE_OFF         0u
#define REMOTE_BLE_ADVERTISING 1u
#define REMOTE_BLE_CONNECTED   2u
#define REMOTE_BLE_PAIRING     3u

#define STATUS_REQ0 0xd7u
#define STATUS_REQ1 0x38u
#define STATUS_REQ2 0x4bu
#define STATUS_REQ3 0x46u

/* The scheduler tick is about 0.2 ms in this keyboard build. */
#define BLE_HOLD_TICKS           15000u /* about 3 seconds */
#define BLE_CLEAR_HOLD_TICKS     25000u /* about 5 seconds */
#define BLE_MACRO_EDGE_TICKS        60u /* about 12 ms */
#define BK_BOOT_TICKS             1500u /* about 300 ms */
#define STATUS_POLL_TICKS          800u /* about 160 ms, like the factory loop */
#define STATUS_STALE_TICKS        5000u /* about 1 second */
#define COMMAND_COOLDOWN_TICKS    1250u /* about 250 ms */
#define UNKNOWN_RETRY_TICKS       5000u /* do not command-storm if MISO is absent */

/* The first phase deliberately has only the three mutually exclusive modes.
 * USB and wireless keyboard reports are never active at the same time. */
typedef uint8_t link_mode_t;
enum {
    MODE_CHARGE_STANDBY = 0,
    MODE_USB,
    MODE_BLE1,
};

static volatile link_mode_t current_mode;
static uint8_t link_sequence;
static uint8_t last_sent_sequence;
static uint8_t acked_sequence;
static uint8_t media_requests,media_sent;
static uint16_t media_last_usage;
static uint8_t last_status_reply[4];
static uint8_t last_report[LINK_REPORT_SIZE];
#ifdef BYK_BLE_NKRO
static __xdata uint8_t last_bitmap[15];
/* V59 raw storage is owned by reliable sender. */
#endif
static __xdata report_keyboard_t usb_release_6kro;
#ifdef NKRO_ENABLE
static __xdata report_nkro_t usb_release_nkro;
#endif

static bool initialized;
static bool bk_bus_ready;
static bool battery_path_enabled;
static bool output_enabled;
static bool have_report;
static bool transition_release_pending;
static volatile bool pair_requested;
static bool pair_observed;
static bool status_valid;
static bool slot_switch_pending;
static bool remote_power_switch_on;
static bool remote_hid_ready;
static uint8_t selected_slot;
static uint8_t remote_slot;
static uint8_t pair_generation;
static uint8_t remote_ble_state;
static volatile uint8_t control_pending;
static volatile uint16_t boot_ticks;
static volatile bool boot_due;
static volatile uint16_t poll_ticks;
static volatile uint16_t status_age_ticks;
static volatile uint16_t command_cooldown_ticks;
static volatile uint16_t unknown_retry_ticks;
static volatile uint16_t ble_macro_edge_ticks;
static volatile uint16_t ble_hold_ticks;
static volatile bool poll_due;
static volatile bool ble_key_down;
static volatile bool ble_hold_sent;
static volatile bool ble_clear_down;
static volatile bool ble_clear_sent;
static volatile uint16_t ble_clear_ticks;
static volatile uint16_t status_good;
static volatile uint16_t status_bad;
static volatile uint16_t slot_ui_ticks;
static volatile uint16_t slot_feedback_ticks;
static volatile uint8_t clear_flash_count;
/* Software radio standby only: keep matrix scan, P33 polling and power path. */
static volatile bool radio_sleep,pm_dim,pm_release_wait;
static bool pm_waking;
static uint8_t physical_held;
static volatile uint16_t pm_divider;
static volatile uint8_t pm_seconds_due;
static uint16_t idle_seconds,offline_seconds,pair_seconds;
static bool indicator_map_dirty=true;
static uint8_t slot_row[4]={1,1,1,1},slot_col[4]={1,2,3,4};
static uint8_t slot_hold_row=0xff,slot_hold_col=0xff;
static uint8_t slot_hold_owner;
static uint8_t clear_hold_row=0xff,clear_hold_col=0xff;

void byk916_link_keymap_changed(void) { indicator_map_dirty=true; }

static void refresh_indicator_map(void)
{
    uint8_t found=0;
    slot_row[0]=slot_row[1]=slot_row[2]=slot_row[3]=1;
    slot_col[0]=1;slot_col[1]=2;slot_col[2]=3;slot_col[3]=4;
    for (uint8_t layer=0;layer<DYNAMIC_KEYMAP_LAYER_COUNT;layer++) {
        for (uint8_t row=0;row<MATRIX_ROWS;row++) {
            for (uint8_t col=0;col<MATRIX_COLS;col++) {
                uint16_t code=keymap_get_keycode(layer,row,col);
                uint8_t slot=(code==BLE1_CTL)?0u:((code==BLE2_CTL)?1u:((code==RF24_CTL)?3u:0xffu));
                if (slot!=0xffu && !(found&(1u<<slot))) {
                    slot_row[slot]=row;slot_col[slot]=col;found|=(1u<<slot);
                }
            }
        }
    }
    indicator_map_dirty=false;
    indicators_render_request();
}

bool byk916_link_rgb_asleep(void) { return current_mode==MODE_BLE1 && pm_dim; }

/* Matrix calls this for every physical edge (including Fn). Keycode is the
 * press-latched value, so releases remain correct after changing layers. */
bool byk916_link_key_event(uint8_t row,uint8_t col,uint16_t code,bool pressed)
{
    if (pressed) { if (physical_held<255) ++physical_held; }
    else if (physical_held) --physical_held;
    idle_seconds=offline_seconds=0;
    if (pm_dim) { pm_dim=false;indicators_render_request(); }
    if (pressed && current_mode==MODE_BLE1 && (radio_sleep || pm_release_wait)) {
        radio_sleep=false;pm_release_wait=false;pm_waking=true;
        output_enabled=false;remote_hid_ready=false;status_valid=false;
        pair_seconds=0;
        for (uint8_t i=0;i<8;i++) last_report[i]=0;
#ifdef BYK_BLE_NKRO
    for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
#endif
        report_transport_reset();
        __critical { control_pending=LINK_CMD_BLE_ON; }
        slot_switch_pending=true;
        return false; /* first press wakes only; matrix suppresses its release */
    }
    if (pm_waking && current_mode==MODE_BLE1) return false;
    if ((code==BLE1_CTL || code==BLE2_CTL || code==RF24_CTL || code==BLE_CLEAR) && current_mode!=MODE_BLE1)
        return false;
    if (code==BLE1_CTL || code==BLE2_CTL || code==RF24_CTL) {
        if (pressed) {
            if (slot_hold_owner) return false; /* one long-press owner */
            slot_hold_owner=(code==BLE1_CTL)?1u:((code==BLE2_CTL)?2u:4u);
            slot_hold_row=row;slot_hold_col=col;
            slot_row[slot_hold_owner-1]=row;slot_col[slot_hold_owner-1]=col;
            pair_seconds=0;
            byk916_bk3632_ble_key(slot_hold_owner,true);
        } else if (row==slot_hold_row && col==slot_hold_col) {
            byk916_bk3632_ble_key(slot_hold_owner,false);
            slot_hold_owner=0;slot_hold_row=slot_hold_col=0xff;
        }
        return false;
    }
    if (code==BLE_CLEAR) {
        if (pressed) {
            if (clear_hold_row!=0xff) return false;
            clear_hold_row=row;clear_hold_col=col;pair_seconds=0;
            byk916_bk3632_ble_clear_key(true);
        } else if (row==clear_hold_row && col==clear_hold_col) {
            byk916_bk3632_ble_clear_key(false);
            clear_hold_row=clear_hold_col=0xff;
        }
        return false;
    }
    return true;
}

static void bk_set_battery_path(bool enabled)
{
    if (enabled) GPIO_HIGH(7, DC_DC_ENABLE_P7_6);
    else GPIO_LOW(7, DC_DC_ENABLE_P7_6);
    battery_path_enabled = enabled;
}

static void bk_bus_release(void)
{
    GPIO_INPUT(0, (uint8_t)(BK_BT_MISO_P0_3 | BK_BT_MOSI_P0_4 | BK_BT_MOT_P0_2));
    GPIO_PULLUP_OFF(0, (uint8_t)(BK_BT_MISO_P0_3 | BK_BT_MOSI_P0_4 | BK_BT_MOT_P0_2));
    GPIO_INPUT(4, (uint8_t)(BK_BT_STATUS_P4_1 | BK_BT_CLK_P4_7));
    GPIO_PULLUP_OFF(4, (uint8_t)(BK_BT_STATUS_P4_1 | BK_BT_CLK_P4_7));
    GPIO_INPUT(7, BK_BT_CSN_P7_5);
    GPIO_PULLUP_OFF(7, BK_BT_CSN_P7_5);
    bk_bus_ready = false;
}

static void bk_bus_enable(void)
{
    GPIO_HIGH(7, BK_BT_CSN_P7_5);
    GPIO_HIGH(4, BK_BT_CLK_P4_7);
    GPIO_HIGH(0, (uint8_t)(BK_BT_MOSI_P0_4 | BK_BT_MOT_P0_2));
    GPIO_OUTPUT(7, BK_BT_CSN_P7_5);
    GPIO_OUTPUT(4, BK_BT_CLK_P4_7);
    GPIO_OUTPUT(0, (uint8_t)(BK_BT_MOSI_P0_4 | BK_BT_MOT_P0_2));
    GPIO_INPUT(0, BK_BT_MISO_P0_3);
    GPIO_PULLUP_ON(0, BK_BT_MISO_P0_3);
    GPIO_INPUT(4, BK_BT_STATUS_P4_1);
    GPIO_PULLUP_ON(4, BK_BT_STATUS_P4_1);
    bk_bus_ready = true;
}

static uint8_t link_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
#ifdef BYK_CRC8_NIBBLE
    /* poly07/init0,16-byte CODE table, no XRAM cache. All callers foreground.
     * Exactly two4bit updates replace eight per-bit branch iterations.
     * SPI port/timing code remains identical to V71. */
    static __code const uint8_t table[16]={0x00,0x07,0x0e,0x09,0x1c,0x1b,0x12,0x15,
                                         0x38,0x3f,0x36,0x31,0x24,0x23,0x2a,0x2d};
    while(len--) {
        crc^=*data++;
        crc=(uint8_t)((crc<<4)^table[crc>>4]);
        crc=(uint8_t)((crc<<4)^table[crc>>4]);
    }
#else
    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
 #endif
    return crc;
}

static uint8_t link_spi_byte(uint8_t value)
{
    uint8_t received = 0;
    /* Matrix/LED code also performs P4 read-modify-write operations. Protect one
     * byte at a time while allowing Timer2 to run between bytes. */
    __critical {
        for (uint8_t bit = 0; bit < 8; bit++) {
            received <<= 1;
            if (value & 0x80u) GPIO_HIGH(0, BK_BT_MOSI_P0_4);
            else GPIO_LOW(0, BK_BT_MOSI_P0_4);
            GPIO_LOW(4, BK_BT_CLK_P4_7);
            if (P0_3) received |= 1u;
            GPIO_HIGH(4, BK_BT_CLK_P4_7);
            value <<= 1;
        }
    }
    return received;
}

/* Dedicated reverse clocks: mode 3 changes data on falling edges and
 * samples on rising edges. Keep proven forward HID timing unchanged. */
static uint8_t link_status_byte(void)
{
    uint8_t received = 0;
    __critical {
        GPIO_HIGH(0, BK_BT_MOSI_P0_4);
#ifdef BYK_REVERSE_BYTE_TRIM
        /* Keep explicit low/high waits; shorten only bookkeeping. Generated
         * sampling location/high overhead still changes: hardware validation
         * required, do NOT describe this as waveform-identical. */
        for (uint8_t remaining=8u; remaining; --remaining)
#else
        for (uint8_t bit = 0; bit < 8u; bit++)
#endif
        {
            GPIO_LOW(4, BK_BT_CLK_P4_7);
            delay_us(2);
            GPIO_HIGH(4, BK_BT_CLK_P4_7);
            delay_us(2);
#ifdef BYK_REVERSE_BYTE_TRIM
            received <<= 1;
            if(P0_3)received |= 1u;
#else
            received = (uint8_t)((received << 1) | (P0_3 ? 1u : 0u));
#endif
        }
    }
    return received;
}

static void link_send_encoded(uint8_t value)
{
    (void)link_spi_byte((uint8_t)(LINK_NIBBLE_BASE | (value >> 4)));
    (void)link_spi_byte((uint8_t)(LINK_NIBBLE_BASE | (value & 0x0fu)));
}

#ifdef BYK_LINK_RELIABLE
/* Stop-and-wait reliable link transport. ACK means BK FIFO acceptance, not host ACK.
 * Six finite local slots; overflow deliberately loses edges, then sends
 * release + newest keyboard/media in a fresh session. No allocation/ISR I/O. */
#define REL_CAP 6u
static __xdata uint8_t rel_queue[REL_CAP][16];
static __xdata uint8_t rel_raw[21];
static uint8_t rel_head,rel_tail,rel_count,rel_seq,rel_wait,rel_live,rel_sync_sent;
static uint16_t rel_epoch;
static uint8_t rel_media[4];
static volatile uint16_t rel_ticks;
static uint8_t rel_failures;
static uint16_t rel_recoveries;
#ifdef BYK_ACK_CREDIT_REUSE
/* One local opportunity after ACK emptied our queue. TTL is active-time,
 * not wall-clock. Pauses explicitly invalidate it. No speculative dequeue. */
static volatile uint8_t rel_credit_ttl;
static uint8_t rel_credit_flags,rel_credit_slot;
void byk_link_credit_invalidate(void) {rel_credit_ttl=0;}
#endif
static void rel_new_session(void)
{
#ifdef BYK_ACK_CREDIT_REUSE
    rel_credit_ttl=0;
#endif
    ++rel_epoch;
    rel_live=rel_wait=rel_head=rel_tail=rel_count=rel_seq=rel_sync_sent=0;
    rel_failures=0;
    __critical { rel_ticks=0; }
}
static void rel_append(uint8_t kind,const uint8_t *data)
{
    uint8_t n=kind==1u?15u:4u;
    rel_queue[rel_head][0]=kind;
    for(uint8_t i=0;i<15u;i++)rel_queue[rel_head][1u+i]=i<n?data[i]:0;
    if(++rel_head==REL_CAP)rel_head=0;
    ++rel_count;
}
static void rel_snapshot(void)
{
    uint8_t zero[15]={0};
    rel_append(1,zero);rel_append(2,zero);
    rel_append(1,last_bitmap);rel_append(2,rel_media);
}
static void rel_enqueue(uint8_t kind,const uint8_t *data)
{
    if(kind==2u)for(uint8_t i=0;i<4;i++)rel_media[i]=data[i];
    if(!rel_live)return; /* disconnected: coalesce, never replay old edges */
    if(rel_count==REL_CAP){++rel_recoveries;rel_new_session();return;}
    rel_append(kind,data);
    __critical {rel_ticks=0;} /* first idle edge must not wait 160ms */
}
static void rel_wire(uint8_t kind,const uint8_t *data)
{
    rel_raw[0]=3;rel_raw[1]=(uint8_t)rel_epoch;rel_raw[2]=(uint8_t)(rel_epoch>>8);
    rel_raw[3]=rel_seq;rel_raw[4]=kind;
    for(uint8_t i=0;i<15;i++)rel_raw[5+i]=kind?data[i]:0;
    rel_raw[20]=link_crc8(rel_raw,20);
    GPIO_LOW(0,BK_BT_MOT_P0_2);GPIO_LOW(7,BK_BT_CSN_P7_5);
    (void)link_spi_byte(0xd6);(void)link_spi_byte(0x3b);
    for(uint8_t i=0;i<21;i++)link_send_encoded(rel_raw[i]);
    GPIO_HIGH(7,BK_BT_CSN_P7_5);GPIO_HIGH(0,BK_BT_MOT_P0_2);
    watchdog_kick();
}
#ifdef BYK_ACK_MACRO_SERVICE
/* Shared forward-only primitive. Does not query or dequeue. Caller owns
 * eligibility and consumes credit before invoking. */
static void rel_send_pending(void)
{
    rel_wire(rel_queue[rel_tail][0],&rel_queue[rel_tail][1]);rel_wait=1;
    __critical {rel_ticks=20u;}
}
static void rel_send_credit(void)
{
    rel_credit_ttl=0;
    rel_send_pending();
}
#endif
static bool rel_reply_pages(uint8_t *r,uint8_t first)
{
    uint8_t page[4];
    /* Keep the proven four-byte TX depth; no eight-byte FIFO assumption. */
    for(uint8_t p=0;p<2;p++) {
        GPIO_LOW(0,BK_BT_MOT_P0_2);GPIO_LOW(7,BK_BT_CSN_P7_5);
        (void)link_spi_byte(0xd7);(void)link_spi_byte(0x38);
        (void)link_spi_byte(0x4b);(void)link_spi_byte((uint8_t)(0x47+first+p));
        GPIO_HIGH(7,BK_BT_CSN_P7_5);delay_us(400);
        GPIO_LOW(7,BK_BT_CSN_P7_5);delay_us(2);
        for(uint8_t i=0;i<4;i++)page[i]=link_status_byte();
        GPIO_HIGH(7,BK_BT_CSN_P7_5);GPIO_HIGH(0,BK_BT_MOT_P0_2);
        watchdog_kick();
        if(page[0]!=(uint8_t)(0xb5+first+p) || page[3]!=link_crc8(page,3))return false;
        r[1+p*2]=page[1];r[2+p*2]=page[2];
    }
    return true;
}
static void rel_service(void)
{
    uint8_t r[8];uint16_t ticks;
    __critical {ticks=rel_ticks;}
    if(!initialized || current_mode!=MODE_BLE1 ||
       (!output_enabled && !transition_release_pending && !pm_release_wait) ||
       !bk_bus_ready || control_pending || slot_switch_pending || !remote_hid_ready ||
       remote_slot!=selected_slot) {
#ifdef BYK_ACK_CREDIT_REUSE
        rel_credit_ttl=0;
#endif
        return;
    }
    if(ticks)return;
#ifdef BYK_ACK_CREDIT_REUSE
    if(rel_count==1 && !rel_wait && rel_live && !rel_failures &&
       rel_credit_ttl && rel_credit_slot==selected_slot &&
       (rel_credit_flags&(rel_queue[rel_tail][0]==1u?8u:16u))) {
        /* Consume BEFORE wire. Receipt must still be ACKed normally; a
         * stale BK snapshot/full FIFO can reject without losing our frame. */
#ifdef BYK_ACK_MACRO_SERVICE
        rel_send_credit();return;
#else
        rel_credit_ttl=0;
        rel_wire(rel_queue[rel_tail][0],&rel_queue[rel_tail][1]);rel_wait=1;
        __critical {rel_ticks=20u;}
        return;
#endif
    }
    rel_credit_ttl=0; /* any actual query replaces/invalidates old credit */
#endif
    __critical {rel_ticks=(rel_count || !rel_live)?20u:800u;}
    if(!rel_reply_pages(r,0))
    {
        /* Missing ACK never means success. Rate-limit an unavailable MISO. */
        if(rel_failures<250u)++rel_failures;
        if(rel_failures>=8u){__critical {rel_ticks=800u;}return;}
        if(rel_live && rel_wait)rel_wire(rel_queue[rel_tail][0],&rel_queue[rel_tail][1]);
        return;
    }
    rel_failures=0;
    if(!(r[4]&1u)) {if(rel_live)rel_new_session();return;}
    if(!rel_live && !rel_sync_sent) {
        /* BYK-only reboot: choose a new session relative to live BK state,
         * rather than accidentally matching an old boot's epoch/sequence. */
        rel_epoch=(uint16_t)(((uint16_t)r[2]<<8)|r[1]);++rel_epoch;
        rel_seq=0;rel_sync_sent=1;rel_wire(0,rel_raw);return;
    }
    if(!(r[4]&2u) || r[1]!=(uint8_t)rel_epoch || r[2]!=(uint8_t)(rel_epoch>>8)) {
        if(rel_live){rel_new_session();return;}
        rel_wire(0,rel_raw);return;
    }
    if(!rel_live){rel_live=1;rel_seq=1;rel_snapshot();}
    if(rel_wait && (r[4]&4u) && r[3]==rel_seq) {
        rel_wait=0;if(++rel_tail==REL_CAP)rel_tail=0;--rel_count;++rel_seq;
#ifdef BYK_ACK_CREDIT_REUSE
        if(!rel_count) {
            rel_credit_flags=r[4]&24u;rel_credit_slot=selected_slot;
            rel_credit_ttl=100u; /* <=20ms active time, at most one frame */
        }
#endif
    }
    if(!rel_count)return;
    /* Credit is advisory; BK atomically refuses full FIFO without ACK. */
    if(!rel_wait && !(r[4]&(rel_queue[rel_tail][0]==1u?8u:16u))) {
        return;
    }
#ifdef BYK_ACK_MACRO_SERVICE
    rel_send_pending();
#else
    rel_wire(rel_queue[rel_tail][0],&rel_queue[rel_tail][1]);rel_wait=1;
    __critical {rel_ticks=20u;}
#endif
}

/* VIA08 01 06: read-only on demand; USB may have disconnected BLE already. */
void byk916_wireless_diag(__xdata uint8_t *out)
{
    uint8_t r[5];bool ok=false;
    for(uint8_t i=0;i<29;i++)out[i]=0;
    if(bk_bus_ready)ok=rel_reply_pages(r,2);
    out[0]=59;out[1]=8;out[2]=ok?1:0;
    if(ok)for(uint8_t i=0;i<4;i++)out[3+i]=r[1+i];
    out[7]=rel_live;out[8]=rel_count;out[9]=rel_wait;
    out[10]=(uint8_t)rel_epoch;out[11]=(uint8_t)(rel_epoch>>8);
    out[12]=rel_seq;out[13]=(uint8_t)rel_recoveries;out[14]=(uint8_t)(rel_recoveries>>8);
    out[15]=rel_failures;
}
#endif
static uint8_t reply_check(uint8_t ack, uint8_t status)
{
    return (uint8_t)(REPLY_CHECK_BASE |
                     ((REPLY_SYNC ^ ack ^ (ack >> 4) ^ status ^ (status >> 4)) & 0x0fu));
}

static bool link_apply_reply(const uint8_t *reply)
{
    uint8_t state;
    uint8_t old_state = remote_ble_state;
    uint8_t old_slot = remote_slot;
    bool was_hid_ready = remote_hid_ready;
    if (reply[0] != REPLY_SYNC ||
        (reply[1] & 0xc0u) != REPLY_ACK_BASE ||
        (reply[2] & 0xc0u) != REPLY_STATUS_BASE ||
        reply[3] != reply_check(reply[1], reply[2])) {
        status_bad++;
        return false;
    }

    state = (uint8_t)((reply[2] & REPLY_STATE_MASK) >> REPLY_STATE_SHIFT);

    acked_sequence = (uint8_t)(reply[1] & 0x0fu);
    remote_power_switch_on = (reply[1] & REPLY_SWITCH_ON) != 0;
    remote_hid_ready = (reply[1] & REPLY_HID_READY) != 0;
    remote_ble_state = state;
    if (reply[1] & 0x10u)
        remote_slot = 4u;
    else if (reply[2] & REPLY_SLOT2)
        remote_slot = 2u;
    else
        remote_slot = 1u;
    status_valid = true;
    __critical {
        status_age_ticks = 0;
        unknown_retry_ticks = 0;
    }
    status_good++;
    if (current_mode == MODE_BLE1) {
        keyboard_set_led_state((state == REMOTE_BLE_CONNECTED && remote_slot == selected_slot)
                               ? (uint8_t)(reply[2] & REPLY_LED_MASK) : 0u);
    }
#ifdef BYK_LINK_RELIABLE
    if(rel_live && (!remote_hid_ready || remote_slot!=selected_slot))rel_new_session();
#endif
    if (pair_requested && state == REMOTE_BLE_PAIRING) pair_observed = true;
    if (radio_sleep || pm_release_wait) return true; /* stale connected must not cancel sleep */
    if (pm_waking && state==REMOTE_BLE_CONNECTED && remote_slot==selected_slot && remote_hid_ready) {
        pm_waking=false;
        output_enabled=true;
    }
    if (state == REMOTE_BLE_CONNECTED && remote_slot == selected_slot && remote_hid_ready) {
        clear_flash_count = 0;
        if (!was_hid_ready || old_state != REMOTE_BLE_CONNECTED || old_slot != selected_slot)
            __critical { slot_feedback_ticks = 10000u; }
        slot_switch_pending = false;
        pair_requested = false;
        pair_observed = false;
    }
    return true;
}

/* Forward reports and controls are write-only transactions. The two trailing
 * bytes merely push the 26-byte encoded frame through BK3632's four-byte RX
 * threshold; no reply is consumed in this CS window. */
static void link_transaction(uint8_t sequence, uint8_t flags, const uint8_t *report)
{
    uint8_t raw[LINK_RAW_SIZE];
    if (!bk_bus_ready) return;

    raw[0] = LINK_VERSION;
    raw[1] = sequence;
    raw[2] = flags;
    for (uint8_t i = 0; i < LINK_REPORT_SIZE; i++) raw[3u + i] = report[i];
    raw[11] = link_crc8(raw, 11);

    GPIO_LOW(0, BK_BT_MOT_P0_2);
    GPIO_LOW(7, BK_BT_CSN_P7_5);
    (void)link_spi_byte(LINK_SYNC0);
    (void)link_spi_byte(LINK_SYNC1);
    for (uint8_t i = 0; i < LINK_RAW_SIZE; i++) link_send_encoded(raw[i]);
    (void)link_spi_byte(0xffu);
    (void)link_spi_byte(0xffu);
    GPIO_HIGH(7, BK_BT_CSN_P7_5);
    GPIO_HIGH(0, BK_BT_MOT_P0_2);
    watchdog_kick();
}

/* Factory-style reverse transaction:
 *   A: CS low, send request, CS high
 *   wait about 203 us
 *   B: CS low again, clock exactly four reply bytes, CS high.
 * A malformed/missing reply is diagnostic only and never gates mode ownership. */
static bool link_read_status(void)
{
    uint8_t reply[4];
    if (!bk_bus_ready) return false;

    GPIO_LOW(0, BK_BT_MOT_P0_2);
    GPIO_LOW(7, BK_BT_CSN_P7_5);
    (void)link_spi_byte(STATUS_REQ0);
    (void)link_spi_byte(STATUS_REQ1);
    (void)link_spi_byte(STATUS_REQ2);
    (void)link_spi_byte(STATUS_REQ3);
    GPIO_HIGH(7, BK_BT_CSN_P7_5);

    delay_us(400);

    GPIO_LOW(7, BK_BT_CSN_P7_5);
    delay_us(2);
    for (uint8_t i = 0; i < 4u; i++) {
        reply[i] = link_status_byte();
        last_status_reply[i] = reply[i];
    }
    GPIO_HIGH(7, BK_BT_CSN_P7_5);
    GPIO_HIGH(0, BK_BT_MOT_P0_2);
    watchdog_kick();
    return link_apply_reply(reply);
}

static void queue_control(uint8_t command)
{
    __critical { control_pending = command; }
}

static void send_control(uint8_t command)
{
    uint8_t control[LINK_REPORT_SIZE] = {0};
#ifdef BYK_LINK_RELIABLE
    rel_new_session();
    if(command==LINK_CMD_BLE_PAIR) {
        dynamic_macro_abort();report_transport_reset();
        for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
        for(uint8_t i=0;i<4;i++)rel_media[i]=0;
    }
    remote_hid_ready=false;
    slot_switch_pending=true;
#endif
    control[0] = command;
    control[1] = selected_slot;
    control[2] = pair_generation;
    link_transaction(last_sent_sequence,
                     (P4_1 ? LINK_FLAG_CONNECTED : 0u) | LINK_FLAG_CONTROL,
                     control);
    __critical {
        command_cooldown_ticks = COMMAND_COOLDOWN_TICKS;
        unknown_retry_ticks = UNKNOWN_RETRY_TICKS;
    }
}

static void link_send_report(const uint8_t *report)
{
    uint8_t sequence;
    uint8_t flags;

    for (uint8_t i = 0; i < LINK_REPORT_SIZE; i++) last_report[i] = report[i];
    have_report = true;
#ifdef BYK_BLE_NKRO
    for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
    last_bitmap[0]=report[0];
    for(uint8_t i=2;i<8;i++) {
        uint8_t k=report[i];
        if(k>=4 && k<=0x73){uint8_t n=k-4;last_bitmap[1+(n>>3)]|=(uint8_t)(1u<<(n&7));}
    }
#endif
    if (!initialized || current_mode != MODE_BLE1 || !output_enabled || !bk_bus_ready) return;

#ifdef BYK_LINK_RELIABLE
    rel_enqueue(1,last_bitmap);
    __critical {ble_macro_edge_ticks=BLE_MACRO_EDGE_TICKS;}
    return;
#endif
    sequence = link_sequence++;
    flags = P4_1 ? LINK_FLAG_CONNECTED : 0u;
    last_sent_sequence = sequence;
    link_transaction(sequence, flags, report);
    /* GATT local-write success is not a peer ACK. Preserve the proven 12 ms
     * BLE macro edge interval independently of the reverse status poll. */
    __critical { ble_macro_edge_ticks = BLE_MACRO_EDGE_TICKS; }
}

#ifdef BYK_BLE_NKRO
/* Foreground-only, paired with BK V58+. No false claim of capability from
 * the old four-byte status reply. Control/media remain legacy frames. */
static void link_send_bitmap(const __xdata uint8_t *report)
{
    for(uint8_t i=0;i<15;i++)last_bitmap[i]=report[i];
    have_report=true;
    if(!initialized || current_mode!=MODE_BLE1 || !output_enabled || !bk_bus_ready)return;
#ifdef BYK_LINK_RELIABLE
    rel_enqueue(1,last_bitmap);
#else
#error V59 BLE NKRO requires paired reliable transport
#endif
    __critical {ble_macro_edge_ticks=BLE_MACRO_EDGE_TICKS;}
}
#endif

static link_mode_t desired_mode(void)
{
    if (!P4_4) {
        usb_host_forget();
        return MODE_BLE1;
    }
    if (usb_host_present()) return MODE_USB;
    return remote_power_switch_on ? MODE_BLE1 : MODE_CHARGE_STANDBY;
}

static uint8_t desired_command(void)
{
    if (current_mode != MODE_BLE1 || radio_sleep) return LINK_CMD_BLE_OFF;
    return pair_requested ? LINK_CMD_BLE_PAIR : LINK_CMD_BLE_ON;
}

static bool remote_matches_mode(void)
{
    if (current_mode != MODE_BLE1 || radio_sleep) return remote_ble_state == REMOTE_BLE_OFF;
    if (remote_slot != selected_slot) return false;
    if (pair_requested) {
        return remote_ble_state == REMOTE_BLE_PAIRING ||
               remote_ble_state == REMOTE_BLE_CONNECTED;
    }
    return remote_ble_state == REMOTE_BLE_PAIRING ||
           remote_ble_state == REMOTE_BLE_ADVERTISING ||
           remote_ble_state == REMOTE_BLE_CONNECTED;
}

static void reconcile_remote_mode(void)
{
    uint16_t age;
    uint16_t cooldown;
    uint16_t unknown_retry;
    bool booting;

    __critical {
        age = status_age_ticks;
        cooldown = command_cooldown_ticks;
        unknown_retry = unknown_retry_ticks;
        booting = boot_ticks != 0 || boot_due;
    }
    if (booting || !bk_bus_ready || control_pending) return;

    if (status_valid && age < STATUS_STALE_TICKS) {
        if (!remote_matches_mode() && cooldown == 0) queue_control(desired_command());
    } else if (unknown_retry == 0) {
        /* Keep forward control independent from MISO validity: retry slowly,
         * but never use a missing reply as a transport-ownership gate. */
        queue_control(desired_command());
    }
}

static void enter_mode(link_mode_t mode)
{
    uint8_t zero[LINK_REPORT_SIZE] = {0};

    dynamic_macro_abort();
    report_transport_reset();
#ifdef BYK_LINK_RELIABLE
    rel_new_session();
    for(uint8_t i=0;i<4;i++)rel_media[i]=0;
#endif
    transition_release_pending = false;
    __critical {
        pair_requested = false;
        ble_key_down = false;
        ble_hold_sent = false;
        ble_hold_ticks = 0;
    }
    pair_observed = false;
    output_enabled = false;
    current_mode = mode;
    radio_sleep=pm_dim=pm_release_wait=pm_waking=false;
    idle_seconds=offline_seconds=pair_seconds=0;
    __critical { pm_seconds_due=0;pm_divider=0; }
    slot_hold_owner=0;slot_hold_row=slot_hold_col=0xff;
    clear_hold_row=clear_hold_col=0xff;
    __critical { ble_clear_down=false;ble_clear_sent=false;ble_clear_ticks=0; }
    indicators_render_request();
    keyboard_set_led_state(0);

    if (mode == MODE_USB) {
        /* USB exclusively owns reports and BLE remains closed-loop OFF. Keep
         * P7.6 high so a physically powered-on board does not power-cycle BK
         * when VBUS is unplugged; the switch disconnects BAT+ when it is OFF. */
        bk_set_battery_path(true);
        if (!bk_bus_ready) bk_bus_enable();
        __critical { boot_ticks = 0; }
        queue_control(LINK_CMD_BLE_OFF);
        output_enabled = usb_host_active();
        if (output_enabled) {
#ifdef NKRO_ENABLE
            if (host_nkro_active()) usb_send_nkro(&usb_release_nkro);
            else
#endif
            usb_send_report(&usb_release_6kro);
        }
    } else if (mode == MODE_CHARGE_STANDBY) {
        bk_set_battery_path(false);
        if (!bk_bus_ready) bk_bus_enable();
        __critical { boot_ticks = 0; }
        queue_control(LINK_CMD_BLE_OFF);
    } else {
        bool warm_handover = bk_bus_ready && (battery_path_enabled || P4_4);
        /* USB mode kept the switched battery path alive, so unplugging a board
         * whose physical switch is ON can restore BLE immediately. Battery-only
         * power-on and charge-standby transitions still use the cold-boot wait. */
        bk_set_battery_path(true);
        for (uint8_t i = 0; i < LINK_REPORT_SIZE; i++) last_report[i] = zero[i];
#ifdef BYK_BLE_NKRO
    for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
#endif
        have_report = true;
        if (warm_handover) {
            output_enabled = true;
            __critical { boot_ticks = 0; }
            queue_control(LINK_CMD_BLE_ON);
        } else {
            __critical { boot_ticks = BK_BOOT_TICKS; }
        }
    }

    status_valid = false;
    __critical {
        status_age_ticks = STATUS_STALE_TICKS;
        poll_ticks = STATUS_POLL_TICKS;
        poll_due = mode != MODE_BLE1 && bk_bus_ready;
        command_cooldown_ticks = 0;
        unknown_retry_ticks = UNKNOWN_RETRY_TICKS;
        boot_due = false;
    }
}

void byk916_bk3632_link_init(void)
{
    initialized = false;
    current_mode = 0xffu;
    selected_slot = (user_settings.rf_link >= 1u && user_settings.rf_link <= 4u) ? user_settings.rf_link : 1u;
    remote_slot = 1u;
    link_sequence = 0;
    last_sent_sequence = 0;
    acked_sequence = 0xffu;
    for (uint8_t i = 0; i < LINK_REPORT_SIZE; i++) last_report[i] = 0;
#ifdef BYK_BLE_NKRO
    for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
#endif
    have_report = true;
    output_enabled = false;
    transition_release_pending = false;
    pair_requested = false;
    pair_observed = false;
    status_valid = false;
    slot_switch_pending = true;
    remote_power_switch_on = false;
    remote_hid_ready = false;
    remote_ble_state = REMOTE_BLE_OFF;
    control_pending = 0;
    boot_ticks = 0;
    boot_due = false;
    poll_ticks = STATUS_POLL_TICKS;
    status_age_ticks = STATUS_STALE_TICKS;
    command_cooldown_ticks = 0;
    unknown_retry_ticks = 0;
    ble_macro_edge_ticks = 0;
    ble_hold_ticks = 0;
    poll_due = false;
    ble_key_down = false;
    ble_hold_sent = false;
    ble_clear_down = false;
    ble_clear_sent = false;
    ble_clear_ticks = 0;
    slot_ui_ticks = 0;
    slot_feedback_ticks = 0;
    clear_flash_count = 0;
    pair_generation = 0;
    status_good = 0;
    status_bad = 0;
    bk_bus_release();
    battery_path_enabled = false;
    bk_set_battery_path(false);
}

void byk916_bk3632_link_tick(void)
{
    if (!initialized) return;
    if (++pm_divider>=5000u) {
        pm_divider=0;
        if (pm_seconds_due<255u) ++pm_seconds_due;
    }
    if (poll_ticks && --poll_ticks == 0) poll_due = true;
    if (status_age_ticks != 0xffffu) status_age_ticks++;
    if (command_cooldown_ticks) command_cooldown_ticks--;
    if (unknown_retry_ticks) unknown_retry_ticks--;
    if (ble_macro_edge_ticks) ble_macro_edge_ticks--;
#ifdef BYK_ACK_CREDIT_REUSE
    if(rel_credit_ttl)--rel_credit_ttl;
#endif
#ifdef BYK_MACRO_TRACE
    if(mt_state==2 && mt_clock<25000u)++mt_clock;
#endif
#ifdef BYK_LINK_RELIABLE
    if(rel_ticks) --rel_ticks;
#endif
    slot_ui_ticks++;
    if (slot_feedback_ticks) slot_feedback_ticks--;

    if (boot_ticks && --boot_ticks == 0) boot_due = true;

    if (ble_key_down && !ble_hold_sent && current_mode == MODE_BLE1) {
        if (ble_hold_ticks < BLE_HOLD_TICKS && ++ble_hold_ticks >= BLE_HOLD_TICKS) {
            ble_hold_sent = true;
            pair_generation++;
            slot_ui_ticks = 0;
            slot_feedback_ticks = 0;
            pair_requested = true;
            queue_control(LINK_CMD_BLE_PAIR);
        }
    }
    if (ble_clear_down && !ble_clear_sent && current_mode == MODE_BLE1) {
        if (ble_clear_ticks < BLE_CLEAR_HOLD_TICKS && ++ble_clear_ticks >= BLE_CLEAR_HOLD_TICKS) {
            ble_clear_sent = true;
            clear_flash_count = 6u;
            slot_ui_ticks = 0;
        }
    }
    if (clear_flash_count && slot_ui_ticks >= 6000u) {
        clear_flash_count = 0;
        pair_generation++;
        pair_requested = true;
        queue_control(LINK_CMD_BLE_PAIR);
    }
}

#ifdef BYK_INPUT_BATCH_TICKS
void byk916_bk3632_link_tick_elapsed(uint8_t units)
{
    if (!initialized || !units) return;
#ifdef BYK_MACRO_TRACE
    if(mt_state==2)mt_clock=(mt_clock>25000u-units)?25000u:(mt_clock+units);
#endif
#ifdef BYK_ACK_CREDIT_REUSE
    rel_credit_ttl=rel_credit_ttl>units?(rel_credit_ttl-units):0;
#endif
    pm_divider+=units;
    if (pm_divider>=5000u) {
        pm_divider-=5000u;
        if (pm_seconds_due<255u) ++pm_seconds_due;
    }
    if (poll_ticks && poll_ticks<=units) poll_due=true;
    poll_ticks=(poll_ticks>units)?(poll_ticks-units):0;
    if (status_age_ticks>(uint16_t)(65535u-units)) status_age_ticks=65535u;
    else status_age_ticks+=units;
    command_cooldown_ticks=(command_cooldown_ticks>units)?(command_cooldown_ticks-units):0;
    unknown_retry_ticks=(unknown_retry_ticks>units)?(unknown_retry_ticks-units):0;
    ble_macro_edge_ticks=(ble_macro_edge_ticks>units)?(ble_macro_edge_ticks-units):0;
#ifdef BYK_LINK_RELIABLE
    rel_ticks=(rel_ticks>units)?(rel_ticks-units):0;
#endif
    /* Feedback can be cleared by an edge below; it is never armed there. */
    slot_feedback_ticks=(slot_feedback_ticks>units)?(slot_feedback_ticks-units):0;
    if (boot_ticks && boot_ticks<=units) boot_due=true;
    boot_ticks=(boot_ticks>units)?(boot_ticks-units):0;
    while (units--) {
    slot_ui_ticks++;


    if (ble_key_down && !ble_hold_sent && current_mode == MODE_BLE1) {
        if (ble_hold_ticks < BLE_HOLD_TICKS && ++ble_hold_ticks >= BLE_HOLD_TICKS) {
            ble_hold_sent = true;
            pair_generation++;
            slot_ui_ticks = 0;
            slot_feedback_ticks = 0;
            pair_requested = true;
            queue_control(LINK_CMD_BLE_PAIR);
        }
    }
    if (ble_clear_down && !ble_clear_sent && current_mode == MODE_BLE1) {
        if (ble_clear_ticks < BLE_CLEAR_HOLD_TICKS && ++ble_clear_ticks >= BLE_CLEAR_HOLD_TICKS) {
            ble_clear_sent = true;
            clear_flash_count = 6u;
            slot_ui_ticks = 0;
        }
    }
    if (clear_flash_count && slot_ui_ticks >= 6000u) {
        clear_flash_count = 0;
        pair_generation++;
        pair_requested = true;
        queue_control(LINK_CMD_BLE_PAIR);
    }
    }
}
#endif

#ifdef BYK_POWER_TIMING_GUARD
/* Foreground-only. Scan and auto-standby must agree on outstanding input.
 * Do not call BLE/SPI or macro state functions from the sampling ISR. */
static bool link_input_busy(void)
{
    extern bool static_macro_active(void);
    return physical_held || dynamic_macro_active() || static_macro_active() ||
           rel_count || rel_wait || transition_release_pending;
}
#endif
static void link_power_task(void)
{
    uint8_t seconds;
    uint16_t age,edge;
    __critical { seconds=pm_seconds_due;pm_seconds_due=0;age=status_age_ticks;edge=ble_macro_edge_ticks; }
    if (current_mode!=MODE_BLE1) return;
    if (pm_release_wait) {
#ifdef BYK_LINK_RELIABLE
        rel_service();
#endif
        if (!edge) {
            pm_release_wait=false;radio_sleep=true;pm_waking=false;pm_dim=true;
            pair_requested=false;pair_observed=false;clear_flash_count=0;
            slot_switch_pending=false;slot_feedback_ticks=0;
            keyboard_set_led_state(0);
            queue_control(LINK_CMD_BLE_OFF);
            indicators_render_request();
        }
        return;
    }
    if (radio_sleep) return;
    if (
#ifdef BYK_POWER_TIMING_GUARD
        link_input_busy()
#else
        physical_held || dynamic_macro_active()
#endif
    ) {
        idle_seconds=offline_seconds=pair_seconds=0;
        return;
    }
    while (seconds--) {
        if (idle_seconds<900u) ++idle_seconds;
        if (status_valid && age<STATUS_STALE_TICKS && remote_hid_ready && remote_slot==selected_slot)
            offline_seconds=0;
        else if (offline_seconds<300u) ++offline_seconds;
        if (pair_requested || remote_ble_state==REMOTE_BLE_PAIRING) {
            if (pair_seconds<180u) ++pair_seconds;
        } else pair_seconds=0;
    }
    if (idle_seconds>=120u && !pm_dim && !pair_requested && remote_ble_state!=REMOTE_BLE_PAIRING) {
        pm_dim=true;indicators_render_request();
    }
    if (idle_seconds>=900u || offline_seconds>=300u || pair_seconds>=180u) {
        uint8_t zero[8]={0};
        /* No physical key/macro held. Release both report types, allow the
         * existing BLE guard interval before disconnect. Not a peer ACK. */
        link_send_report(zero);
#ifdef BYK_LINK_RELIABLE
        rel_enqueue(2,zero);
#else
        link_transaction(link_sequence++,0x20u,zero);
#endif
        __critical { ble_macro_edge_ticks=BLE_MACRO_EDGE_TICKS; }
        output_enabled=false;pm_release_wait=true;
    }
}

#ifdef BYK_DYNAMIC_SCAN
void byk916_scan_power_task(void)
{
    extern void byk_scan_force_active(uint8_t force);
#ifndef BYK_POWER_TIMING_GUARD
    extern bool static_macro_active(void);
#endif
#ifdef BYK_POWER_TIMING_GUARD
    uint8_t force=current_mode==MODE_USB || link_input_busy() || pm_release_wait ||
        pm_waking || control_pending || pair_requested;
#else
    uint8_t force=current_mode==MODE_USB || physical_held || dynamic_macro_active() ||
        static_macro_active() || transition_release_pending || pm_release_wait ||
        pm_waking || control_pending || pair_requested ||
        rel_count || rel_wait;
#endif
    byk_scan_force_active(force);
}
#endif
void byk916_bk3632_link_task(void)
{
    link_mode_t wanted;
    uint8_t command;
    bool due;
    bool start_radio;
    bool booting;

    if (!initialized) return;
#ifdef BYK_MACRO_TRACE
    {uint16_t elapsed;__critical {elapsed=mt_clock;}
#ifdef BYK_PHASE_TRACE
     if(mp_time_invalid)mt_freeze(3);
#endif
     if(mt_state==2 && elapsed>=25000u)mt_freeze(1);}
#endif
    if (indicator_map_dirty) refresh_indicator_map();
    wanted = desired_mode();

    if (wanted != current_mode) {
        /* Before leaving BLE, enqueue a complete release and preserve the
         * proven edge interval before OFF/USB takeover. The wait latch must
         * outlive output_enabled, which is cleared immediately after submit. */
        if (current_mode == MODE_BLE1 && bk_bus_ready) {
            uint16_t edge_ticks;
            if (!transition_release_pending && output_enabled) {
                uint8_t zero[LINK_REPORT_SIZE] = {0};
                link_send_report(zero);
#ifdef BYK_LINK_RELIABLE
                rel_enqueue(2,zero);
#endif
                transition_release_pending = true;
                output_enabled = false;
                dynamic_macro_abort();
            }
#ifdef BYK_LINK_RELIABLE
            rel_service();
#endif
            __critical { edge_ticks = ble_macro_edge_ticks; }
            if (transition_release_pending && edge_ticks) return;
        }
        enter_mode(wanted);
    }

    __critical {
        start_radio = boot_due;
        boot_due = false;
    }
    if (start_radio && current_mode == MODE_BLE1) {
        if (!bk_bus_ready) bk_bus_enable();
        output_enabled = true;
        queue_control(desired_command());
        __critical { poll_due = true; }
    }

    link_power_task();

    if (current_mode == MODE_USB) {
        bool active = usb_host_active();
        if (active && !output_enabled) {
            report_transport_reset();
            output_enabled = true;
#ifdef NKRO_ENABLE
            if (host_nkro_active()) usb_send_nkro(&usb_release_nkro);
            else
#endif
            usb_send_report(&usb_release_6kro);
        } else if (!active) {
            output_enabled = false;
        }
    }

    __critical {
        command = control_pending;
        control_pending = 0;
        due = poll_due;
        booting = boot_ticks != 0;
        if (due) {
            poll_due = false;
            poll_ticks = STATUS_POLL_TICKS;
        }
    }

    if (command && bk_bus_ready && !booting) {
        send_control(command);
        if (current_mode == MODE_BLE1 && !pm_waking && command != LINK_CMD_BLE_OFF && have_report) {
#ifdef BYK_BLE_NKRO
            link_send_bitmap(last_bitmap);
#else
            link_send_report(last_report);
#endif
        }
    }

    if (due && bk_bus_ready && !booting) (void)link_read_status();
    reconcile_remote_mode();
#ifdef BYK_ACK_MACRO_SERVICE
    {
        extern bool byk916_ble_macro_key_ready(void);
        uint8_t before=rel_seq;
        bool awaiting=rel_wait;
        rel_service();
        /* One matched ACK emptied the queue and supplied single-use credit.
         * This is NOT a second link_task or a drain loop. Recheck USB/power
         * mode after the potentially lengthy reply, before advancing macro. */
        if(awaiting && before!=rel_seq && !command && !booting &&
           rel_credit_ttl && (rel_credit_flags&8u) &&
           rel_credit_slot==selected_slot && byk916_ble_macro_key_ready() &&
           desired_mode()==current_mode) {
            extern void dynamic_macro_after_ack(void);
            dynamic_macro_after_ack();
            /* No new report (duplicate owner/delay/end) -> no idle polling.
             * TTL expired while composing -> leave normal task to query;
             * never add an unbounded reverse transaction on this path. */
            if(rel_count==1 && !rel_wait && rel_credit_ttl &&
               (rel_credit_flags&8u) && rel_queue[rel_tail][0]==1u &&
               !control_pending && !slot_switch_pending &&
               desired_mode()==current_mode) {
                rel_send_credit(); /* forward only, no query fallback */
            }
        }
    }
#elif defined(BYK_LINK_RELIABLE)
    rel_service();
#endif
}

void byk916_bk3632_select_initial_mode(void)
{
    report_transport_reset();
    selected_slot = (user_settings.rf_link >= 1u && user_settings.rf_link <= 4u) ? user_settings.rf_link : 1u;
    slot_switch_pending = true;
    slot_feedback_ticks = 0;
    enter_mode(desired_mode());
    initialized = true;
}

bool byk916_bk3632_usb_selected(void)
{
    return initialized && current_mode == MODE_USB;
}

bool byk916_bk3632_output_enabled(void)
{
    return initialized && output_enabled;
}

bool byk916_bk3632_charge_standby(void)
{
    return !initialized || current_mode == MODE_CHARGE_STANDBY;
}

bool byk916_bk3632_charging(void)
{
    return P4_4 && !P7_7;
}

bool byk916_bk3632_key_allowed(uint16_t keycode)
{
    (void)keycode;
    return initialized && current_mode != MODE_CHARGE_STANDBY;
}

void byk916_bk3632_usb_select(void)
{
    /* Phase one is automatic and strictly exclusive. Fn+U cannot override
     * VBUS/host classification. */
}

void byk916_bk3632_ble_key(uint8_t slot, bool pressed)
{
    if (!initialized || current_mode != MODE_BLE1 || slot < 1u || slot > 4u) return;

    if (pressed) {
        dynamic_macro_abort();
        report_transport_reset();
#ifdef BYK_LINK_RELIABLE
        rel_new_session();
        for(uint8_t i=0;i<15;i++)last_bitmap[i]=0;
        for(uint8_t i=0;i<4;i++)rel_media[i]=0;
#endif
        selected_slot = slot;
        keyboard_set_led_state(0);
        slot_switch_pending = true;
        slot_ui_ticks = 0;
        slot_feedback_ticks = 15000u;
        clear_flash_count = 0;
        if (user_settings.rf_link != slot) {
            user_settings.rf_link = slot;
            settings_save();
        }
        __critical {
            ble_key_down = true;
            ble_hold_ticks = 0;
            ble_hold_sent = false;
            pair_requested = false;
            pair_observed = false;
            control_pending = LINK_CMD_BLE_ON;
        }
    } else {
        __critical {
            ble_key_down = false;
            ble_hold_ticks = 0;
        }
    }
}

void byk916_bk3632_ble_clear_key(bool pressed)
{
    if (!initialized || current_mode != MODE_BLE1) return;
    __critical {
        ble_clear_down = pressed;
        ble_clear_ticks = 0;
        if (pressed) ble_clear_sent = false;
    }
}

#ifdef BYK_BLE_MACRO_ACCEPT_PACE
/* Foreground only: plain keyboard macro may advance once previous input
 * is accepted into BK's reliable FIFO. Not GATT completion or host ACK.
 * NO new queue slots; no new edge while any local frame is queued/inflight. */
bool byk916_ble_macro_key_ready(void)
{
    return initialized && current_mode==MODE_BLE1 && output_enabled &&
        bk_bus_ready && remote_hid_ready && remote_slot==selected_slot &&
        !control_pending && !slot_switch_pending && !pair_requested &&
        !radio_sleep && !pm_waking && !pm_release_wait && !transition_release_pending &&
        rel_live && !rel_count && !rel_wait && !rel_failures &&
        !(rel_media[0]|rel_media[1]|rel_media[2]|rel_media[3]);
}
#endif
#ifndef BYK_USB_MACRO_PIPELINE
static bool usb_media_idle(void) { return true; }
#endif

bool byk916_bk3632_report_acked(void)
{
    if (!initialized || !output_enabled) return false;
    if (current_mode == MODE_USB) {
        return usb_host_active() && usb_hw_ep1_in_free() && usb_media_idle()
#ifdef BYK_INPUT_RELIABLE
            && usb_keyboard_queue_idle()
#endif
            ;
    }
    if (current_mode == MODE_BLE1) {
        uint16_t edge_ticks;
        __critical { edge_ticks = ble_macro_edge_ticks; }
        return edge_ticks == 0
#ifdef BYK_LINK_RELIABLE
            && rel_live && !rel_count && !rel_wait
#endif
            ;
    }
    return false;
}

bool byk916_bk3632_slot_indicator_active(void)
{
    return initialized && current_mode == MODE_BLE1 && !radio_sleep && !pm_release_wait &&
           (clear_flash_count || pair_requested || slot_switch_pending ||
            (remote_slot == selected_slot &&
             (remote_ble_state == REMOTE_BLE_PAIRING ||
              remote_ble_state == REMOTE_BLE_ADVERTISING ||
              (remote_ble_state == REMOTE_BLE_CONNECTED && !remote_hid_ready) ||
              slot_feedback_ticks)));
}

bool byk916_bk3632_slot_indicator(uint8_t row, uint8_t col, uint8_t *rgb)
{
    uint16_t ticks;
    bool on = true;
    if (!byk916_bk3632_slot_indicator_active() || row != slot_row[selected_slot-1u] || col != slot_col[selected_slot-1u]) return false;
    __critical { ticks = slot_ui_ticks; }
    rgb[0] = rgb[1] = rgb[2] = 0;
    uint8_t ch = (selected_slot == 4u) ? 1u : 2u;
    if (clear_flash_count) {
        on = ((ticks / 1000u) & 1u) == 0;
        if (on) rgb[0] = 255u;
    } else if (pair_requested) {
        on = ((ticks / 750u) & 1u) == 0;
        if (on) { rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u; }
    } else if (remote_ble_state == REMOTE_BLE_CONNECTED && remote_slot == selected_slot) {
        rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u;
    } else if (remote_ble_state == REMOTE_BLE_PAIRING && remote_slot == selected_slot) {
        on = ((ticks / 750u) & 1u) == 0;
        if (on) { rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u; }
    } else if (slot_switch_pending && remote_slot != selected_slot) {
        on = ((ticks / 2500u) & 1u) == 0;
        if (on) { rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u; }
    } else if (remote_ble_state == REMOTE_BLE_ADVERTISING) {
        on = ((ticks / 2500u) & 1u) == 0;
        if (on) { rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u; }
    } else {
        rgb[ch] = 255u; if (selected_slot == 4u) rgb[2] = 255u;
    }
    return true;
}

void byk916_bk3632_diag_snapshot(__xdata uint8_t *out)
{
    uint16_t good;
    uint16_t bad;
    __critical {
        good = status_good;
        bad = status_bad;
    }
    out[0] = (uint8_t)current_mode;
    out[1] = remote_ble_state;
    out[2] = status_valid ? 1u : 0u;
    out[3] = acked_sequence;
    out[4] = (uint8_t)good;
    out[5] = (uint8_t)(good >> 8);
    out[6] = (uint8_t)bad;
    out[7] = (uint8_t)(bad >> 8);
    out[8] = selected_slot;
    out[9] = remote_slot;
    out[10] = remote_power_switch_on ? 1u : 0u;
    out[11] = byk916_bk3632_charging() ? 1u : 0u;
    out[12] = remote_hid_ready ? 1u : 0u;
    for (uint8_t i = 0; i < 4u; i++) out[13u + i] = last_status_reply[i];
    out[17] = pair_requested ? 1u : 0u;
    out[18] = output_enabled ? 1u : 0u;
    out[19] = media_requests;
    out[20] = media_sent;
    out[21] = (uint8_t)media_last_usage;
    out[22] = (uint8_t)(media_last_usage>>8);
    out[23] = radio_sleep?2u:(pm_waking?3u:(pm_dim?1u:0u));
    out[24] = slot_row[selected_slot-1u];
    out[25] = slot_col[selected_slot-1u];
    out[26] = physical_held;
    out[27] = (uint8_t)idle_seconds;
    out[28] = (uint8_t)(idle_seconds>>8);
}

void byk916_bk3632_send_extra(__xdata const report_extra_t *report)
{
    uint8_t payload[8] = {0};
    uint8_t bit;
    if (report->report_id != REPORT_ID_CONSUMER) return;
    ++media_requests;
    if (report->usage) media_last_usage=report->usage;
    if (!initialized || current_mode != MODE_BLE1 || !output_enabled ||
        !bk_bus_ready) return;
#ifndef BYK_LINK_RELIABLE
    if(!remote_hid_ready || remote_slot!=selected_slot)return;
#endif
    switch (report->usage) {
        case 0x224u: bit=0u; break;
        case 0x225u: bit=1u; break;
        case 0x226u: bit=2u; break;
        case 0x227u: bit=3u; break;
        case 0x221u: bit=4u; break;
        case 0x22au: bit=5u; break;
        case 0x223u: bit=6u; break;
        case 0x18au: bit=7u; break;
        case 0x0e2u: bit=8u; break;
        case 0x0eau: bit=9u; break;
        case 0x0e9u: bit=10u; break;
        case 0x0cdu: bit=11u; break;
        case 0x0b7u: bit=12u; break;
        case 0x0b6u: bit=13u; break;
        case 0x0b5u: bit=14u; break;
        case 0x183u: bit=15u; break;
        case 0x194u: bit=16u; break;
        case 0x192u: bit=17u; break;
        case 0x209u: bit=18u; break;
        case 0x0b2u: bit=19u; break;
        case 0x0b3u: bit=20u; break;
        case 0x0b4u: bit=21u; break;
        case 0x08du: bit=22u; break;
        case 0x004u: bit=23u; break;
        case 0x030u: bit=24u; break;
        case 0x307u: bit=25u; break;
        case 0x30au: bit=26u; break;
        case 0x30bu: bit=27u; break;
        case 0x1b1u: bit=28u; break;
        case 0x0b8u: bit=29u; break;
        case 0: bit=0xffu; break;
        default: return; /* Not declared by the unchanged BLE Report Map. */
    }
    if (bit != 0xffu) payload[bit >> 3] = (uint8_t)(1u << (bit & 7u));
#ifdef BYK_LINK_RELIABLE
    rel_enqueue(2,payload);
#else
    link_transaction(link_sequence++, 0x20u, payload);
#endif
    ++media_sent;
    __critical { ble_macro_edge_ticks=BLE_MACRO_EDGE_TICKS; }
}

void byk916_bk3632_send_6kro(__xdata const report_keyboard_t *report)
{
    link_send_report(report->raw);
}

#ifdef NKRO_ENABLE
void byk916_bk3632_send_nkro(__xdata const report_nkro_t *report)
{
#ifdef BYK_BLE_NKRO
    link_send_bitmap(report->raw);
#else
    uint8_t boot_report[LINK_REPORT_SIZE] = {0};
    uint8_t count = 0;
    boot_report[0] = report->mods;
    for (uint8_t usage = NKRO_USAGE_MIN;
         usage < (uint8_t)(NKRO_USAGE_MIN + NKRO_REPORT_BITS * 8u);
         usage++) {
        uint8_t index = (uint8_t)(usage - NKRO_USAGE_MIN);
        if (report->bits[index >> 3] & (uint8_t)(1u << (index & 7))) {
            if (count < 6u) boot_report[2u + count] = usage;
            count++;
        }
    }
    if (count > 6u) {
        for (uint8_t i = 2; i < LINK_REPORT_SIZE; i++) boot_report[i] = 0x01u;
    }
    link_send_report(boot_report);
#endif
}
#endif

#ifdef BYK_INPUT_RELIABLE
/* Physical accounting only after foreground discards an old transport's
 * queued matrix frames. Does not wake/pair/send or replay held keys. */
void byk916_link_sync_held(uint8_t count) { physical_held=count; }
#endif
