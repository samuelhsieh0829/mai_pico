#ifndef TEST_PICO_UNIQUE_ID_H
#define TEST_PICO_UNIQUE_ID_H

#include <stdint.h>
typedef struct { uint8_t id[8]; } pico_unique_board_id_t;
void pico_get_unique_board_id(pico_unique_board_id_t *id);

#endif
