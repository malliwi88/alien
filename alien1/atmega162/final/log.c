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
#define log_mode_null       0
#define log_mode_commanding 1
#define log_mode_waiting    2

uint8_t  log_state, log_substate, log_mode;
uint8_t  log_command[6];   /* 1byte command, 4byte argument, 1byte crc */
uint16_t log_timeout;
uint32_t log_position, log_position_b;

/* Response should be:  0x01, 0x00, 0x00, 0x01, 0xAA */
/*                       ack, fill, fill, echo, echo */
#define log_cmd8_expected_response_length 5
uint8_t log_cmd8_expected_response[log_cmd8_expected_response_length] = 
                                         { 0x01, 0x00, 0x00, 0x01, 0xAA };

/* SS   - PB4  (Master Output)
   MOSI - PB5  (Master Output)
   MISO - PB6  (Master Input)
   SCK  - PB7  (Master Output) */

#define SS_HIGH  PORTB |=   _BV(PB4)
#define SS_LOW   PORTB &= ~(_BV(PB4))

/* Every first command-byte starts with 0b01xxxxxx where xxxxxx is a command */
#define SDCMD(x)  (0x40 | x)

/* We set the block length to 128 and put a message in every block. This is the
 * easiest way to handle anything that might occur due to half-written blocks 
 * etc. The first block contains location of the block to start/resume on. 
 * This value is updated every quarter-megabyte; which will happen roughly
 * every half-hour. */

#if messages_max_length > 128
  #error log block size is too small, messages_max_length is greather than 128
#endif

#define log_quarter_megabyte_mask 0x0003FFFF
#define log_quarter_megabyte      0x00040000

/* For more information on this, see the SD Card Association's Physical layer 
 * specification, available easily w. no registration on their website. */

