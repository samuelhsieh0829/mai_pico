/*
 * Machine-readable Web configurator protocol (CFG/1).
 */

#ifndef CFG_PROTOCOL_H
#define CFG_PROTOCOL_H

#include <stdbool.h>

void cfg_command(int argc, char *argv[]);
void cfg_update();
bool cfg_is_active();

#endif
