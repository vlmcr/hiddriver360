# HidDriver 360
Experimental on-console HID driver focussing on providing third party, non xinput controller support to the xbox 360 without dongles.

## What works
- Supports both 17559 retail and 17489 devkit dashboards
- Reading controller input via USB
- Up to 4 concurrent controllers, in combination with original xbox 360 controllers or without them
- Controllers are registered inside xam and therefore fully recognized in the entire xbox 360 user interface, ring of light will update accordingly, no original controllers are needed etc
- The controller works in all tested games

## Current limitations
- no rumble support for HID controllers (GIP pads do rumble, see below)

## Supported controllers
- DualShock 4 (PS4 controller)
- DualShock 4 Wireless Adapter (CUH-ZWA1E)
- DualSense (PS5 controller)
- DualSense Edge
- DualShock 3
- Nintendo Switch Pro controller
- Any USB HID compliant controller thanks to built in mapping assistant :)
- Xbox One / Xbox Series controllers over USB (GIP, see below) - experimental

## Xbox One / Series (GIP) support
Modern Xbox pads are not HID, they talk Microsoft's vendor specific GIP protocol
(interface class 0xFF, subclass 0x47, protocol 0xD0). This fork adds a second,
independent code path for them next to the HID one:

- detected by interface class, so official pads (Xbox One, One S, Series X|S, Elite 2) and licensed third party GIP pads are picked up
- power on / vendor init packets mirror the Linux `xpad` driver (`gip.h`)
- input report `0x20` is parsed into the same `ButtonsReport` the HID path uses, so XAM registration, ring of light and games behave exactly like for the other pads
- guide button packets (`0x07`) are acknowledged, otherwise the pad keeps re-sending them
- rumble from `XamInputSetState` is translated into GIP `0x09` packets (disable with `HIDDRIVER_GIP_RUMBLE 0`)
- Bluetooth is not covered, the console has no Bluetooth stack; plug the pad in with a USB cable

Tested descriptor of a Series X|S pad (model 1914, `045e:0b12`) is in
`tools/model1914_series_descriptor.txt`, dumped with `tools/usb_descriptor_dump.py`
on Windows. Interface 0 (alt 0) is the gamepad with interrupt OUT `0x02` and
interrupt IN `0x82`, interfaces 1 and 2 are audio and bulk and are ignored.

Host side test: `python tools/gip_host_test/build_and_run.py` compiles the real GIP
section of `main.cpp` (32 bit, with the kernel stubbed) and runs it against model 1914
style packets: init sequence, input report, guide button acks, OUT queue ordering,
rumble and the vendor init table.

Known unknown: the driver hooks the kernel HID class driver's AddDevice. If the
360 USB stack routes vendor class devices somewhere else, the GIP pad never
reaches that hook. The hook now logs the class/subclass/protocol of every device
it sees (`EINTIM: Interface N class ...`), so a debug log tells you right away
whether the pad arrives. If it does not, the match has to move one level up,
to the point where USBD picks a class driver.

## How to use
1. load plugin (either at runtime or at boot via your launch.ini)
2. Connect controllers via USB
3. Controllers with a built in mapping will start working right away, for all other controllers that are USB HID compliant a mapping assistant will be started that guides you through the process of mapping your controller
4. enjoy :)

## How to build
1. Acquire the official Xbox 360 SDK using black magic
2. Install visual studio 2010 ultimate and visual studio 2019
3. Install the sdk using the "FULL" preset
4. Open the solution in visual studio 2019 and build
5. Hopefully enjoy :)

## Showcase
https://github.com/user-attachments/assets/f090e5f4-538d-457f-8189-2c5b98579984


## Attributions
- [EinTim23](https://github.com/EinTim23/) for Reverse engineering the xbox 360s HID and controller implementation and implementing this driver
- [localcc](https://github.com/localcc/) for providing help about low level USB related questions and beaming the idea of doing this into my head
- [Rapidjson](https://github.com/Tencent/rapidjson/) JSON library used to save/load user defined mappings
- [Lufa](https://github.com/abcminiuser/lufa) HID report descriptor parser implementation used in hiddriver360 is derived from their implementation
- [iMoD1998](https://github.com/iMoD1998) Creating the Detours library used in hiddriver360
