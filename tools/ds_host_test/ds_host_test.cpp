// Host side test for the DualSense tilt steering section of hiddriver360.
// build_and_run.py extracts the real section from hiddriver/main.cpp into
// ds_section.inc; the kernel is stubbed here.
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

typedef uintptr_t DWORD;
typedef uint8_t  BYTE;
typedef int32_t  LONG;
typedef int32_t  NTSTATUS;
#define NT_ERROR(s) ((uint32_t)(s) >> 30 == 3)

#include "../../hiddriver/usb.h"
#include "../../hiddriver/dualsense.h"

static int DbgPrint(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); return 0; }
static LONG InterlockedCompareExchange(volatile LONG* d, LONG e, LONG c) { LONG o = *d; if (o == c) *d = e; return o; }
static LONG InterlockedExchange(volatile LONG* d, LONG v) { LONG o = *d; *d = v; return o; }
static uint16_t swap_endianness_16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
#define USB_ENDPOINT_TYPE_INTERRUPT 0x03
#define USB_DIRECTION_IN  1
#define USB_DIRECTION_OUT 0
const uint16_t SONY_VENDOR_ID = 0x054C;

struct UsbTrb { DWORD endpoint; DWORD callback; DWORD savedEndpoint; BYTE padding[4]; BYTE flags; BYTE controllerIndex; BYTE pad2; BYTE endpointIndex; void* buffer; DWORD length; };
struct deviceHandle; struct HidControllerExtension { int dummy; };
struct deviceHandle { HidControllerExtension* driver; };

struct Controller {
	deviceHandle* deviceHandle;
	HidControllerExtension* controllerDriver;
	ButtonsReport currentState;
	uint16_t vendorId, productId;
	bool dsTiltSteering; uint8_t dsTouchpadPrev; uint16_t dsToggleHoldoff; uint16_t dsSteerDelay; uint16_t dsTraceCounter; int32_t dsSteerFiltered;
	UsbTrb dsOutTrb; uint8_t* dsOutBuffer; volatile LONG dsOutBusy; volatile LONG dsLightbarPending;
	uint8_t dsLightbar[3]; bool dsLightbarSetupSent;
};
static Controller connectedControllers[4];

struct Sent { std::vector<uint8_t> data; UsbTrb* trb; DWORD cb; };
static std::vector<Sent> g_sent;
static void SendInterruptRequest(deviceHandle*, UsbTrb* trb, void* data, uint32_t len, DWORD cb) {
	Sent s; s.data.assign((uint8_t*)data, (uint8_t*)data + len); s.trb = trb; s.cb = cb; g_sent.push_back(s);
}
static usb_endpoint_descriptor g_epOut = { 7, 5, 0x02, 0x03, swap_endianness_16(64), 4 };
static usb_endpoint_descriptor* UsbdGetEndpointDescriptor(deviceHandle*, int, int, int dir) { return dir == USB_DIRECTION_OUT ? &g_epOut : NULL; }
static NTSTATUS UsbdOpenEndpoint(deviceHandle*, int, int, int, int, DWORD* ep) { *ep = 0x1002; return 0; }

#include "ds_section.inc"

static int g_fail = 0;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FAIL %s (line %d)\n", m, __LINE__); g_fail++; } } while (0)
static void completeOut() { Sent& s = g_sent.back(); ((int32_t(*)(DWORD, int32_t))(uintptr_t)s.cb)((DWORD)(uintptr_t)s.trb, 0); }

static void report(uint8_t* r, bool touch, int16_t ax, int16_t ay, int16_t az) {
	memset(r, 0, 64); r[0] = 0x01; r[1] = 128; r[2] = 128;
	if (touch) r[DS_IN_BUTTONS2] |= DS_BTN2_TOUCHPAD;
	r[22] = ax & 0xFF; r[23] = (ax >> 8) & 0xFF; r[24] = ay & 0xFF; r[25] = (ay >> 8) & 0xFF; r[26] = az & 0xFF; r[27] = (az >> 8) & 0xFF;
}
static int16_t settle(int idx, uint8_t* r, int n) { ButtonsReport b; for (int i = 0; i < n; i++) { memset(&b, 0, sizeof b); DsProcessInputReport(idx, r, &b); } return b.x; }
static void skipDelay(int idx, uint8_t* r) { connectedControllers[idx].dsSteerDelay = 0; (void)r; }

