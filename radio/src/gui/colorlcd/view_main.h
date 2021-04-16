/*
 * Copyright (C) OpenTX
 *
 * Based on code named
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

#ifndef _VIEW_MAIN_H_
#define _VIEW_MAIN_H_

#include "form.h"
#include <memory>

class TopBar;
class SetupWidgetsPage;

class ViewMain: public Window
{
    // singleton
    explicit ViewMain();

  public:
    ~ViewMain() override;

    static ViewMain * instance()
    {
      if (!_instance)
        _instance = new ViewMain();

      return _instance;
    }

#if defined(DEBUG_WINDOWS)
    std::string getName() const override
    {
      return "ViewMain";
    }
#endif

    // Set topbar visibility
    void setTopbarVisible(bool visible);

    // Get the available space in the middle of the screen
    // (without topbar)
    rect_t getMainZone(rect_t zone) const;

    unsigned getMainViewsCount() const;
    void setMainViewsCount(unsigned views);

    coord_t getMainViewLeftPos(unsigned view) const;
  
    unsigned getCurrentMainView() const;
    void setCurrentMainView(unsigned view);
    void nextMainView();
    void previousMainView();

  protected:
    static ViewMain * _instance;

    unsigned views = 0;
    TopBar*  topbar = nullptr;

    friend class SetupWidgetsPage;

    void setScrollPositionX(coord_t value) override;
    void setScrollPositionY(coord_t value) override;

#if defined(HARDWARE_KEYS)
    void onEvent(event_t event) override;
#endif
    void paint(BitmapBuffer * dc) override;

    void openMenu();
    void createTopbar();
};

#endif // _VIEW_MAIN_H_
