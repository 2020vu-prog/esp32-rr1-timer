# RR1 Timer Interrupt Integration Test

Minimal ESP32-C5 ESP-IDF project that configures a GPTimer alarm interrupt every
10 ms. The ISR increments a counter and notifies the main task every 100
interrupts.

Build with an active ESP-IDF v5.5.4 environment:

```sh
idf.py -B build_verify set-target esp32c5
idf.py -B build_verify build
```

Verified locally with:

```sh
PATH=/Users/chriswitte/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$PATH \
IDF_TOOLS_PATH=/Users/chriswitte/.espressif/tools \
IDF_PYTHON_ENV_PATH=/Users/chriswitte/.espressif/tools/python/v5.5.4/venv \
/Users/chriswitte/.espressif/tools/python/v5.5.4/venv/bin/python \
/Users/chriswitte/.espressif/v5.5.4/esp-idf/tools/idf.py -B build_verify build
```

* gpio0 lane 1
* gpio1 lane 2
* gpio pin high when lane clear, low when blocked
* simulate a finish once every 40 seconds, then 80 seconds, then back to 40, then 80, etc
* gpio blocked for 150 ms to simulate a car
* lane 1 and lane 2 winner and time chosen at randoom b/t 0-4000ms
* activate user led during finish simulation:: RR1_PIN_SEEED_C5_ONBOARD_LED 27
