#pragma once

#include "report.h"

void byk916_bk3632_link_init(void);
void byk916_link_keymap_changed(void);
bool byk916_link_key_event(uint8_t row,uint8_t col,uint16_t keycode,bool pressed);
bool byk916_link_rgb_asleep(void);
void byk916_bk3632_link_tick(void);
void byk916_bk3632_link_task(void);
void byk916_bk3632_select_initial_mode(void);
bool byk916_bk3632_usb_selected(void);
bool byk916_bk3632_output_enabled(void);
bool byk916_bk3632_charge_standby(void);
bool byk916_bk3632_charging(void);
bool byk916_bk3632_key_allowed(uint16_t keycode);
bool byk916_bk3632_report_acked(void);
bool byk916_bk3632_slot_indicator(uint8_t row, uint8_t col, uint8_t *rgb);
bool byk916_bk3632_slot_indicator_active(void);
void byk916_bk3632_ble_key(uint8_t slot, bool pressed);
void byk916_bk3632_ble_clear_key(bool pressed);
void byk916_bk3632_usb_select(void);
void byk916_bk3632_diag_snapshot(__xdata uint8_t *out);
void byk916_bk3632_send_extra(__xdata const report_extra_t *report);
void byk916_bk3632_send_6kro(__xdata const report_keyboard_t *report);
#ifdef NKRO_ENABLE
void byk916_bk3632_send_nkro(__xdata const report_nkro_t *report);
#endif
