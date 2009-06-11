/*
    Copyright (C) 2008  Daniel Richman

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    For a full copy of the GNU General Public License, 
    see <http://www.gnu.org/licenses/>.
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>
#include <stdlib.h>

#include "camera.h"
#include "gps.h"
#include "hexdump.h"
#include "log.h"
#include "main.h"
#include "messages.h"
#include "radio.h"
#include "sms.h"
#include "statusled.h"
#include "temperature.h"
#include "timer1.h"
#include "timer3.h"
#include "watchdog.h"

/* Log header can be changed by any modules during initialisation.
 * It's the first byte sent to the SD card - could contain reset info,
 * for example */
uint8_t log_state, log_header;

/* TODO: log.c: add ISR */

void log_start()
{
  /* TODO: log.c: log_start() */
  /* log_state++; */
}

void log_init()
{
  /* TODO: log.c: log_init() */
}
