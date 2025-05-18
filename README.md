# Zigbee ESP-32 ADC for Home Assistant
Analog ADC ESP-32 inputs to Zigbee for Home Assistant. This is intended for soil moisture sensor monitoring, but can be used for any analog signal via the ESP-32 ADC. This is based on the 'HA_on_off_light' in the ESP-32 examples, and code from [prairiesnpr](https://github.com/prairiesnpr/esp_zha_test_bench/).

Building
---

Follow the setup guide for the ESP-32 SDK (for example for the esp32c6 [here](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/linux-macos-setup.html)), and configure the build environment:

``` bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py erase-flash flash
```

You should be able to see the logging with `idf.py monitor`.

Integration into Home Assistant
----

This currently requires two modifications to work with HA, a ZHA 'quirk' and a ZHA-toolkit automation. The quirk can be found in `quirk.py` and can be installed following the instructions [here](https://community.home-assistant.io/t/how-to-setup-local-zha-quirks/341226/10). This exposes the `AnalogInput` cluster such that Home Assistant can read it.

However this still apparently has to be read manually, which can be circumvented by a [zha-toolkit](https://github.com/mdeweerd/zha-toolkit) automation in `automation.yaml` that polls the value of `present_value` every minute. The values required for this can be found in Devices->Device->Manage Zigbee Device (along with other information that the ESP-32 Zigbee API exposes).
