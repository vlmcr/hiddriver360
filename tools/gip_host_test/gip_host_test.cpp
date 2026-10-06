// Host side test for the GIP code path of hiddriver360.
//
// build_and_run.py extracts the real GIP section of hiddriver/main.cpp
// (between the "GIP ... specific start" / "GIP specific end" markers) into
// gip_section.inc and compiles it together with this file. Everything the
// section needs from the Xbox kernel is stubbed below, so the parser, the OUT
// queue, the ack / init / rumble logic and the SET_CONFIGURATION callback are
// exercised exactly as they will run on the console.

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// On the console DWORD is 32 bit and holds pointers; keep that property on a 64 bit host.
typedef uintptr_t DWORD;
typedef uint8_t  BYTE;
typedef int32_t  LONG;
typedef int32_t  NTSTATUS;
#define NT_ERROR(s) ((uint32_t)(s) >> 30 == 3)

#include "../../hiddriver/usb.h"
#include "../../hiddriver/gip.h"

// ---- kernel stubs ----------------------------------------------------------

static std::string g_log;
static int DbgPrint(const char* fmt, ...) {
	char buf[512];
	va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
	g_log += buf;
	fputs(buf, stdout);
	return 0;
}

static LONG InterlockedCompareExchange(volatile LONG* dst, LONG exch, LONG cmp) {
	LONG old = *dst; if (old == cmp) *dst = exch; return old;
}
static LONG InterlockedExchange(volatile LONG* dst, LONG val) { LONG old = *dst; *dst = val; return old; }

static uint16_t swap_endianness_16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

#define USB_ENDPOINT_TYPE_INTERRUPT 0x03
#define USB_DIRECTION_IN  1
#define USB_DIRECTION_OUT 0

struct UsbTrb {
	DWORD endpoint; DWORD callback; DWORD savedEndpoint; BYTE padding[4];
	BYTE flags; BYTE controllerIndex; BYTE pad2; BYTE endpointIndex;
	void* buffer; DWORD length;
};
struct UsbPacket { BYTE bmRequestType; BYTE bRequest; uint16_t wValue; uint16_t wIndex; uint16_t wLength; };
struct UsbControlTrb { UsbTrb trb; BYTE pad[4]; UsbPacket packet; };

struct deviceHandle;
struct HidControllerExtension {
	deviceHandle* deviceHandle;
	UsbTrb interruptTrb;
	BYTE interfaceNumber; BYTE gap20[3];
	UsbControlTrb controlTrb;
	BYTE cleanupDone;
};
struct deviceHandle { HidControllerExtension* driver; };

// Only the Controller fields the GIP section touches. Names must match main.cpp.
struct Controller {
	deviceHandle* deviceHandle;
	HidControllerExtension* controllerDriver;
	ButtonsReport currentState;
	uint8_t userIndex;
	uint32_t deviceContext;
	uint16_t vendorId;
	uint16_t productId;
	uint32_t packetNumber;
	void* reportData;

	bool isGip;
	uint16_t gipInPacketSize;
	UsbTrb gipOutTrb;
	uint8_t* gipOutBuffer;
	volatile LONG gipOutBusy;
	uint8_t gipOutSerial;
	uint8_t gipOutHead;
	uint8_t gipOutCount;
	struct { uint8_t data[GIP_MAX_PACKET]; uint8_t length; } gipOutQueue[GIP_OUT_QUEUE_DEPTH];
	volatile LONG gipRumblePending;
	uint8_t gipRumble[2];
};

static Controller connectedControllers[4];
static Controller c;
static int globalIndex = -1;

// Every OUT packet the driver sends ends up here, in order.
struct SentPacket { std::vector<uint8_t> data; UsbTrb* trb; DWORD callback; };
static std::vector<SentPacket> g_sent;
static std::vector<UsbTrb*> g_queuedIn;

static void SendInterruptRequest(deviceHandle*, UsbTrb* trb, void* data, uint32_t length, DWORD callback) {
	trb->buffer = data; trb->length = length; trb->callback = callback;
	SentPacket p; p.data.assign((uint8_t*)data, (uint8_t*)data + length); p.trb = trb; p.callback = callback;
	g_sent.push_back(p);
}
static int interruptHandler(DWORD, int32_t) { return 0; }
static int UsbdQueueAsyncTransfer(deviceHandle*, void* trb) { g_queuedIn.push_back((UsbTrb*)trb); return 0; }

