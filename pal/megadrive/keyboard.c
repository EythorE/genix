/*
 * Saturn keyboard driver for Mega Drive
 *
 * Full scancode-to-ASCII translation with modifier support (shift, ctrl,
 * alt, caps lock). Uses the Saturn keyboard protocol via controller port 2.
 *
 * Adapted from FUZIX platform-megadrive
 * Source: https://github.com/EythorE/FUZIX/tree/megadrive/Kernel/platform/platform-megadrive
 */
#include <stdint.h>
#include "keyboard.h"
#include "keycode.h"

/* Forward declaration — defined at bottom of file */
static const uint8_t saturn_keymap[];

#define SCANCODE_LEFT_ALT    0x11
#define SCANCODE_RIGHT_ALT   0x17
#define SCANCODE_LEFT_SHIFT  0x12
#define SCANCODE_RIGHT_SHIFT 0x59
#define SCANCODE_LEFT_CTRL   0x14
#define SCANCODE_RIGHT_CTRL  0x18
#define SCANCODE_CAPS_LOCK   0x58

/* From keyboard_read.S */
extern void initKeyboard(void);
extern int ReadKeyboard(void);
extern uint8_t scancode_buffer[12];

static int shift_down = 0;
static int ctrl_down = 0;
static int alt_down = 0;
static int caps_lock = 0;

void keyboard_init(void) {
    shift_down = 0;
    ctrl_down = 0;
    alt_down = 0;
    caps_lock = 0;
    initKeyboard();
}

/*
 * Update modifier key state based on scancode
 * Returns 1 if the keypress was a modifier key
 */
static int update_modifiers(uint8_t scancode, int is_make) {
    int is_modifier = 1;

    switch (scancode) {
        case SCANCODE_LEFT_SHIFT:
        case SCANCODE_RIGHT_SHIFT:
            shift_down = is_make;
            break;
        case SCANCODE_LEFT_CTRL:
        case SCANCODE_RIGHT_CTRL:
            ctrl_down = is_make;
            break;
        case SCANCODE_LEFT_ALT:
        case SCANCODE_RIGHT_ALT:
            alt_down = is_make;
            break;
        case SCANCODE_CAPS_LOCK:
            if (is_make)
                caps_lock = !caps_lock;
            break;
        default:
            is_modifier = 0;
            break;
    }
    return is_modifier;
}

static uint8_t apply_caps_lock(uint8_t keycode) {
    if (keycode >= 'a' && keycode <= 'z')
        return keycode - 32;
    else if (keycode >= 'A' && keycode <= 'Z')
        return keycode + 32;
    return keycode;
}

static uint8_t apply_ctrl(uint8_t keycode) {
    if (keycode >= 'a' && keycode <= 'z')
        return CTRL(keycode);
    else if (keycode >= 'A' && keycode <= 'Z')
        return CTRL(keycode + 32);

    switch (keycode) {
        case '@':  return CTRL('@');
        case '[':  return CTRL('[');
        case '\\': return CTRL('\\');
        case ']':  return CTRL(']');
        case '^':  return CTRL('^');
        case '_':  return CTRL('_');
    }
    return keycode;
}

/*
 * Read a key from the keyboard, handling modifiers and special keys.
 * Returns the keycode or 0 if no key was pressed.
 */
uint8_t keyboard_read(void) {
    int result;
    uint8_t make_break, scancode, keycode;
    int is_make;

    result = ReadKeyboard();
    if (result != 0)
        return 0;

    make_break = scancode_buffer[7];
    is_make = (make_break & 0x08) != 0;

    scancode = (scancode_buffer[8] << 4) | scancode_buffer[9];

    if (update_modifiers(scancode, is_make))
        return 0;

    /* Only process key-down events for non-modifiers */
    if (!is_make)
        return 0;

    if (shift_down)
        keycode = saturn_keymap[scancode + 256];
    else
        keycode = saturn_keymap[scancode];

    if (caps_lock)
        keycode = apply_caps_lock(keycode);

    if (ctrl_down)
        keycode = apply_ctrl(keycode);

    return keycode;
}

