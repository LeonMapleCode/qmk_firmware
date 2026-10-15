/* 爷自用
 *
- 宏循环的启动 / 停止键
- 按一下：播放 VIA 里宏的内容，**共 99 遍后自动停止**
- 宏内每个按键：**立即按下**，按住 `50 + 随机 10~110 ms` 后抬起（模拟真人），宏里的 down/up 对：down 立即执行，up 延迟随机 10~110ms（宏自带 delay 步照常生效）
- 每遍之间随机间隔 10~110ms
- 循环中**再按一次同一个键**：立即打断（补发未释放的键 + clear_keyboard）
- 循环中**按别的宏键（M1~M15）**：停止当前宏，从头计 99 次播放新宏
- 宏内容本身在 VIA / Keychron Launcher 的 MACRO 页编辑，**不需要重新编译固件**
- 宏文本里支持**随机延时令牌** `{#毫秒数}` 或 `{@毫秒数}` 如{#3000}随机取3000以内的值延时，与 VIA 原生 delay 步互不影响
 */

#include QMK_KEYBOARD_H
#include "keychron_common.h"
#include "dynamic_keymap.h"

enum layers {
    MAC_BASE,
    MAC_FN,
    WIN_BASE,
    WIN_FN,
};

#define FN_MAC MO(MAC_FN)
#define FN_WIN MO(WIN_FN)

#define MACRO_TAP_BASE_HOLD_MS  50
#define MACRO_HOLD_EXTRA_MIN_MS 10
#define MACRO_HOLD_EXTRA_MAX_MS 110
#define MACRO_LOOP_GAP_MIN_MS   10
#define MACRO_LOOP_GAP_MAX_MS   110
#define MACRO_LOOP_COUNT        99
#define MACRO_DELAY_TOKEN_MAX   59999
#define MACRO_LIT_MAX           20

enum macro_play_state {
    MP_IDLE,
    MP_FETCH,
    MP_HOLD,
    MP_DELAY,
    MP_GAP,
    MP_LITTYPE,
};

static bool     loop_active;
static uint8_t  play_state = MP_IDLE;
static uint8_t  loop_macro_id;
static uint8_t  loop_passes;
static uint16_t buf_size;
static uint16_t buf_pos;
static uint16_t macro_start;
static uint16_t play_deadline;
static uint16_t play_wait;
static uint8_t  held_key;
static bool     held_shift;
static bool     lit_collect;
static bool     lit_typing;
static uint8_t  lit_len;
static uint8_t  lit_pos;
static uint8_t  lit_buf[MACRO_LIT_MAX];
static uint32_t rng_state;

static uint16_t rand_range(uint16_t lo, uint16_t hi) {
    if (!rng_state) rng_state = timer_read32() | 1;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return lo + (uint16_t)(rng_state % (uint32_t)(hi - lo + 1));
}

static uint8_t macro_buf_byte(uint16_t offset) {
    uint8_t data = 0;
    if (offset < buf_size) dynamic_keymap_macro_get_buffer(offset, 1, &data);
    return data;
}

static void type_char_start(uint8_t c) {
    held_key   = pgm_read_byte(&ascii_to_keycode_lut[c]);
    held_shift = (pgm_read_byte(&ascii_to_shift_lut[c >> 3]) >> (c & 7)) & 1;
    if (held_shift) register_code(KC_LSFT);
    register_code(held_key);
    play_wait     = MACRO_TAP_BASE_HOLD_MS + rand_range(MACRO_HOLD_EXTRA_MIN_MS, MACRO_HOLD_EXTRA_MAX_MS);
    play_deadline = timer_read();
    play_state    = MP_HOLD;
}

static void macro_loop_stop(void) {
    loop_active = false;
    if (play_state == MP_HOLD && held_key) {
        unregister_code(held_key);
        if (held_shift) unregister_code(KC_LSFT);
    }
    held_key    = 0;
    held_shift  = false;
    lit_collect = false;
    lit_typing  = false;
    clear_keyboard();
    play_state = MP_IDLE;
}

