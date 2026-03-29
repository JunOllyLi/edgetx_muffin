/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "hal/switch_driver.h"
#include "definitions.h"
#include "myeeprom.h"
#include "mcp_pins.h"

#include <stdlib.h>
#include <assert.h>

struct mcp_switch_t
{
    const char*   name;
    uint32_t      bit_high;
    uint32_t      bit_low;

    SwitchHwType type;
    SwitchConfig config;
    uint8_t      flags;
};

#include "mcp23017_switches.inc"

extern uint32_t ShadowInput;  // from mcp23017

void boardInitSwitches() {}

SwitchHwPos boardSwitchGetPosition(uint8_t idx)
{
    const mcp_switch_t *sw = &_switch_defs[idx];
    bool inv = sw->flags & SWITCH_HW_INVERTED;
    SwitchHwPos ret = SWITCH_HW_UP;

    switch (sw->type) {
    case SWITCH_HW_2POS:
        if (0 == (ShadowInput & sw->bit_low))
            ret = SWITCH_HW_DOWN;
        break;

    case SWITCH_HW_3POS: {
        auto hi = (0 == (ShadowInput & sw->bit_high));
        auto lo = (0 == (ShadowInput & sw->bit_low));

        if (!hi && !lo)
            ret = SWITCH_HW_MID;
        else if (!hi && lo)
            ret = SWITCH_HW_DOWN;
        }
        break;
    default:
        break;
    }

    if (inv) {
        return ret == SWITCH_HW_UP     ? SWITCH_HW_DOWN
                : (ret == SWITCH_HW_DOWN ? SWITCH_HW_UP : ret);
    }

    return ret;
}

const char* boardSwitchGetName(uint8_t idx)
{
    return _switch_defs[idx].name;
}

SwitchHwType boardSwitchGetType(uint8_t idx)
{
    return _switch_defs[idx].type;
}

uint8_t boardGetMaxSwitches() { return n_switches; }
uint8_t boardGetMaxFctSwitches() { return n_fct_switches; }

SwitchConfig boardSwitchGetDefaultConfig(uint8_t idx) { return _switch_defs[idx].config; }