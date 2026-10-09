#include "devices/kbd.h"
#include <ctype.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/io.h"

/* 키보드 데이터 레지스터 포트. */
#define DATA_REG 0x60

/* Shift 계열 키의 현재 상태.
   눌려 있으면 true, 아니면 false. */
static bool left_shift, right_shift;    /* 왼쪽 및 오른쪽 Shift 키. */
static bool left_alt, right_alt;        /* 왼쪽 및 오른쪽 Alt 키. */
static bool left_ctrl, right_ctrl;      /* 왼쪽 및 오른쪽 Ctrl 키. */

/* Caps Lock 상태.
   켜져 있으면 true, 꺼져 있으면 false. */
static bool caps_lock;

/* 눌린 키의 개수. */
static int64_t key_cnt;

static intr_handler_func keyboard_interrupt;

/* 키보드를 초기화한다. */
void
kbd_init (void) {
	intr_register_ext (0x21, keyboard_interrupt, "8042 Keyboard");
}

/* 키보드 통계를 출력한다. */
void
kbd_print_stats (void) {
	printf ("Keyboard: %lld keys pressed\n", key_cnt);
}

/* 연속된 스캔코드 집합을 문자에 매핑한다. */
struct keymap {
	uint8_t first_scancode;     /* 첫 번째 스캔코드. */
	const char *chars;          /* chars[0]은 스캔코드 first_scancode,
								   chars[1]은 스캔코드 first_scancode + 1,
								   이런 식으로 문자열 끝까지 대응된다. */
};

/* Shift 키가 눌렸는지와 관계없이 같은 문자를 만드는 키들.
   영문자의 대소문자는 예외인데, 이는 다른 곳에서
   처리한다.  */
static const struct keymap invariant_keymap[] = {
	{0x01, "\033"},
	{0x0e, "\b"},
	{0x0f, "\tQWERTYUIOP"},
	{0x1c, "\r"},
	{0x1e, "ASDFGHJKL"},
	{0x2c, "ZXCVBNM"},
	{0x37, "*"},
	{0x39, " "},
	{0, NULL},
};

/* Shift 없이 눌렸을 때의 문자. Shift 여부가 영향을 주는
   키들에 해당한다. */
static const struct keymap unshifted_keymap[] = {
	{0x02, "1234567890-="},
	{0x1a, "[]"},
	{0x27, ";'`"},
	{0x2b, "\\"},
	{0x33, ",./"},
	{0, NULL},
};

/* Shift와 함께 눌렸을 때의 문자. Shift 여부가 영향을 주는
   키들에 해당한다. */
static const struct keymap shifted_keymap[] = {
	{0x02, "!@#$%^&*()_+"},
	{0x1a, "{}"},
	{0x27, ":\"~"},
	{0x2b, "|"},
	{0x33, "<>?"},
	{0, NULL},
};

static bool map_key (const struct keymap[], unsigned scancode, uint8_t *);

static void
keyboard_interrupt (struct intr_frame *args UNUSED) {
	/* Shift 계열 키의 상태. */
	bool shift = left_shift || right_shift;
	bool alt = left_alt || right_alt;
	bool ctrl = left_ctrl || right_ctrl;

	/* 키보드 스캔코드. */
	unsigned code;

	/* 키가 눌렸으면 false, 떼어졌으면 true. */
	bool release;

	/* `code'에 대응하는 문자. */
	uint8_t c;

	/* 스캔코드를 읽는다. 접두(prefix) 코드라면 두 번째 바이트까지 읽는다. */
	code = inb (DATA_REG);
	if (code == 0xe0)
		code = (code << 8) | inb (DATA_REG);

	/* 0x80 비트로 키 누름과 키 뗌을 구별한다
	   (접두 코드가 있어도 마찬가지). */
	release = (code & 0x80) != 0;
	code &= ~0x80u;

	/* 키 해석. */
	if (code == 0x3a) {
		/* Caps Lock. */
		if (!release)
			caps_lock = !caps_lock;
	} else if (map_key (invariant_keymap, code, &c)
			|| (!shift && map_key (unshifted_keymap, code, &c))
			|| (shift && map_key (shifted_keymap, code, &c))) {
		/* 일반 문자. */
		if (!release) {
			/* Ctrl, Shift 처리.
			   Ctrl이 Shift보다 우선한다는 점에 유의. */
			if (ctrl && c >= 0x40 && c < 0x60) {
				/* A는 0x41, Ctrl+A는 0x01, 기타 등등. */
				c -= 0x40;
			} else if (shift == caps_lock)
				c = tolower (c);

			/* 최상위 비트를 설정하여 Alt를 처리한다.
			   이 0x80은 키 누름과 키 뗌을 구별하는 데 쓰는
			   0x80과는 관계가 없다. */
			if (alt)
				c += 0x80;

			/* 키보드 버퍼에 추가한다. */
			if (!input_full ()) {
				key_cnt++;
				input_putc (c);
			}
		}
	} else {
		/* 키코드를 Shift 상태 변수에 매핑한다. */
		struct shift_key {
			unsigned scancode;
			bool *state_var;
		};

		/* Shift 계열 키 테이블. */
		static const struct shift_key shift_keys[] = {
			{  0x2a, &left_shift},
			{  0x36, &right_shift},
			{  0x38, &left_alt},
			{0xe038, &right_alt},
			{  0x1d, &left_ctrl},
			{0xe01d, &right_ctrl},
			{0,      NULL},
		};

		const struct shift_key *key;

		/* 테이블을 탐색한다. */
		for (key = shift_keys; key->scancode != 0; key++)
			if (key->scancode == code) {
				*key->state_var = !release;
				break;
			}
	}
}

/* 키맵 배열 K에서 SCANCODE를 찾는다.
   찾으면 *C를 대응하는 문자로 설정하고 true를
   반환한다.
   찾지 못하면 false를 반환하고 C는 무시된다. */
static bool
map_key (const struct keymap k[], unsigned scancode, uint8_t *c) {
	for (; k->first_scancode != 0; k++)
		if (scancode >= k->first_scancode
				&& scancode < k->first_scancode + strlen (k->chars)) {
			*c = k->chars[scancode - k->first_scancode];
			return true;
		}

	return false;
}