static void macro_pass_end(void) {
    if (--loop_passes == 0) {
        macro_loop_stop();
        return;
    }
    buf_pos       = macro_start;
    lit_collect   = false;
    play_wait     = rand_range(MACRO_LOOP_GAP_MIN_MS, MACRO_LOOP_GAP_MAX_MS);
    play_deadline = timer_read();
    play_state    = MP_GAP;
}

static void macro_loop_start(uint8_t id) {
    uint8_t left = id;
    if (loop_active) macro_loop_stop();
    buf_size = dynamic_keymap_macro_get_buffer_size();
    if (buf_size == 0) return;
    if (macro_buf_byte(buf_size - 1) != 0) return;
    macro_start = 0;
    while (left > 0 && macro_start < buf_size) {
        if (macro_buf_byte(macro_start) == 0) left--;
        macro_start++;
    }
    if (macro_start >= buf_size || macro_buf_byte(macro_start) == 0) return;
    loop_macro_id = id;
    loop_passes   = MACRO_LOOP_COUNT;
    held_key      = 0;
    held_shift    = false;
    lit_collect   = false;
    lit_typing    = false;
    buf_pos       = macro_start;
    loop_active   = true;
    play_state    = MP_FETCH;
}

bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode >= QK_MACRO_0 && keycode <= QK_MACRO_15) {
        if (record->event.pressed) {
            uint8_t id = keycode - QK_MACRO_0;
            if (loop_active && loop_macro_id == id) {
                macro_loop_stop();
            } else {
                macro_loop_start(id);
            }
        }
        return false;
    }
    return true;
}