// Endpoint descriptors exactly as dumped from the model 1914 pad (tools/model1914_series_descriptor.txt)
static usb_endpoint_descriptor g_epOut = { 7, 5, 0x02, 0x03, swap_endianness_16(64), 4 };
static usb_endpoint_descriptor g_epIn  = { 7, 5, 0x82, 0x03, swap_endianness_16(64), 4 };
static usb_endpoint_descriptor* UsbdGetEndpointDescriptor(deviceHandle*, int, int type, int direction) {
	if (type != USB_ENDPOINT_TYPE_INTERRUPT) return NULL;
	return direction == USB_DIRECTION_IN ? &g_epIn : &g_epOut;
}
static std::vector<std::pair<int, DWORD*> > g_openedEndpoints;
static NTSTATUS UsbdOpenEndpoint(deviceHandle*, int, int address, int maxPacket, int interval, DWORD* endpoint) {
	*endpoint = 0x1000 + address;
	g_openedEndpoints.push_back(std::make_pair(address, endpoint));
	printf("  [stub] UsbdOpenEndpoint addr=0x%02x maxPacket=%d interval=%d\n", address, maxPacket, interval);
	return 0;
}
static int XamUserBindDeviceCallback(unsigned int, unsigned int, uint8_t, bool, uint8_t* userIndex) {
	if (userIndex)
		*userIndex = 0;
	return 0;
}

// diagnostics counters used by the GIP section on the console
static volatile LONG g_statGipReady = 0;
#define HIDLOG_COUNT(counter) ((void)0)

#include "gip_section.inc"

// ---- test helpers ----------------------------------------------------------

static int g_failures = 0;
#define CHECK(cond, msg) do { if (cond) { printf("  ok   %s\n", msg); } else { printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } } while (0)

static void completeOut() {
	// simulate the kernel finishing the OUT transfer that is in flight
	SentPacket& p = g_sent.back();
	((int32_t(*)(DWORD, int32_t))(uintptr_t)p.callback)((DWORD)(uintptr_t)p.trb, 0);
}

static std::string hex(const std::vector<uint8_t>& v) {
	std::string s; char b[4];
	for (size_t i = 0; i < v.size(); i++) { snprintf(b, sizeof(b), "%02x ", v[i]); s += b; }
	return s;
}