ISR(SPI_STC_vect)
{
  uint8_t c;
  c = SPDR;

  /* If log_mode_commanding is set by the previous state, then that will be 
   * completed (ie. the command will be sent). Then log_mode_waiting will
   * start, although this is kept separate from _commanding by an else
   * if and so will begin in the next interrupt. When it has finished
   * waiting it will set log_mode to null, and then will fall through to 
   * the switch, which can handle the response. */

  if (log_mode == log_mode_commanding)
  {
    /* Send data */
    SPDR = log_command[log_substate];

    /* Reset buffer */
    log_command[log_substate] = 0;

    /* Next byte */
    log_substate++;

    if (log_substate == 6)
    {
      log_substate = 0;
      log_mode     = log_mode_waiting;
      /* Code below will handle the response */
    }
  }
  else if (log_mode == log_mode_waiting)
  {
    if (c == 0xFF)
    {
      log_timeout++;

      if (log_timeout == log_timeout_max)
      {
        log_timeout = 0;
        log_state   = log_state_deselect;
        log_mode    = log_mode_null;
        SS_HIGH;
      }

      SPDR = 0xFF;    /* Get another byte */
      return;         /* No going onto the switch */
    }
    else
    {
      log_timeout  = 0;
      log_mode = log_mode_null;
    }
  }

  if (log_mode == log_mode_null)
  {
    switch (log_state)
    {
      case log_state_initreset:
        /* The card needs 80 clocks (10 bytes) atleast to intialise. */
        log_substate++;

        if (log_substate == 100)
        {
          /* Next status: _reset, CMD0 */
          log_state++;
          log_substate = 0;

          /* Select slave (if not already selected) */
          SS_LOW;

          log_mode = log_mode_commanding;
          log_command[0] = SDCMD(0);
          log_command[5] = 0x95;      /* One of two required CRCs */

          /* No arguments */
        }

        SPDR = 0xFF;
        break;

      case log_state_reset:
        /* Response to CMD0 is a single 0x01 */

        if (c == 0x01)
        {
          /* Success! */
          log_state++;

          /* Next: check voltage info (CMD8) */
          log_mode = log_mode_commanding;
          log_command[0] = SDCMD(8);
          log_command[3] = 0x01;      /* Specify Voltage Range   */
          log_command[4] = 0xAA;      /* Echoed test string      */
          log_command[5] = 0x87;      /* CRC (last one required) */
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        /* We either need to get another byte, bail, or go to the next command
         * at this point in each case. In all cases a 0xFF is appropriate as it
         * will retrieve a byte, provide a space between now and the next cmd
         * or contribute to the bailing spin-down. */
        SPDR = 0xFF;
        break;

      case log_state_getocr:
        if (c == log_cmd8_expected_response[log_substate])
        {
          log_substate++;

          if (log_substate == log_cmd8_expected_response_length)
          {
            /* Success! */
            log_state++;
            log_substate = 0;

            /* Next: Ready Wait: CMD1 */
            log_mode = log_mode_commanding;
            log_command[0] = SDCMD(1);
          }
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_readywait:
        /* This will respond with one byte. If it's 0x01 then it's still 
         * initialising. If it's 0x00 then it's ready. Otherwise, bad 
         * things have happened */

        if (c == 0x01)
        {
          /* Not ready yet; re-send CMD1 */
          log_mode = log_mode_commanding;
          log_command[0] = SDCMD(1);
        }
        else if (c == 0x00)
        {
          /* Success - It's ready! */
          log_state++;

          /* Next: Set block length to 128 */
          log_mode = log_mode_commanding;
          log_command[0] = SDCMD(16);
          log_command[4] = 0x80;         /* 0x00000080 = 128 */
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_setblklen:
        /* Expected response: single byte; 0x00 */

        if (c == 0x00)
        {
          /* Success - It's ready! */
          log_state++;

          /* Next: Read Superblock */
          log_mode = log_mode_commanding;
          log_command[0] = SDCMD(17);
        }
        else        
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_readsuper_r:
        /* Expected response: single byte; 0x00 */

        if (c == 0x00)
        {
          /* Success */
          log_state++;

          /* Now wait for the data-start-token */
          log_mode = log_mode_waiting;
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_readsuper_s:
        /* Expected response: single byte; 0xFE */

        if (c == 0xFE)
        {
          /* Success - DATA INCOMING! */
          log_state++;
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_readsuper_d:
        /* 128 Bytes of data... */

        /* The location at which we should start wriitng is a 2 byte
         * long integer, repeated three times. If there is a half-written
         * value (ie. it crashed during write), we try to deduce a good value:
         *  - If the first and second are (atleast) the same, use that
         *  - If the last two are the same then use that value (ie. first is
              corrupted) 
         *  - Otherwise: If none are the same, use the first (ie. second is
              corrupted) */

        /* (value % 4) is the same as (value & 0x03) 
         * This represents the byte in log_position that is being considered,
         * ie. byte_reading_number % 4 */
        #define log_position_byte    (ba(log_position)[log_substate & 0x03])
        #define log_position_b_byte  (ba(log_position_b)[log_substate & 0x03])

        /* We use log_timeout as a byte to store temporary stuff */
        #define log_position_state log_timeout

        if (log_substate < 4)
        {
          /* Reading the value */
          log_position_byte = c;
        }
        else if (log_substate < 8)
        {
          /* Read the second value whilst storing the first */
          log_position_b_byte = c;

          if (log_position_byte != c)
          {
            /* Flag the fact that the second is not the same to the first */
            log_position_state = 1;
          }
        }
        else if (log_substate < 12)
        {
          if (log_position_state != 0)
          {
            /* If the second is not the same as the first, then compare the
             * third to the second. */

            if (log_position_b_byte != c)
            {
              /* Second != Third */
              log_position_state = 2;
            }
          }
        }

        SPDR = 0xFF;

        log_substate++;
        if (log_substate == 128 + 2)  /* Two CRCs, ignored */
        {
          /* By now, all tests have completed, time to get the right value. 
           * If log_position_state == 1 then use the second. Otherwise, use
           * the first. If we're using the first it is already in the right 
           * place. Using the second we must move position_b to position... */

          if (log_position_state == 1)
          {
            log_position = log_position_b;
          }

          /* Check that it's a valid quarter-megabyte, and != 0 */
          if (log_position & log_quarter_megabyte_mask || log_position == 0)
          {
            /* It's bad. Use the default. */
            log_position  = log_quarter_megabyte;
          }
          else
          {
            /* It's ok; move onto the next quarter megabyte to write */
            log_position += log_quarter_megabyte;
          }

          /* Success */
          log_state++;
          log_substate = 0;
          log_position_state = 0;

          /* Next: Run on into _idle, which prepares to write data */
        }
        else
        {
          /* Else: Don't run on to idle... */
          break;
        }

        #undef log_position_byte
        #undef log_position_b_byte
        #undef log_position_state

      case log_state_idle:
      case log_state_write_data:
        /* Next: Write a block. But where? */
        log_mode = log_mode_commanding;
        log_command[0] = SDCMD(24);

        /* Are we about to start a new quarter-megabyte? 
         * If we are, have we already written the superblock?
         * (check log_state == _write_data) */
        if ((log_position & log_quarter_megabyte_mask) || 
            (log_state == log_state_write_data))
        {
          /* Unpack log_command, solving endianness. This 
           * actually produces very nice assembly with -O2,
           * gcc optimises it to 4 loads and 4 saves. */
          log_command[1] = (log_position & 0xFF000000) >> 24;
          log_command[2] = (log_position & 0x00FF0000) >> 16;
          log_command[3] = (log_position & 0x0000FF00) >> 8;
          log_command[4] = (log_position & 0x000000FF); 

          log_state = log_state_writing_data;
          log_position += 128;
        }
        else
        {
          /* Update the superblock - write block 0 */
          log_state = log_state_writing_super;
        }

        SPDR = 0xFF;
        break;

      case log_state_writing_super:
      case log_state_writing_data:
        /* Expect 0x00 to acknowledge write command, then send 0xFE (data
         * token), then send data. */
        if (log_substate == 0)
        {
          if (c == 0x00)
          {
            /* Good, now transmit data token... */
            SPDR = 0xFE;
            log_substate++;
          }
          else
          {
            /* Bad. */
            log_state = log_state_deselect;
            SS_HIGH;
            SPDR = 0xFF;
          }
        }
        else
        {
          if (log_state == log_state_writing_super)
          {
            if (log_substate < 12 + 1)
            {
              /* Minus one, modulo 4 to repeat 4 bytes 3 times 
               * Doing & 0x03 is the same as % 4, just faster */
              SPDR = ba(log_position)[ (log_substate - 1) & 0x03 ];
            }
            else
            {
              SPDR = 0x00;
            }
          }
          else /* writing_data */
          {
            SPDR = messages_get_char(&log_data);
          }

          log_substate++; 

          if (log_substate == 128 + 1 + 2)  /* one data token and 2 crcs */
          {
            /* Next: Wait for data_response token */
            log_mode = log_mode_waiting;
            log_state++;
            log_substate = 0;
          }
        }
        break;

      case log_state_writewait_super:
      case log_state_writewait_data:
        /* Expected: 0x*5 */
        if (log_substate == 0)
        {
          if ((c & 0x0F) == 0x05)
          {
            /* Now we wait for the write to finish */
            log_substate = 1;
          }
        }
        else
        {
          if (c == 0xFF)
          {
            /* Success! */
            log_state++;
            log_substate = 0;

            /* Next: Check Status (CMD13) */
            log_mode = log_mode_commanding;
            log_command[0] = SDCMD(13);
          }
        }

        SPDR = 0xFF;
        break;

      case log_state_writecheck_super:
      case log_state_writecheck_data:
        /* Expected Response: 0x00, 0x00 */
        if (c == 0x00)
        {
          if (log_substate == 0)
          {
            log_substate = 1;
          }
          else
          {
            /* Success! */
            log_substate = 0;

            /* The next state will set the command itself. */
            if (log_state == log_state_writecheck_super)
            {
              log_state = log_state_write_data;
            }
            else
            {
              log_state = log_state_deselect_idle;
              SS_HIGH;
            }
          }
        }
        else
        {
          log_state = log_state_deselect;
          SS_HIGH;
        }

        SPDR = 0xFF;
        break;

      case log_state_deselect_idle:
        /* After deselecting, the Interrupt-loop will stop, and we wait 
         * until log_start is called. By not setting SPDR another interrupt
         * will not be generated. */
        log_state = log_state_idle;
        break;

      case log_state_deselect:
        /* Something broke. Deselect and go back to the beginning */
        log_state = log_state_initreset;
        break;
    }
  }
}

void log_init()
{
  /* Set SS, MOSI and SCK as output; keep MISO as input; pullup MISO */
  DDRB  |= ((_BV(PB4)) | (_BV(PB5)) | (_BV(PB7)));
  PORTB |= ((_BV(PB6)));
  SS_HIGH;

  /* Setup SPI: Interrupts on, SPI on, Master on, MSB first, Speed: f/16 */
  SPCR = ((_BV(SPIE)) | (_BV(SPE)) | (_BV(MSTR)) | (_BV(SPR0)));

  /* The ISR will set SPDR, which will in turn cause another interrupt after
   * that byte is transferred. log_start begins this loop. When it is done
   * it will not touch SPDR, and the loop will stop until log_start is called
   * again */
}

