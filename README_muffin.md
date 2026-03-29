[![GitHub license](https://img.shields.io/github/license/Edgetx/edgetx)](https://github.com/EdgeTX/edgetx/blob/main/LICENSE)


<p align="center">
<a href="https://raw.githubusercontent.com/EdgeTX/edgetx.github.io/master/docs/assets/logo.png"><img src="https://raw.githubusercontent.com/EdgeTX/edgetx.github.io/master/docs/assets/logo.png" align="center" height="150" width="150" ></a>

# Welcome to EdgeTX, Project Muffin!
Project Muffin is an effort of porting EdgeTX (https://github.com/EdgeTX/edgetx.git) to ESP32S3 platform.

### Hardware
Project Muffin use customize designed PCB in the case of FlySky FS6 TX.
The PCB design is in https://github.com/JunOllyLi/TX_PCB.git


### Software Build
+ Latest code uses `ESP-IDF v5.3` for the build.
+ To build the software, go to `<root_of_src>/radio/src/targets/muffin/esp32_build` and type `idf.py build`

### Credit
Some of the code used other's open source code directly:
+ ESP32 WiFI ftp-server code came from https://github.com/tmrttmrt/opentx.git
+ Initial EPSNOW code came from https://github.com/tmrttmrt/opentx-espnow-rx.git

