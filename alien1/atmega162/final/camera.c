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

/* NOTE: The camera is on Port A, Outputs 3 (focus) and 2 (fullshutter). */

/* How long to wait between taking photos, in seconds.
 * Cannot be < (camera_hold_focus + camera_hold_both) */
#define camera_rate          20
#define camera_hold_focus    2
#define camera_hold_both     1

/* Basically, when seconds == camera_rate we want to reset the counter
 * In order to do that, the last step of photo-taking must happen then,
 * ie. that's when we disconnect the focus/shutter shortings. */
#define camera_state_focus   camera_rate - camera_hold_focus - camera_hold_both
#define camera_state_both    camera_rate - camera_hold_both
#define camera_state_off     camera_rate

uint8_t camera_state;

void camera_proc()
{
  /* Called every second. */
  switch (camera_state)
  {
    /* This one goes first because it's the only one that doesn't need the
     * camera_state++ - so it's the only one that break;s */

    case camera_state_off:
      PORTA &= ~((_BV(PA3)) | (_BV(PA2)));
      camera_state = 0;
      break;

    /* Don't break. These stack up, ie. _both has both PA3 and PA2 set,
     * focus has PB0 set, and both have state++ */

    case camera_state_both:
      PORTA |= _BV(PA2);
      /* Run on, over the next case, to set PA3 */

    case camera_state_focus:
      PORTA |= _BV(PA3);
      /* Run on to increment _state */

    default:
      /* When it's anything but _off, we bump up the state, which acts
       * also as a seconds counter */
      camera_state++;
  }
}

void camera_init()
{
  PORTA &= ~((_BV(PA3))  | (_BV(PA2)));    /* Both off */
  DDRA  |=  ((_BV(DDA3)) | (_BV(DDA2)));   /* Both outputs */
}
