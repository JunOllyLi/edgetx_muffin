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

#include "edgetx.h"
#include "getset_helpers.h"
#include "hal/adc_driver.h"
#include "hw_extmodule.h"
#include "hw_inputs.h"
#include "hw_intmodule.h"
#include "hw_serial.h"
#include "numberedit.h"
#include "static.h"

#include "radio_wifi.h"

#if defined(BLUETOOTH)
#include "hw_bluetooth.h"
#endif

#define SET_DIRTY() storageDirty(EE_GENERAL)

static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(2),
                                     LV_GRID_TEMPLATE_LAST};
static const lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                     LV_GRID_TEMPLATE_LAST};

static void onSSIDChanged()
{
  SET_DIRTY();
}

struct SSIDEdit : public RadioTextEdit {
  SSIDEdit(Window *parent, const rect_t &rect) :
      RadioTextEdit(parent, rect, g_eeGeneral.wifi_ssid,
                    sizeof(g_eeGeneral.wifi_ssid))
  {
    setChangeHandler(onSSIDChanged);
  }
};

static void onPasswordChanged()
{
  SET_DIRTY();
}

struct PasswordEdit : public RadioTextEdit {
  PasswordEdit(Window *parent, const rect_t &rect) :
      RadioTextEdit(parent, rect, g_eeGeneral.wifi_password,
                    sizeof(g_eeGeneral.wifi_password))
  {
    setChangeHandler(onPasswordChanged);
  }
};

static uint8_t wifi_en = 0;
static char wifi_status[20];

RadioWiFiPage::RadioWiFiPage(const PageDef& pageDef):
  PageGroupItem(pageDef, PAD_TINY)
{
}

void RadioWiFiPage::checkEvents()
{
  if (wifi_en != isWiFiStarted()) {
    if (wifi_en) {
      startWiFi(g_eeGeneral.wifi_ssid, g_eeGeneral.wifi_password, g_eeGeneral.ftppass);
    } else {
      stopWiFi();
    }
  }
  strncpy(wifi_status, getWiFiStatus(), sizeof(wifi_status));
  wifi_status[sizeof(wifi_status) - 1] = 0; // truncate the string if too big
  status->update();
}

void RadioWiFiPage::build(Window * window)
{
  window->setFlexLayout();
  FlexGridLayout grid(col_dsc, row_dsc);
  lv_obj_set_style_pad_all(window->getLvObj(), lv_dpx(8), 0);

  strcpy(wifi_status, getWiFiStatus());

  auto line = window->newLine(grid);
  new StaticText(line, rect_t{}, "SSID");
  new SSIDEdit(line, rect_t{});

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, "Password");
  new PasswordEdit(line, rect_t{});

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, "WiFi Status");
  status = new RadioTextEdit(line, rect_t{}, wifi_status,
                    sizeof(wifi_status));

  line = window->newLine(grid);
  new StaticText(line, rect_t{}, "WiFi ON");
  auto box = new Window(line, rect_t{});
  box->setFlexLayout(LV_FLEX_FLOW_ROW, lv_dpx(8));
  lv_obj_set_style_grid_cell_x_align(box->getLvObj(), LV_GRID_ALIGN_STRETCH, 0);
  lv_obj_set_style_flex_cross_place(box->getLvObj(), LV_FLEX_ALIGN_CENTER, 0);
  new ToggleSwitch(box, rect_t{}, GET_SET_DEFAULT(wifi_en));
}

void RadioWiFiPage::cleanup()
{
}
