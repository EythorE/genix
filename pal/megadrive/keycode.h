/*
 * Key codes for special keys
 *
 * These match the codes used in the Saturn keyboard scancode table.
 * Non-printable keys use values 0x80+ to avoid collision with ASCII.
 */
#ifndef _KEYCODE_H
#define _KEYCODE_H

#define KEY_BS      0x08
#define KEY_TAB     0x09
#define KEY_ENTER   0x0D
#define KEY_ESC     0x1B

/* Special keys (non-ASCII) */
#define KEY_F1      0x81
#define KEY_F2      0x82
#define KEY_F3      0x83
#define KEY_F4      0x84
#define KEY_F5      0x85
#define KEY_F6      0x86
#define KEY_F7      0x87
#define KEY_F8      0x88
#define KEY_F9      0x89
#define KEY_F10     0x8A
#define KEY_F11     0x8B
#define KEY_F12     0x8C

#define KEY_INSERT  0x90
#define KEY_DEL     0x7F
#define KEY_HOME    0x91
#define KEY_END     0x92
#define KEY_PGUP    0x93
#define KEY_PGDOWN  0x94
#define KEY_UP      0x95
#define KEY_DOWN    0x96
#define KEY_LEFT    0x97
#define KEY_RIGHT   0x98
#define KEY_PRINT   0x99
#define KEY_PAUSE   0x9A

/* Control key modifier macro */
#define CTRL(c)     ((c) & 0x1F)

#endif /* _KEYCODE_H */
