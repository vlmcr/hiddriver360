# DualSense tilt steering (Gran Turismo style)

Optional motion steering for the DualSense / DualSense Edge over USB. Hold the pad like a
steering wheel: its roll angle drives the left stick X axis. Everything else (LY, triggers,
buttons, the HID mapping) stays as before.

## How to use

1. Plug the pad in over USB. The lightbar turns **blue**.
2. Click the touchpad once: the lightbar turns **red** and tilt steering is on.
3. Rotate the pad left/right like a wheel. 45 degrees is full lock, the first 3 degrees are a
   dead zone, the value is lightly smoothed.
4. Click the touchpad again: lightbar **blue**, the physical left stick is back.

The touchpad click is not mapped to any Xbox button, games never see it.

## Try it on the PC first

`tools/motion_demo/index.html` runs the exact same integer math in the browser over WebHID.
Serve the repository root (`python -m http.server 8000`), open
`http://localhost:8000/tools/motion_demo/` in Chrome or Edge with the pad on a USB cable, connect,
click the touchpad and rotate the pad. The page draws the physical stick and the computed stick,
toggles the lightbar like the driver does, and prints the `#define` values to copy into
`hiddriver/dualsense.h` if you move the sliders.

## Implementation notes

- `hiddriver/dualsense.h`: report offsets, output report layout, tuning constants (`DS_TILT_*`),
  `HIDDRIVER_DS_TILT 0` compiles the feature out, `HIDDRIVER_DS_LIGHTBAR 0` keeps steering but
  never writes to the pad.
- `hiddriver/main.cpp`, section "DualSense specific": touchpad edge detection with a hold-off,
  roll from the accelerometer via an integer square root and an arctan lookup table (pitch
  independent, no FPU in USB callback context), lightbar through the full 63 byte USB output
  report 0x02 on the pad's interrupt OUT endpoint.
- The OUT `UsbTrb` is reverse engineered and the kernel writes past it. It must stay the last
  member of `Controller`, behind the guard bytes; `static_assert`s enforce that. A pointer
  placed after it crashed the running title on every lightbar write.
- Lightbar writes are queued from the input callback with a no-op completion and spaced by
  `DS_LIGHTBAR_MIN_SPACING` input reports; the latest colour wins.
- `tools/ds_host_test` compiles the real driver section on the PC (32 bit MSVC) and checks the
  math, the toggle and the output packets: `python tools/ds_host_test/build_and_run.py`.
