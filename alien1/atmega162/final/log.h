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

#ifndef ALIEN_LOG_HEADER
#define ALIEN_LOG_HEADER

#define log_state_initreset        0    /* Init - 80 clocks */
#define log_state_reset            1    /* Send reset - CMD0, check */
#define log_state_getocr           2    /* Check voltage info - CMD8 */
#define log_state_readywait        3    /* Send CMD1 until it's ready */
#define log_state_setblklen        4    /* Send SET BLOCKLEN command (16) */
#define log_state_readsuper_r      5    /* Read Superblock - Response */
#define log_state_readsuper_s      6    /* Read Superblock - Data Token */
#define log_state_readsuper_d      7    /* Read Superblock - Data! */
#define log_state_idle             8    /* Waiting for data, write CMD24 */
#define log_state_writing_super    9    /* Writing superblock */
#define log_state_writewait_super  10   /* Waiting for write finish */
#define log_state_writecheck_super 11   /* CMD13: Check status */
#define log_state_write_data       12   /* Write data CMD24 */
#define log_state_writing_data     13   /* Writing data */
#define log_state_writewait_data   14   /* Waiting for write finish */
#define log_state_writecheck_data  15   /* CMD13: Check status */

#define log_state_deselect_idle    17   /* Wind down, end loop, goto idle */
#define log_state_deselect         18   /* Wind down, end loop, goto 0 */

#define log_timeout_max          250    /* Don't hang around */
#define log_timeout_write_max    4000 

extern uint8_t log_state;

#define log_start()  SPDR = 0xFF
void log_init();

#endif 
