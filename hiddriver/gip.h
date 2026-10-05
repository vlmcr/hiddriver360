#pragma once
#include <stdint.h>

// GIP (Game Input Protocol) - the vendor specific USB protocol used by
// Xbox One and Xbox Series controllers (and licensed third party pads).
//
// Packet layout (both directions):
//   [0] command
//   [1] options (GIP_OPT_*)
//   [2] sequence number
//   [3] payload length
//   [4..] payload
//
// Layout and init sequence follow the Linux xpad driver and the public
// GIP reverse engineering notes. Everything on the wire is little endian.

#define GIP_INTERFACE_CLASS     0xFF
#define GIP_INTERFACE_SUBCLASS  0x47
#define GIP_INTERFACE_PROTOCOL  0xD0

#define GIP_CMD_ACK             0x01
#define GIP_CMD_ANNOUNCE        0x02
#define GIP_CMD_IDENTIFY        0x04
#define GIP_CMD_POWER           0x05
#define GIP_CMD_AUTHENTICATE    0x06
#define GIP_CMD_VIRTUAL_KEY     0x07   // guide button
#define GIP_CMD_RUMBLE          0x09
#define GIP_CMD_LED             0x0A
#define GIP_CMD_INPUT           0x20

#define GIP_OPT_ACK             0x10
#define GIP_OPT_INTERNAL        0x20

// Input report (GIP_CMD_INPUT) payload, offsets relative to packet start
#define GIP_INPUT_MIN_PAYLOAD   14    // 2 button bytes + 2 triggers + 4 stick axes
#define GIP_INPUT_BUTTONS0      4
#define GIP_INPUT_BUTTONS1      5
#define GIP_INPUT_LT            6     // uint16 LE, 0..1023
#define GIP_INPUT_RT            8
#define GIP_INPUT_LX            10    // int16 LE
#define GIP_INPUT_LY            12
#define GIP_INPUT_RX            14
#define GIP_INPUT_RY            16

// data[4]
#define GIP_BTN0_SYNC           (1 << 0)
#define GIP_BTN0_GUIDE          (1 << 1)   // only set by some third party pads, official pads use GIP_CMD_VIRTUAL_KEY
#define GIP_BTN0_MENU           (1 << 2)   // start
#define GIP_BTN0_VIEW           (1 << 3)   // back
#define GIP_BTN0_A              (1 << 4)
#define GIP_BTN0_B              (1 << 5)
#define GIP_BTN0_X              (1 << 6)
#define GIP_BTN0_Y              (1 << 7)

// data[5]
#define GIP_BTN1_DPAD_UP        (1 << 0)
#define GIP_BTN1_DPAD_DOWN      (1 << 1)
#define GIP_BTN1_DPAD_LEFT      (1 << 2)
#define GIP_BTN1_DPAD_RIGHT     (1 << 3)
#define GIP_BTN1_LB             (1 << 4)
#define GIP_BTN1_RB             (1 << 5)
#define GIP_BTN1_LS             (1 << 6)
#define GIP_BTN1_RS             (1 << 7)

#define GIP_MAX_PACKET          64
#define GIP_OUT_QUEUE_DEPTH     8

// ---- init packets --------------------------------------------------------
// Byte [2] of every init packet is replaced with a running sequence number
// before it is sent, exactly like xpad does.

// Required by every GIP pad: tells the controller to power up and start
// sending input reports.
static const uint8_t gip_power_on[] = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0x00, 0x01, 0x00 };

// Xbox One S (045e:02ea) and Elite Series 2 (045e:0b00) need this too.
static const uint8_t gip_s_init[] = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0x00, 0x0F, 0x06 };

// Elite Series 2 extra input packet init.
static const uint8_t gip_elite2_init[] = { 0x4D, 0x10, 0x01, 0x02, 0x07, 0x00 };

// PDP pads want the LED on and a fake auth response.
static const uint8_t gip_pdp_led_on[] = { GIP_CMD_LED, GIP_OPT_INTERNAL, 0x00, 0x03, 0x00, 0x01, 0x14 };
static const uint8_t gip_pdp_auth[]   = { GIP_CMD_AUTHENTICATE, GIP_OPT_INTERNAL, 0x00, 0x02, 0x01, 0x00 };

// Some Hori / PDP sticks wait for the host to ack their identify packet.
static const uint8_t gip_hori_ack_id[] = {
	GIP_CMD_ACK, GIP_OPT_INTERNAL, 0x00, 0x09,
	0x00, GIP_CMD_IDENTIFY, GIP_OPT_INTERNAL, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

struct GipInitPacket {
	uint16_t vendorId;    // 0 = any vendor
	uint16_t productId;   // 0 = any product of that vendor
	const uint8_t* data;
	uint8_t length;
};

#define GIP_INIT_PKT(vid, pid, pkt) { vid, pid, pkt, sizeof(pkt) }

// Order matters, this mirrors xboxone_init_packets[] in xpad.
static const GipInitPacket gip_init_packets[] = {
	GIP_INIT_PKT(0x0E6F, 0x0165, gip_hori_ack_id),
	GIP_INIT_PKT(0x0F0D, 0x0067, gip_hori_ack_id),
	GIP_INIT_PKT(0x0000, 0x0000, gip_power_on),
	GIP_INIT_PKT(0x045E, 0x02EA, gip_s_init),
	GIP_INIT_PKT(0x045E, 0x0B00, gip_s_init),
	GIP_INIT_PKT(0x045E, 0x0B00, gip_elite2_init),
	GIP_INIT_PKT(0x0E6F, 0x0000, gip_pdp_led_on),
	GIP_INIT_PKT(0x0E6F, 0x0000, gip_pdp_auth),
};

// Ack that must be sent back for every guide button packet that carries
// GIP_OPT_ACK, otherwise the pad keeps re-sending it. Byte [2] is set to the
// sequence number of the packet being acknowledged.
static const uint8_t gip_virtual_key_ack[] = {
	GIP_CMD_ACK, GIP_OPT_INTERNAL, 0x00, 0x09,
	0x00, GIP_CMD_VIRTUAL_KEY, GIP_OPT_INTERNAL, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Rumble packet template. Payload:
//   [4] 0x00  [5] motor mask 0x0F (LT, RT, left, right)
//   [6] LT    [7] RT   [8] left (strong)  [9] right (weak)   all 0..100
//   [10] duration  [11] delay  [12] repeat
static const uint8_t gip_rumble_template[] = {
	GIP_CMD_RUMBLE, 0x00, 0x00, 0x09,
	0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00
};
#define GIP_RUMBLE_LT     6
#define GIP_RUMBLE_RT     7
#define GIP_RUMBLE_LEFT   8
#define GIP_RUMBLE_RIGHT  9

// Set to 0 to disable translating XamInputSetState vibration into GIP rumble.
#ifndef HIDDRIVER_GIP_RUMBLE
#define HIDDRIVER_GIP_RUMBLE 1
#endif