int main() {
	deviceHandle dev; HidControllerExtension ext; dev.driver = &ext;
	Controller& c = connectedControllers[0]; memset(&c, 0, sizeof c);
	c.deviceHandle = &dev; c.controllerDriver = &ext; c.vendorId = 0x054C; c.productId = 0x0CE6;
	uint8_t r[64];

	printf("== math ==\n");
	CHECK(DsIsqrt32(8192u * 8192u) == 8192, "isqrt exact square");
	CHECK(DsAtan2Deg10(0, 8192) == 0, "atan2 flat = 0");
	CHECK(DsAtan2Deg10(8192, 8192) == 450, "atan2 45 degrees");
	CHECK(DsAtan2Deg10(-8192, 8192) == -450, "atan2 -45 degrees");
	CHECK(DsAtan2Deg10(8192, 0) == 900, "atan2 90 degrees");
	{ Controller t; memset(&t, 0, sizeof t); DsRollToSteer(t, -32768, -32768, -32768); CHECK(true, "extreme accel does not crash (64 bit square sum)"); }

	printf("== OUT endpoint + idle colour ==\n");
	CHECK(DsOpenOutEndpoint(0) == 0 && c.dsOutBuffer != NULL, "OUT endpoint opened, buffer allocated");
	DsSetLightbar(0, DS_LIGHTBAR_OFF_R, DS_LIGHTBAR_OFF_G, DS_LIGHTBAR_OFF_B);
	CHECK(g_sent.empty() && c.dsLightbarPending == 1, "colour change only marks pending (thread sends)");
	DsPumpOut(0);
	CHECK(g_sent.size() == 1 && g_sent[0].data.size() == 48, "one 48 byte report sent");
	{ std::vector<uint8_t>& d = g_sent[0].data;
	  CHECK(d[0] == 0x02 && d[2] == 0x04, "report 0x02 with lightbar control flag");
	  CHECK(d[39] == 0x02 && d[42] == 0x02, "first report carries LIGHT_OUT setup");
	  CHECK(d[45] == 0 && d[46] == 0 && d[47] == 255, "idle colour blue"); }
	completeOut();

	printf("== toggle + steering ==\n");
	report(r, false, 0, 8192, 0);
	CHECK(settle(0, r, 3) == 0 && !c.dsTiltSteering, "flat, mode off: LX untouched (0)");
	report(r, true, 0, 8192, 0); settle(0, r, 1);
	CHECK(c.dsTiltSteering, "touchpad press toggles mode on");
	CHECK(g_sent.size() == 1, "nothing sent from the input callback itself");
	DsPumpOut(0);
	CHECK(g_sent.size() == 2 && g_sent[1].data[45] == 255 && g_sent[1].data[46] == 0 && g_sent[1].data[47] == 0, "lightbar red sent");
	CHECK(g_sent[1].data[39] == 0, "second report has no setup bytes");
	completeOut();
	settle(0, r, 5);
	CHECK(c.dsTiltSteering, "holding the touchpad does not re-toggle");
	report(r, false, 0, 8192, 0); settle(0, r, 1);
	report(r, true, 0, 8192, 0); settle(0, r, 1);
	CHECK(c.dsTiltSteering, "second press inside the hold-off window is ignored (bounce)");
	report(r, false, 0, 8192, 0); settle(0, r, 30);

	skipDelay(0, r);
	report(r, false, 5793, 5793, 0);               // 45 degrees, tilt so ax is positive
	int16_t lx = settle(0, r, 40);
	CHECK(lx == -32767, "45 degree roll with positive ax = full stick LEFT (-32767)");
	report(r, false, -5793, 5793, 0);
	lx = settle(0, r, 40);
	CHECK(lx == 32767, "opposite roll = full stick RIGHT (+32767)");
	report(r, false, 400, 8192, 0);
	lx = settle(0, r, 40);
	CHECK(lx == 0, "2.8 degrees is inside the dead zone");
	report(r, false, 8192, 0, 8192);               // 45 degree roll while pitched 90 degrees toward the player
	lx = settle(0, r, 40);
	CHECK(lx == -32767, "roll independent of pitch (horizontal from ay and az)");
	report(r, false, 2000, 8192, 0);
	lx = settle(0, r, 40);
	CHECK(lx < -5000 && lx > -20000, "13.7 degrees gives a partial deflection");

	printf("== toggle off restores physical stick ==\n");
	report(r, true, 5793, 5793, 0); settle(0, r, 1);
	CHECK(!c.dsTiltSteering, "touchpad press toggles mode off");
	DsPumpOut(0);
	CHECK(g_sent.size() == 3 && g_sent[2].data[47] == 255 && g_sent[2].data[45] == 0, "lightbar blue sent");
	report(r, false, 5793, 5793, 0);
	{ ButtonsReport b; memset(&b, 0, sizeof b); b.x = 1234; settle(0, r, 30); DsProcessInputReport(0, r, &b); CHECK(b.x == 1234, "mode off: LX from HID mapping preserved"); }

	printf("== busy endpoint: latest colour wins ==\n");
	DsSetLightbar(0, 1, 2, 3); DsPumpOut(0);         // report 3 still in flight -> stays pending
	CHECK(g_sent.size() == 3 && c.dsLightbarPending == 1, "while busy nothing new is sent, colour pending");
	DsSetLightbar(0, 9, 9, 9);
	completeOut();
	CHECK(g_sent.size() == 3, "completion itself never sends");
	DsPumpOut(0);
	CHECK(g_sent.size() == 4 && g_sent[3].data[45] == 9 && g_sent[3].data[47] == 9, "next pump sends the latest pending colour once");
	completeOut();
	CHECK(c.dsOutBusy == 0 && c.dsLightbarPending == 0, "endpoint idle");

	printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
	return g_fail ? 1 : 0;
}
