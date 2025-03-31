# What is this repository?

This repository contains files for the custom thermostats used at the Fort
Collins Creator Hub.

# How to clone this repository

This project uses git submodules, unfortunately. To clone the repository, run:

```shell
git clone --recurse-submodules https://github.com/fortcollinscreatorhub/Thermostat
```

If you wish to push back to the github repo, you probably want to clone with
`ssh`, or edit `.git/config` to use ssh URLs after cloning, e.g.:

```shell
git clone --recurse-submodules git@github.com:fortcollinscreatorhub/Thermostat
```

Or after cloning:

```shell
vi .git/config
```

Even if you clone using the ssh URL, you'll still need to edit the remote
definition for each submodule after the initial clone operation.

## Hardware Revisions

v1 was a a breadboard-based design, using some standalone (non-WiFi)
micro-controller; perhaps an AVR or STM32. For this design, we only have the
software and not schematics etc.

v2 uses an ESP32. The PCB design is found in directory [pcb/](pcb/). File
[thermostat.dxf](thermostat.dxf) describes some of the physical design.

## Software Versions

v1 hardware uses the code in directory [code/arduino-v1/](code/arduino-v1/).

v2 hardware can use any of following software stacks:
* A port of the older Arduino code. Found in directory
[code/arduino-esp32/](code/arduino-esp32/).
* WiFi-/MQTT-capable software built using the ESP-IDF framework. Found in
directory [code/esp-idf](code/esp-idf). This version is currently deployed.
* Updated WiFi-/MQTT-capable software built using the ESP-IDF framework, plus
the FCCH "connection manager" component which provides a permanently enabled
password-protected web admin interface. Found in directory
[code/esp-idf-v2](code/esp-idf-v2). This version is not yet deployed.
