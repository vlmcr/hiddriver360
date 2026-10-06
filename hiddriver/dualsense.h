#pragma once
#include <stdint.h>

// Sony DualSense (PS5) specifics used by the tilt steering mode.
//
// Layout of the USB input report 0x01 and output report 0x02 follows the
// public Linux hid-playstation driver. Offsets below include the report ID
// byte at [0]. Everything on the wire is little endian.

#define DS_PID_DUALSENSE        0x0CE6
#define DS_PID_DUALSENSE_EDGE   0x0DF2

#define DS_INPUT_REPORT_USB     0x01
#define DS_INPUT_REPORT_SIZE    64
#define DS_IN_BUTTONS2          10      // bit0 PS, bit1 touchpad click, bit2 mute
#define DS_BTN2_TOUCHPAD        (1 << 1)
#define DS_IN_ACCEL_X           22      // int16 LE, about 8192 units per g
#define DS_IN_ACCEL_Y           24
#define DS_IN_ACCEL_Z           26

#define DS_OUTPUT_REPORT_USB    0x02
#define DS_OUTPUT_REPORT_SIZE   48
#define DS_OUT_VALID_FLAG1      2
#define DS_FLAG1_LIGHTBAR_CONTROL       0x04
#define DS_OUT_VALID_FLAG2      39
#define DS_FLAG2_LIGHTBAR_SETUP_CONTROL 0x02
#define DS_OUT_LIGHTBAR_SETUP   42
#define DS_LIGHTBAR_SETUP_LIGHT_OUT     0x02   // cancels the firmware blue fade-in so colours apply at once
#define DS_OUT_LIGHTBAR_R       45
#define DS_OUT_LIGHTBAR_G       46
#define DS_OUT_LIGHTBAR_B       47

// ---- tilt steering tuning (angles in tenths of a degree, integer only) ----
#define DS_TILT_MAX_DEG10       450     // full stick deflection at this roll angle
#define DS_TILT_DEADZONE_DEG10  30      // ignore tilt below this
#define DS_TILT_FILTER_SHIFT    2       // exponential smoothing, new sample weight 1/4
#define DS_TILT_INVERT          0       // 0 = negate roll (left tilt -> stick left), 1 = opposite
#define DS_TILT_TOGGLE_HOLDOFF  25      // reports to ignore after a toggle (~100 ms), masks switch bounce
#define DS_TILT_START_DELAY     500     // reports (~2 s) between switching on and applying steering
#define DS_LIGHTBAR_MIN_SPACING 50      // input reports between two lightbar writes (~200 ms)

#define DS_LIGHTBAR_ON_R  255
#define DS_LIGHTBAR_ON_G  0
#define DS_LIGHTBAR_ON_B  0
#define DS_LIGHTBAR_OFF_R 0
#define DS_LIGHTBAR_OFF_G 0
#define DS_LIGHTBAR_OFF_B 255

// atan(i / 64) for i = 0..64, in tenths of a degree. Used by DsAtan2Deg10.
static const uint16_t ds_atan_table[65] = {
	0,9,18,27,36,45,54,62,71,80,89,98,106,115,123,132,140,149,157,165,174,182,190,198,
	206,213,221,229,236,244,251,258,266,273,280,287,294,300,307,314,320,326,333,339,345,
	351,357,363,369,374,380,386,391,396,402,407,412,417,422,427,432,436,441,445,450
};

#ifdef __cplusplus
static_assert(DS_TILT_MAX_DEG10 > DS_TILT_DEADZONE_DEG10, "full lock angle must exceed the dead zone");
#endif

// Set to 0 to keep tilt steering but never touch the lightbar (diagnostics).
#ifndef HIDDRIVER_DS_LIGHTBAR
#define HIDDRIVER_DS_LIGHTBAR 1
#endif

// Set to 0 to compile without the tilt steering mode.
#ifndef HIDDRIVER_DS_TILT
#define HIDDRIVER_DS_TILT 1
#endif
