/*
This file is part of the Gamebuino-AKA library,
Copyright (c) Gamebuino 2026

This is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License (LGPL)
as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License (LGPL) for more details.

You should have received a copy of the GNU Lesser General Public
License (LGPL) along with the library.
If not, see <http://www.gnu.org/licenses/>.

Authors:
 - Jean-Marie Papillon
*/
#ifdef __cplusplus
extern "C" {
#endif
#include "freertos/FreeRTOS.h"

    // nitialize timers
void gb_ll_system_init();

    //! called by gb_ll in "c"
void gb_delay_ms(uint32_t u32_ms);

    //! called by gb_ll in "c"
IRAM_ATTR uint32_t gb_get_millis();

    //! delay in microseconds
void gb_delay_us(int64_t u32_us);

    //! timer in microseconds
IRAM_ATTR int64_t gb_get_micros();

#ifdef __cplusplus
}
#endif