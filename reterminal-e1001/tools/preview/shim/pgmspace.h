// The font tables are plain arrays on the host.
#pragma once
#define PROGMEM
#define pgm_read_byte(a) (*(const unsigned char*)(a))
