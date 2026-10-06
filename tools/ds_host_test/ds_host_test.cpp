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
	UsbTrb dsOutTrb; uint8_t* dsOutBuffer; bool dsOutOpened; bool dsOutFailed; uint16_t dsReportsSinceSend;
	volatile LONG dsLightbarPending; uint8_t dsLightbar[3]; bool dsLightbarSetupSent;
};
static Controller connectedControllers[4];

struct Sent { std::vector<uint8_t> data; UsbTrb* trb; DWORD cb; };
static std::vector<Sent> g_sent;
static int32_t noopCompleteHandler(DWORD, int32_t) { return 0; }
static void SendInterruptRequest(deviceHandle*, UsbTrb* trb, void* data, uint32_t len, DWORD cb) {
	Sent s; s.data.assign((uint8_t*)data, (uint8_t*)data + len); s.trb = trb; s.cb = cb; g_sent.push_back(s);
}
static usb_endpoint_descriptor g_epOut = { 7, 5, 0x03, 0x03, swap_endianness_16(64), 4 };
static bool g_noOut = false;
static usb_endpoint_descriptor* UsbdGetEndpointDescriptor(deviceHandle*, int, int, int dir) { return (dir == USB_DIRECTION_OUT && !g_noOut) ? &g_epOut : NULL; }
static NTSTATUS UsbdOpenEndpoint(deviceHandle*, int, int, int, int, DWORD* ep) { *ep = 0x1003; return 0; }

#include "ds_section.inc"

static int g_fail = 0;
#define CHECK(c, m) do { if (c) printf("  ok   %s\n", m); else { printf("  FAIL %s (line %d)\n", m, __LINE__); g_fail++; } } while (0)

static void report(uint8_t* r, bool touch, int16_t ax, int16_t ay, int16_t az) {
	memset(r, 0, 64); r[0] = 0x01; r[1] = 128; r[2] = 128;
	if (touch) r[DS_IN_BUTTONS2] |= DS_BTN2_TOUCHPAD;
	r[22] = ax & 0xFF; r[23] = (ax >> 8) & 0xFF; r[24] = ay & 0xFF; r[25] = (ay >> 8) & 0xFF; r[26] = az & 0xFF; r[27] = (az >> 8) & 0xFF;
}
static int16_t settle(int idx, uint8_t* r, int n) { ButtonsReport b; for (int i = 0; i < n; i++) { memset(&b, 0, sizeof b); DsProcessInputReport(idx, r, &b); } return b.x; }

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

	printf("== first report: lazy open + idle colour from the callback ==\n");
	report(r, false, 0, 8192, 0);
	settle(0, r, 1);
	CHECK(c.dsOutOpened && !c.dsOutFailed && c.dsOutBuffer != NULL, "OUT endpoint opened on first input report");
	CHECK(g_sent.size() == 1 && g_sent[0].data.size() == 48, "blue report queued immediately from the callback");
	{ std::vector<uint8_t>& d = g_sent[0].data;
	  CHECK(d[0] == 0x02 && d[2] == 0x04, "report 0x02 with lightbar control flag");
	  CHECK(d[39] == 0x02 && d[42] == 0x02, "first report carries LIGHT_OUT setup");
	  CHECK(d[45] == 0 && d[46] == 0 && d[47] == 255, "idle colour blue");
	  CHECK(g_sent[0].cb == (DWORD)(uintptr_t)noopCompleteHandler, "no-op completion like the Switch Pro path"); }
	settle(0, r, 3);
	CHECK(g_sent.size() == 1, "nothing more sent while nothing is pending");

	printf("== toggle + steering ==\n");
	report(r, true, 0, 8192, 0); settle(0, r, 1);
	CHECK(c.dsTiltSteering, "touchpad press toggles mode on");
	CHECK(g_sent.size() == 1 && c.dsLightbarPending == 1, "red stays pending until the spacing allows");
	report(r, false, 0, 8192, 0); settle(0, r, 1);
	report(r, true, 0, 8192, 0); settle(0, r, 1);
	CHECK(c.dsTiltSteering, "press inside the hold-off window is ignored (bounce)");
	report(r, false, 0, 8192, 0);
	settle(0, r, DS_LIGHTBAR_MIN_SPACING);
	CHECK(g_sent.size() == 2 && g_sent[1].data[45] == 255 && g_sent[1].data[47] == 0, "red sent after the spacing window");
	CHECK(g_sent[1].data[39] == 0, "second report has no setup bytes");

	c.dsSteerDelay = 0;                              // skip the start delay for the math checks
	report(r, false, 5793, 5793, 0);                 // 45 degrees, tilt so ax is positive
	int16_t lx = settle(0, r, 40);
	CHECK(lx == -32767, "45 degree roll with positive ax = full stick LEFT (-32767)");
	report(r, false, -5793, 5793, 0);
	lx = settle(0, r, 40);
	CHECK(lx == 32767, "opposite roll = full stick RIGHT (+32767)");
	report(r, false, 400, 8192, 0);
	lx = settle(0, r, 40);
	CHECK(lx == 0, "2.8 degrees is inside the dead zone");
	report(r, false, 8192, 0, 8192);
	lx = settle(0, r, 40);
	CHECK(lx == -32767, "roll independent of pitch (horizontal from ay and az)");

	printf("== toggle off restores physical stick ==\n");
	report(r, true, 5793, 5793, 0); settle(0, r, 1);
	CHECK(!c.dsTiltSteering, "touchpad press toggles mode off");
	report(r, false, 5793, 5793, 0);
	settle(0, r, DS_LIGHTBAR_MIN_SPACING + 1);
	CHECK(g_sent.size() == 3 && g_sent[2].data[47] == 255 && g_sent[2].data[45] == 0, "blue sent after the spacing window");
	{ ButtonsReport b; memset(&b, 0, sizeof b); b.x = 1234; DsProcessInputReport(0, r, &b); CHECK(b.x == 1234, "mode off: LX from HID mapping preserved"); }

	printf("== latest colour wins, never two writes inside the spacing ==\n");
	DsSetLightbar(0, 4, 4, 4); settle(0, r, 1);        // spacing long elapsed: goes out at once, resets the window
	size_t before = g_sent.size();
	CHECK(g_sent.back().data[45] == 4, "write goes out immediately once the window has elapsed");
	DsSetLightbar(0, 1, 2, 3);
	DsSetLightbar(0, 9, 9, 9);
	settle(0, r, 5);
	CHECK(g_sent.size() == before, "no write inside the spacing window");
	settle(0, r, DS_LIGHTBAR_MIN_SPACING);
	CHECK(g_sent.size() == before + 1 && g_sent.back().data[45] == 9 && g_sent.back().data[47] == 9, "one write with the latest colour once allowed");

	printf("== OUT endpoint missing: input keeps working ==\n");
	Controller& c2 = connectedControllers[1]; memset(&c2, 0, sizeof c2);
	c2.deviceHandle = &dev; c2.controllerDriver = &ext; c2.vendorId = 0x054C; c2.productId = 0x0DF2;
	g_noOut = true; before = g_sent.size();
	settle(1, r, 3);
	CHECK(c2.dsOutOpened && c2.dsOutFailed && c2.dsOutBuffer == NULL && g_sent.size() == before, "open failure recorded, nothing sent");
	report(r, true, 0, 8192, 0); settle(1, r, 1);
	CHECK(c2.dsTiltSteering, "toggle still works without a lightbar");
	g_noOut = false;

	printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
	return g_fail ? 1 : 0;
}
