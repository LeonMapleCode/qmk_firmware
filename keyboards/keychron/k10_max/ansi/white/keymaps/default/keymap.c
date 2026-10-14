/* Copyright 2024 @ Keychron (https://keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://gnu.org>.
 */

#include QMK_KEYBOARD_H
#include "keychron_common.h"
#include <stdlib.h> // 引入标准库以支持 rand()

enum layers {
    MAC_BASE,
    MAC_FN,
    WIN_BASE,
    WIN_FN,
};

// ==================== 【安全重构】非阻塞宏内部变频延时系统 ====================

enum custom_keycodes {
    MC_FUNC = SAFE_RANGE, // 智能宏循环控制与打断键
};

bool     macro_loop_active = false; // 循环状态总开关
bool     mc_func_pressed   = false; // 记录 MC_FUNC 当前是否被按住
uint16_t captured_macro_id = 0;     // 动态捕获到的宏 ID (MACRO00 - MACRO15)

// 状态机核心控制变量
uint32_t macro_delay_timer = 0;     // 延时计时器
uint16_t next_action_delay = 0;     // 下一步动作前的随机延时
bool     waiting_for_delay = false; // 延时等待标记位

// 生成 50 到 150 毫秒之间的随机数
uint16_t get_random_delay(void) {
    return 10 + (rand() % 101);
}

// =================================================================================

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_ansi_108(
        KC_ESC,             KC_BRID,  KC_BRIU,  KC_MCTRL, KC_LNPAD, BL_DOWN,  BL_UP,    KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,    KC_VOLU,    KC_SNAP,  KC_SIRI,  BL_STEP,  KC_F13,   KC_F14,   KC_F15,   MC_FUNC, // 右上角最后一个键设为 MC_FUNC
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,     KC_BSPC,    KC_INS,   KC_HOME,  KC_PGUP,  KC_NUM,   KC_PSLS,  KC_PAST,  KC_PMNS,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,    KC_BSLS,    KC_DEL,   KC_END,   KC_PGDN,  KC_P7,    KC_P8,    KC_P9,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,              KC_ENT,                                   KC_P4,    KC_P5,    KC_P6,    KC_PPLS,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,              KC_RSFT,              KC_UP,              KC_P1,    KC_P2,    KC_P3,
        KC_LCTL,  KC_LOPTN, KC_LCMMD,                               KC_SPC,                                 KC_RCMMD, KC_ROPTN, MO(MAC_FN), KC_RCTL,    KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_P0,              KC_PDOT,  KC_PENT),

    [MAC_FN] = LAYOUT_ansi_108(
        _______,            KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,     KC_F12,     _______,  _______,  BL_TOGG,  _______,  _______,  _______,  _______,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,  _______,  _______,  _______,
        BL_TOGG,  BL_STEP,  BL_UP,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  BL_DOWN,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,              _______,                                  _______,  _______,  _______,  _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,              _______,              _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,            _______,  _______),

    [WIN_BASE] = LAYOUT_ansi_108(
        KC_ESC,             KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,     KC_F12,     KC_PSCR,  KC_CTANA, BL_STEP,  _______,  _______,  _______,  MC_FUNC, // 右上角最后一个键设为 MC_FUNC
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,     KC_BSPC,    KC_INS,   KC_HOME,  KC_PGUP,  KC_NUM,   KC_PSLS,  KC_PAST,  KC_PMNS,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,    KC_BSLS,    KC_DEL,   KC_END,   KC_PGDN,  KC_P7,    KC_P8,    KC_P9,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,              KC_ENT,                                   KC_P4,    KC_P5,    KC_P6,    KC_PPLS,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,              KC_RSFT,              KC_UP,              KC_P1,    KC_P2,    KC_P3,
        KC_LCTL,  KC_LWIN,  KC_LALT,                                KC_SPC,                                 KC_RALT,  KC_RWIN,  MO(WIN_FN), KC_RCTL,    KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_P0,              KC_PDOT,  KC_PENT),

    [WIN_FN] = LAYOUT_ansi_108(
        _______,            KC_BRID,  KC_BRIU,  KC_TASK,  KC_FILE,  BL_DOWN,  BL_UP,    KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,    KC_VOLU,    _______,  _______,  BL_TOGG,  _______,  _______,  _______,  _______,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,  _______,  _______,  _______,
        BL_TOGG,  BL_STEP,  BL_UP,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  BL_DOWN,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,              _______,                                  _______,  _______,  _______,  _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,              _______,              _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,    _______,    _______,  _______,  _______,  _______,            _______,  _______)
};
// clang-format on

// 持续扫描函数：利用纯非阻塞时间轴，优雅地控制宏的下一次触发
void matrix_scan_user(void) {
    if (!macro_loop_active || captured_macro_id == 0) {
        return;
    }

    // 如果当前正处于动作之间的随机延时冷却期
    if (waiting_for_delay) {
        if (timer_elapsed(macro_delay_timer) >= next_action_delay) {
            waiting_for_delay = false; // 冷却时间到，允许进行下一次宏吐字
        } else {
            return; // 时间没到，直接退出，把CPU完整让给键盘进行常规按键扫描
        }
    }

    // 触发被捕获宏的一次完整周期
    keyrecord_t mock_record;
    
    mock_record.event.pressed = true;
    process_macro(captured_macro_id, &mock_record); 
    
    mock_record.event.pressed = false;
    process_macro(captured_macro_id, &mock_record);

    // 每一次宏输出完后，不使用while死等，而是标记进入非阻塞等待，延时 50-150ms
    macro_delay_timer = timer_read();
    next_action_delay = get_random_delay();
    waiting_for_delay = true;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // ------------------ 拦截控制键 MC_FUNC ------------------
    if (keycode == MC_FUNC) {
        if (record->event.pressed) {
            mc_func_pressed = true;
            if (macro_loop_active) {
                macro_loop_active = false;
                waiting_for_delay = false;
                clear_keyboard(); // 再次按下当前键：一键瞬间打断暴毙，清空卡键
            }
        } else {
            mc_func_pressed = false;
        }
        return false;
    }

    // ------------------ 动态捕获 VIA 宏绑定 ------------------
    if (mc_func_pressed && record->event.pressed) {
        if (keycode >= MACRO00 && keycode <= MACRO15) {
            captured_macro_id = keycode;    
            macro_loop_active = true;       
            waiting_for_delay = false; // 立即启动
            return false;                   
        }
    }

    // ------------------ 【核心修改】宏内部按键动作变频拦截 ------------------
    // 凡是宏系统正在吐出的普通字符（按下或抬起），只要经过这里，全部强行注入 50~150ms 随机延迟
    if (macro_loop_active && (keycode >= KC_A && keycode <= KC_EXSEL)) {
        // 使用非阻塞延时：动态刷新下一步的等待时间戳，让宏内部的每一次物理按键动作频率彻底随机化
        macro_delay_timer = timer_read();
        next_action_delay = get_random_delay();
        waiting_for_delay = true;
    }

    if (!process_record_keychron_common(keycode, record)) {
        return false;
    }
    return true;
}