int main() {
	// ---- 1. SET_CONFIGURATION completion on a model 1914 pad ----
	printf("== init path (045e:0b12, model 1914) ==\n");
	deviceHandle dev; HidControllerExtension ext; memset(&ext, 0, sizeof(ext));
	dev.driver = &ext; ext.deviceHandle = &dev;
	memset(&c, 0, sizeof(c));
	c.isGip = true; c.vendorId = 0x045E; c.productId = 0x0B12; c.deviceHandle = &dev;
	globalIndex = 0;

	int32_t st = gipSetConfigurationComplete((DWORD)(uintptr_t)&ext.controlTrb, 0);
	CHECK(st == 0, "gipSetConfigurationComplete returns success");
	CHECK((HidControllerExtension*)((BYTE*)&ext.controlTrb - 36) == &ext, "controlTrb sits 36 bytes into the extension (callback arithmetic)");
	CHECK(g_openedEndpoints.size() == 2, "both interrupt endpoints opened");
	CHECK(g_openedEndpoints[0].first == 0x82 && g_openedEndpoints[0].second == (DWORD*)&ext.interruptTrb, "IN 0x82 opened on the extension's interruptTrb");
	CHECK(g_openedEndpoints[1].first == 0x02 && g_openedEndpoints[1].second == (DWORD*)&connectedControllers[0].gipOutTrb, "OUT 0x02 opened on the controller slot's gipOutTrb");
	CHECK(connectedControllers[0].isGip && connectedControllers[0].controllerDriver == &ext, "controller registered in slot 0");
	CHECK(connectedControllers[0].gipInPacketSize == 64, "IN packet size 64");
	CHECK(ext.interruptTrb.length == 64 && ext.interruptTrb.buffer == connectedControllers[0].reportData, "IN trb points at report buffer");
	CHECK(g_queuedIn.size() == 1 && g_queuedIn[0] == &ext.interruptTrb, "first IN transfer queued");

	// For a Series pad only the generic power on packet applies
	CHECK(g_sent.size() == 1, "exactly one OUT packet in flight after init");
	std::vector<uint8_t> expectPowerOn; expectPowerOn.push_back(0x05); expectPowerOn.push_back(0x20); expectPowerOn.push_back(0x00); expectPowerOn.push_back(0x01); expectPowerOn.push_back(0x00);
	CHECK(g_sent[0].data == expectPowerOn, "power on packet 05 20 00 01 00 with sequence 0");
	CHECK(connectedControllers[0].gipOutBusy == 1 && connectedControllers[0].gipOutCount == 0, "OUT endpoint busy, queue drained");
	completeOut();
	CHECK(connectedControllers[0].gipOutBusy == 0 && g_sent.size() == 1, "completion frees endpoint, nothing else sent");

	// ---- 2. Input report ----
	printf("== input report 0x20 ==\n");
	uint8_t* in = (uint8_t*)connectedControllers[0].reportData;
	memset(in, 0, 64);
	// Layout from the GIP spec / xpad: A + DPAD_LEFT + RB held, LT 25%, RT full,
	// LX = +32767 (right), LY = -32768 (down), RX = -1000, RY = +1000.
	// Series pads send payload length 0x1a with extra bytes after the sticks.
	const uint8_t report[] = {
		0x20, 0x00, 0x11, 0x1a,
		GIP_BTN0_A | GIP_BTN0_MENU,                 // [4]
		GIP_BTN1_DPAD_LEFT | GIP_BTN1_RB | GIP_BTN1_LS, // [5]
		0x00, 0x01,                                  // LT = 0x0100 = 256 / 1023
		0xFF, 0x03,                                  // RT = 1023
		0xFF, 0x7F,                                  // LX = 32767
		0x00, 0x80,                                  // LY = -32768
		0x18, 0xFC,                                  // RX = -1000
		0xE8, 0x03,                                  // RY = 1000
		0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // Series extras ([22] bit0 = share)
	};
	memcpy(in, report, sizeof(report));
	GipProcessPacket(0, in, 64);
	ButtonsReport& s = connectedControllers[0].currentState;
	CHECK(s.a_button == 1 && s.b_button == 0 && s.x_button == 0 && s.y_button == 0, "A pressed only");
	CHECK(s.start == 1 && s.back == 0, "menu -> start, view not pressed");
	CHECK(s.dpad_left == 1 && s.dpad_right == 0 && s.dpad_up == 0 && s.dpad_down == 0 && !s.has_hat_switch, "dpad left as discrete bits");
	CHECK(s.r1 == 1 && s.l1 == 0 && s.l3 == 1 && s.r3 == 0, "RB and LS pressed");
	CHECK(s.rx == 64 && s.ry == 255, "triggers scaled 10 bit -> 8 bit (256>>2 = 64, 1023>>2 = 255)");
	CHECK(s.l2 == 0 && s.r2 == 0, "digital trigger bits untouched (analog value wins in XInputdReadStateHook)");
	CHECK(s.x == 32767 && s.y == -32768, "left stick little endian int16 passed through");
	CHECK(s.z == -1000 && s.rz == 1000, "right stick -> z / rz");
	CHECK(s.xbox == 0, "guide not pressed");

	// Short / truncated input packet must be ignored, state kept
	uint8_t shortPkt[] = { 0x20, 0x00, 0x12, 0x04, 0xFF, 0xFF, 0xFF, 0xFF };
	memset(in, 0, 64); memcpy(in, shortPkt, sizeof(shortPkt));
	GipProcessPacket(0, in, 64);
	CHECK(s.a_button == 1 && s.x == 32767, "input packet with too small payload ignored");

	// ---- 3. Guide button packet with ack request ----
	printf("== guide button 0x07 ==\n");
	uint8_t guideDown[] = { 0x07, 0x30, 0x2A, 0x02, 0x01, 0x5B };
	memset(in, 0, 64); memcpy(in, guideDown, sizeof(guideDown));
	GipProcessPacket(0, in, 64);
	CHECK(s.xbox == 1, "guide press recorded");
	CHECK(g_sent.size() == 2, "ack sent immediately (endpoint was idle)");
	const uint8_t expectAck[] = { 0x01, 0x20, 0x2A, 0x09, 0x00, 0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
	CHECK(g_sent[1].data == std::vector<uint8_t>(expectAck, expectAck + sizeof(expectAck)), "ack echoes the pad's sequence number 0x2a");
	completeOut();

	uint8_t guideUp[] = { 0x07, 0x30, 0x2B, 0x02, 0x00, 0x5B };
	memset(in, 0, 64); memcpy(in, guideUp, sizeof(guideUp));
	GipProcessPacket(0, in, 64);
	CHECK(s.xbox == 0, "guide release recorded");
	CHECK(g_sent.size() == 3 && g_sent[2].data[2] == 0x2B, "release acked with its own sequence number");
	completeOut();

	// Guide packet without ack flag must not be acked
	uint8_t guideNoAck[] = { 0x07, 0x20, 0x2C, 0x02, 0x01, 0x5B };
	memset(in, 0, 64); memcpy(in, guideNoAck, sizeof(guideNoAck));
	GipProcessPacket(0, in, 64);
	CHECK(g_sent.size() == 3, "no ack when GIP_OPT_ACK is clear");

	// Input report must not clobber the guide state
	memset(in, 0, 64); memcpy(in, report, sizeof(report));
	GipProcessPacket(0, in, 64);
	CHECK(s.xbox == 1, "guide state survives following input reports");

	// ---- 4. Queue ordering while the endpoint is busy ----
	printf("== OUT queue ==\n");
	size_t before = g_sent.size();
	memset(in, 0, 64); memcpy(in, guideDown, sizeof(guideDown)); in[2] = 0x40;
	GipProcessPacket(0, in, 64);              // ack 0x40 goes out, endpoint busy
	in[2] = 0x41; GipProcessPacket(0, in, 64); // ack 0x41 queued
	in[2] = 0x42; GipProcessPacket(0, in, 64); // ack 0x42 queued
	GipSetRumble(0, 100, 50);                   // rumble pending
	CHECK(g_sent.size() == before + 1, "only one transfer in flight while busy");
	CHECK(connectedControllers[0].gipOutCount == 2 && connectedControllers[0].gipRumblePending == 1, "two acks queued, rumble pending");
	completeOut();
	CHECK(g_sent.size() == before + 2 && g_sent.back().data[2] == 0x41, "second ack sent on completion");
	completeOut();
	CHECK(g_sent.size() == before + 3 && g_sent.back().data[2] == 0x42, "third ack sent on completion");
	completeOut();
	CHECK(g_sent.size() == before + 4 && g_sent.back().data[0] == 0x09, "rumble sent after queued packets");
	{
		const std::vector<uint8_t>& r = g_sent.back().data;
		CHECK(r.size() == 13 && r[3] == 0x09 && r[5] == 0x0F, "rumble packet length 13, payload 9, motor mask 0x0f");
		CHECK(r[8] == 100 && r[9] == 50 && r[10] == 0xFF, "rumble left 100 / right 50 / duration 0xff");
		CHECK(r[2] == 1, "rumble uses next host sequence number (power on was 0)");
	}
	completeOut();
	CHECK(connectedControllers[0].gipOutBusy == 0 && connectedControllers[0].gipOutCount == 0 && connectedControllers[0].gipRumblePending == 0, "queue idle");

	// latest rumble wins when several arrive while busy
	GipSetRumble(0, 10, 10);                    // sent immediately
	GipSetRumble(0, 20, 20);
	GipSetRumble(0, 30, 30);                    // overrides 20
	completeOut();
	CHECK(g_sent.back().data[8] == 30 && g_sent.back().data[9] == 30, "latest rumble value wins");
	completeOut();

	// ---- 5. Announce re-inits ----
	printf("== announce 0x02 ==\n");
	before = g_sent.size();
	uint8_t announce[] = { 0x02, 0x20, 0x00, 0x1C };
	memset(in, 0, 64); memcpy(in, announce, sizeof(announce));
	GipProcessPacket(0, in, 64);
	CHECK(g_sent.size() == before + 1 && g_sent.back().data[0] == 0x05 && g_sent.back().data[3] == 0x01, "announce triggers power on again");
	completeOut();

	// ---- 6. Vendor table for other GIP pads ----
	printf("== init table ==\n");
	g_sent.clear();
	connectedControllers[0].vendorId = 0x045E; connectedControllers[0].productId = 0x0B00; // Elite 2
	GipQueueInitPackets(0); completeOut(); completeOut(); completeOut();
	CHECK(g_sent.size() == 3 && g_sent[0].data[0] == 0x05 && g_sent[1].data[4] == 0x06 && g_sent[2].data[0] == 0x4D, "Elite 2: power on, S init, extra input init");
	CHECK(g_sent[0].data[2] + 1 == g_sent[1].data[2] && g_sent[1].data[2] + 1 == g_sent[2].data[2], "sequence numbers increase per packet");
	g_sent.clear();
	connectedControllers[0].vendorId = 0x0E6F; connectedControllers[0].productId = 0x02A4; // PDP
	GipQueueInitPackets(0); completeOut(); completeOut(); completeOut();
	CHECK(g_sent.size() == 3 && g_sent[0].data[0] == 0x05 && g_sent[1].data[0] == 0x0A && g_sent[2].data[0] == 0x06, "PDP: power on, led, auth");

	printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
	return g_failures ? 1 : 0;
}