uint8_t keyboard_modifiers(void) {
    uint8_t mods = 0;
    if (shift_down) mods |= 1;
    if (ctrl_down)  mods |= 2;
    if (alt_down)   mods |= 4;
    if (caps_lock)  mods |= 8;
    return mods;
}

/*
 * Saturn keyboard scancode-to-ASCII lookup table
 * First 256 bytes: unshifted, next 256 bytes: shifted
 * https://plutiedev.com/saturn-keyboard#scancode-to-ascii
 */
static const uint8_t saturn_keymap[] = {
    /* Unshifted keys (0x00-0xFF) */
    0,    KEY_F9, 0,    KEY_F5, KEY_F3, KEY_F1, KEY_F2, KEY_F12,
    0,    KEY_F10, KEY_F8, KEY_F6, KEY_F4, KEY_TAB, '`',  0,
    0,    0,    0,    0,    0,    'q',  '1',  0,
    0,    KEY_ENTER, 'z',  's',  'a',  'w',  '2',  0,
    0,    'c',  'x',  'd',  'e',  '4',  '3',  0,
    0,    ' ',  'v',  'f',  't',  'r',  '5',  0,
    0,    'n',  'b',  'h',  'g',  'y',  '6',  0,
    0,    0,    'm',  'j',  'u',  '7',  '8',  0,
    0,    ',',  'k',  'i',  'o',  '0',  '9',  0,
    0,    '.',  '/',  'l',  ';',  'p',  '-',  0,
    0,    0,    '\'', 0,    '[',  '=',  0,    0,
    0,    0,    KEY_ENTER, ']',  0,    '\\', 0,    0,
    0,    0,    0,    0,    0,    0,    KEY_BS, 0,
    0,    '1',  0,    '4',  '7',  0,    0,    0,
    '0',  '.',  '2',  '5',  '6',  '8',  KEY_ESC, 0,
    KEY_F11, '+',  '3',  '-',  '*',  '9',  0,    0,
    '/',  KEY_INSERT, KEY_PAUSE, KEY_F7, KEY_PRINT, KEY_DEL, KEY_LEFT, KEY_HOME,
    KEY_END, KEY_UP, KEY_DOWN, KEY_PGUP, KEY_PGDOWN, KEY_RIGHT, 0, 0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,

    /* Shifted keys (0x100-0x1FF) */
    0,    KEY_F9, 0,    KEY_F5, KEY_F3, KEY_F1, KEY_F2, KEY_F12,
    0,    KEY_F10, KEY_F8, KEY_F6, KEY_F4, KEY_TAB, '~',  0,
    0,    0,    0,    0,    0,    'Q',  '!',  0,
    0,    KEY_ENTER, 'Z',  'S',  'A',  'W',  '@',  0,
    0,    'C',  'X',  'D',  'E',  '$',  '#',  0,
    0,    ' ',  'V',  'F',  'T',  'R',  '%',  0,
    0,    'N',  'B',  'H',  'G',  'Y',  '^',  0,
    0,    0,    'M',  'J',  'U',  '&',  '*',  0,
    0,    '<',  'K',  'I',  'O',  ')',  '(',  0,
    0,    '>',  '?',  'L',  ':',  'P',  '_',  0,
    0,    0,    '"',  0,    '{',  '+',  0,    0,
    0,    0,    KEY_ENTER, '}',  0,    '|',  0,    0,
    0,    0,    0,    0,    0,    0,    KEY_BS, 0,
    0,    '1',  0,    '4',  '7',  0,    0,    0,
    '0',  '.',  '2',  '5',  '6',  '8',  KEY_ESC, 0,
    KEY_F11, '+',  '3',  '-',  '*',  '9',  0,    0,
    '/',  KEY_INSERT, KEY_PAUSE, KEY_F7, KEY_PRINT, KEY_DEL, KEY_LEFT, KEY_HOME,
    KEY_END, KEY_UP, KEY_DOWN, KEY_PGUP, KEY_PGDOWN, KEY_RIGHT, 0, 0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
};