void matrix_scan_user(void) {
    uint8_t b, cmd;

    if (!loop_active) return;

    switch (play_state) {
        case MP_FETCH:
            if (buf_pos >= buf_size) {
                macro_pass_end();
                break;
            }
            b = macro_buf_byte(buf_pos++);
            if (lit_collect) {
                if (lit_len >= MACRO_LIT_MAX) {
                    buf_pos--;
                    lit_pos      = 0;
                    lit_typing   = true;
                    lit_collect  = false;
                    play_state   = MP_LITTYPE;
                    break;
                }
                if (b == '}' && lit_len >= 3) {
                    uint16_t v  = 0;
                    bool     ok = true;
                    for (uint8_t k = 2; k < lit_len; k++) {
                        uint8_t c = lit_buf[k];
                        if (c < '0' || c > '9' || v > MACRO_DELAY_TOKEN_MAX / 10) {
                            ok = false;
                            break;
                        }
                        v = v * 10 + (c - '0');
                    }
                    if (ok && v > 0) {
                        lit_collect   = false;
                        play_wait     = rand_range(1, v);
                        play_deadline = timer_read();
                        play_state    = MP_DELAY;
                        break;
                    }
                    if (lit_len < MACRO_LIT_MAX) {
                        lit_buf[lit_len++] = b;
                    }
                    lit_pos      = 0;
                    lit_typing   = true;
                    lit_collect  = false;
                    play_state   = MP_LITTYPE;
                    break;
                }
                if (b == 0) {
                    lit_collect = false;
                    macro_pass_end();
                    break;
                }
                if (b == SS_QMK_PREFIX || b >= 128) {
                    buf_pos--;
                    lit_pos      = 0;
                    lit_typing   = true;
                    lit_collect  = false;
                    play_state   = MP_LITTYPE;
                    break;
                }
                lit_buf[lit_len++] = b;
                break;
            }
            if (b == 0) {
                if (buf_pos == macro_start + 1) {
                    macro_loop_stop();
                    break;
                }
                macro_pass_end();
                break;
            }
            if (b == '{') {
                lit_buf[0]  = '{';
                lit_len     = 1;
                lit_collect = true;
                break;
            }
            if (b != SS_QMK_PREFIX) {
                if (b < 128) {
                    type_char_start(b);
                } else {
                    macro_pass_end();
                }
                break;
            }
            cmd = macro_buf_byte(buf_pos++);
            if (cmd == SS_TAP_CODE) {
                held_shift = false;
                held_key   = macro_buf_byte(buf_pos++);
                register_code(held_key);
                play_wait     = MACRO_TAP_BASE_HOLD_MS + rand_range(MACRO_HOLD_EXTRA_MIN_MS, MACRO_HOLD_EXTRA_MAX_MS);
                play_deadline = timer_read();
                play_state    = MP_HOLD;
            } else if (cmd == SS_DOWN_CODE) {
                register_code(macro_buf_byte(buf_pos++));
            } else if (cmd == SS_UP_CODE) {
                held_shift = false;
                held_key   = macro_buf_byte(buf_pos++);
                play_wait     = rand_range(MACRO_HOLD_EXTRA_MIN_MS, MACRO_HOLD_EXTRA_MAX_MS);
                play_deadline = timer_read();
                play_state    = MP_HOLD;
            } else if (cmd == SS_DELAY_CODE) {
                uint16_t ms = 0;
                while (buf_pos < buf_size) {
                    b = macro_buf_byte(buf_pos++);
                    if (b >= '0' && b <= '9') {
                        if (ms < 6000) ms = ms * 10 + (b - '0');
                        continue;
                    }
                    if (b != 0) {
                        play_wait     = ms;
                        play_deadline = timer_read();
                        play_state    = MP_DELAY;
                    } else {
                        macro_pass_end();
                    }
                    break;
                }
                if (play_state == MP_FETCH) {
                    buf_pos       = macro_start;
                    lit_collect   = false;
                    play_wait     = rand_range(MACRO_LOOP_GAP_MIN_MS, MACRO_LOOP_GAP_MAX_MS);
                    play_deadline = timer_read();
                    play_state    = MP_GAP;
                }
            } else {
                buf_pos       = macro_start;
                lit_collect   = false;
                play_wait     = rand_range(MACRO_LOOP_GAP_MIN_MS, MACRO_LOOP_GAP_MAX_MS);
                play_deadline = timer_read();
                play_state    = MP_GAP;
            }
            break;
        case MP_HOLD:
            if (timer_elapsed(play_deadline) >= play_wait) {
                if (held_key) unregister_code(held_key);
                if (held_shift) unregister_code(KC_LSFT);
                held_key   = 0;
                held_shift = false;
                play_state = lit_typing ? MP_LITTYPE : MP_FETCH;
            }
            break;
        case MP_LITTYPE:
            if (lit_pos >= lit_len) {
                lit_typing = false;
                play_state = MP_FETCH;
                break;
            }
            type_char_start(lit_buf[lit_pos++]);
            break;
        case MP_DELAY:
        case MP_GAP:
            if (timer_elapsed(play_deadline) >= play_wait) play_state = MP_FETCH;
            break;
    }
}

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [MAC_BASE] = LAYOUT_ansi_108(
        KC_ESC,             KC_BRID,  KC_BRIU,  KC_MCTRL, KC_LNPAD, BL_DOWN,  BL_UP,    KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,  KC_SNAP,  KC_SIRI,  BL_STEP,  KC_F13,   KC_F14,   KC_F15,   QK_MACRO_0,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_INS,   KC_HOME,  KC_PGUP,  KC_NUM,   KC_PSLS,  KC_PAST,  KC_PMNS,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,  KC_DEL,   KC_END,   KC_PGDN,  KC_P7,    KC_P8,    KC_P9,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,                                 KC_P4,    KC_P5,    KC_P6,    KC_PPLS,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,            KC_UP,              KC_P1,    KC_P2,    KC_P3,
        KC_LCTL,  KC_LOPTN, KC_LCMMD,                               KC_SPC,                                 KC_RCMMD, KC_ROPTN, FN_MAC,   KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_P0,              KC_PDOT,  KC_PENT),

    [MAC_FN] = LAYOUT_ansi_108(
        _______,            KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   _______,  _______,  BL_TOGG,  _______,  _______,  _______,  _______,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        BL_TOGG,  BL_STEP,  BL_UP,    _______,  _______,  BL_SPI,   _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  BL_DOWN,  _______,  _______,  BL_SPD,   _______,  _______,  _______,  _______,  _______,  _______,            _______,                                _______,  _______,  _______,  _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,            _______,            _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______),

    [WIN_BASE] = LAYOUT_ansi_108(
        KC_ESC,             KC_F1,    KC_F2,    KC_F3,    KC_F4,    KC_F5,    KC_F6,    KC_F7,    KC_F8,    KC_F9,    KC_F10,   KC_F11,   KC_F12,   KC_PSCR,  KC_CTANA, BL_STEP,  _______,  _______,  _______,  QK_MACRO_0,
        KC_GRV,   KC_1,     KC_2,     KC_3,     KC_4,     KC_5,     KC_6,     KC_7,     KC_8,     KC_9,     KC_0,     KC_MINS,  KC_EQL,   KC_BSPC,  KC_INS,   KC_HOME,  KC_PGUP,  KC_NUM,   KC_PSLS,  KC_PAST,  KC_PMNS,
        KC_TAB,   KC_Q,     KC_W,     KC_E,     KC_R,     KC_T,     KC_Y,     KC_U,     KC_I,     KC_O,     KC_P,     KC_LBRC,  KC_RBRC,  KC_BSLS,  KC_DEL,   KC_END,   KC_PGDN,  KC_P7,    KC_P8,    KC_P9,
        KC_CAPS,  KC_A,     KC_S,     KC_D,     KC_F,     KC_G,     KC_H,     KC_J,     KC_K,     KC_L,     KC_SCLN,  KC_QUOT,            KC_ENT,                                 KC_P4,    KC_P5,    KC_P6,    KC_PPLS,
        KC_LSFT,            KC_Z,     KC_X,     KC_C,     KC_V,     KC_B,     KC_N,     KC_M,     KC_COMM,  KC_DOT,   KC_SLSH,            KC_RSFT,            KC_UP,              KC_P1,    KC_P2,    KC_P3,
        KC_LCTL,  KC_LWIN,  KC_LALT,                                KC_SPC,                                 KC_RALT,  KC_RWIN,  FN_WIN,   KC_RCTL,  KC_LEFT,  KC_DOWN,  KC_RGHT,  KC_P0,              KC_PDOT,  KC_PENT),

    [WIN_FN] = LAYOUT_ansi_108(
        _______,            KC_BRID,  KC_BRIU,  KC_TASK,  KC_FILE,  BL_DOWN,  BL_UP,    KC_MPRV,  KC_MPLY,  KC_MNXT,  KC_MUTE,  KC_VOLD,  KC_VOLU,  _______,  _______,  BL_TOGG,  _______,  _______,  _______,  _______,
        _______,  BT_HST1,  BT_HST2,  BT_HST3,  P2P4G,    _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        BL_TOGG,  BL_STEP,  BL_UP,    _______,  _______,  BL_SPI,   _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,
        _______,  _______,  BL_DOWN,  _______,  _______,  BL_SPD,   _______,  _______,  _______,  _______,  _______,  _______,            _______,                                _______,  _______,  _______,  _______,
        _______,            _______,  _______,  _______,  _______,  BAT_LVL,  NK_TOGG,  _______,  _______,  _______,  _______,            _______,            _______,            _______,  _______,  _______,
        _______,  _______,  _______,                                _______,                                _______,  _______,  _______,  _______,  _______,  _______,  _______,  _______,            _______,  _______)
};
// clang-format on
