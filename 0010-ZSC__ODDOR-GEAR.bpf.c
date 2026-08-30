// SPDX-License-Identifier: GPL-2.0-only
/*
 * ZSC ODDOR-GEAR (4785:7353) -- 8-speed H-pattern sim racing shifter.
 *
 * PROBLEM
 * -------
 * The firmware declares Usage (Game Pad) with Buttons 1-8, so hid-input maps
 * them onto BTN_GAMEPAD + (N-1):
 *
 *     gear 1 -> 0x130 BTN_SOUTH (A)      gear 5 -> 0x134 BTN_WEST (Y)
 *     gear 2 -> 0x131 BTN_EAST  (B)      gear 6 -> 0x135 BTN_Z      <-- dead
 *     gear 3 -> 0x132 BTN_C     <-- dead gear 7 -> 0x136 BTN_TL (LB)
 *     gear 4 -> 0x133 BTN_NORTH (X)      gear 8 -> 0x137 BTN_TR (RB)
 *
 * BTN_C (0x132) and BTN_Z (0x135) are legacy codes, not part of the standard
 * gamepad layout in Documentation/input/gamepad.rst, so SDL and Steam Input
 * drop them -- gears 3 and 6 do nothing. Every other gear lands on a face or
 * shoulder button, which is undesirable on a shifter: those are exactly the
 * codes games and other sim hardware compete for.
 *
 * FIX
 * ---
 * Declare the device as Usage (Joystick) rather than Usage (Game Pad). This is
 * both more accurate for an H-pattern shifter and moves all 8 buttons out of
 * the gamepad block entirely, into the contiguous BTN_JOYSTICK block, where
 * they enumerate as plain buttons 1-8:
 *
 *     gear 1 -> 0x120 BTN_TRIGGER  (button 1)
 *     gear 2 -> 0x121 BTN_THUMB    (button 2)
 *     gear 3 -> 0x122 BTN_THUMB2   (button 3)
 *     gear 4 -> 0x123 BTN_TOP      (button 4)
 *     gear 5 -> 0x124 BTN_TOP2     (button 5)
 *     gear 6 -> 0x125 BTN_PINKIE   (button 6)
 *     gear 7 -> 0x126 BTN_BASE     (button 7)
 *     gear 8 -> 0x127 BTN_BASE2    (button 8)
 *
 * No gamepad button codes remain, so the shifter cannot collide with a wheel,
 * pad or any other controller's A/B/X/Y/LB/RB bindings. All 8 gears work.
 *
 * Second, the descriptor declares two phantom 16-bit axes (Usage (0), logical
 * range 0..32767) that are hardwired to zero in every report. The kernel
 * exposes them as ABS_MISC with Min 0 / Max 32767 / Value 0 -- an axis that
 * reads permanently fully-deflected, which testers and games show as a stuck
 * stick. Marking that Input item Constant removes the axis entirely, leaving a
 * clean 8-button, 0-axis joystick.
 *
 * This is two bytes:
 *   offset 3  0x05 Usage (Game Pad)        -> 0x04 Usage (Joystick)
 *   offset 65 0x02 Input (Data,Var,Abs)    -> 0x01 Input (Constant)
 * The descriptor stays 68 bytes and the report format is untouched -- byte 0
 * remains the button bitmask, gear N setting bit N-1.
 *
 * KNOWN TRADE-OFF
 * ---------------
 * A device that is not classified as a GameController is invisible to software
 * that only enumerates GameControllers. Testing with
 * SDL_GAMECONTROLLER_IGNORE_DEVICES=0x4785/0x7353 showed the target game
 * behaving that way. If the shifter does not appear after this change, that is
 * why -- see UNINSTALL below to revert.
 *
 * UNINSTALL
 * ---------
 *     sudo rm /etc/udev-hid-bpf/0010-ZSC__ODDOR-GEAR.bpf.o
 *     sudo rm /etc/udev/rules.d/99-hid-bpf-0010-ZSC__ODDOR-GEAR.rules
 *     sudo udevadm control --reload
 * then replug the shifter.
 *
 * For reference, the original 68-byte report descriptor:
 *
 *     0x05, 0x01,        // Usage Page (Generic Desktop)      0
 *     0x09, 0x05,        // Usage (Game Pad)                  2  <-- byte 3 -> 0x04
 *     0xa1, 0x01,        // Collection (Application)          4
 *     0x15, 0x00,        //  Logical Minimum (0)              6
 *     0x25, 0xff,        //  Logical Maximum (255)            8
 *     0x75, 0x08,        //  Report Size (8)                  10
 *     0x95, 0x40,        //  Report Count (64)                12
 *     0x09, 0x01,        //  Usage (Pointer)                  14
 *     0xb1, 0x02,        //  Feature (Data,Var,Abs)           16
 *     0x09, 0x01,        //  Usage (Pointer)                  18
 *     0x91, 0x02,        //  Output (Data,Var,Abs)            20
 *     0x09, 0x01,        //  Usage (Pointer)                  22
 *     0xa1, 0x00,        //  Collection (Physical)            24
 *     0x05, 0x09,        //   Usage Page (Button)             26
 *     0x19, 0x01,        //   Usage Minimum (1)               28
 *     0x29, 0x08,        //   Usage Maximum (8)               30
 *     0x15, 0x00,        //   Logical Minimum (0)             32
 *     0x25, 0x01,        //   Logical Maximum (1)             34
 *     0x75, 0x01,        //   Report Size (1)                 36
 *     0x95, 0x08,        //   Report Count (8)                38
 *     0x81, 0x02,        //   Input (Data,Var,Abs)            40
 *     0x75, 0x00,        //   Report Size (0)                 42
 *     0x95, 0x00,        //   Report Count (0)                44
 *     0x81, 0x01,        //   Input (Cnst,Arr,Abs)            46
 *     0x05, 0x01,        //   Usage Page (Generic Desktop)    48
 *     0x09, 0x00,        //   Usage (Undefined)               50
 *     0x09, 0x00,        //   Usage (Undefined)               52
 *     0x16, 0x00, 0x00,  //   Logical Minimum (0)             54
 *     0x26, 0xff, 0x7f,  //   Logical Maximum (32767)         57
 *     0x75, 0x10,        //   Report Size (16)                60
 *     0x95, 0x02,        //   Report Count (2)                62
 *     0x81, 0x02,        //   Input (Data,Var,Abs)            64
 *     0xc0,              //  End Collection                   66
 *     0xc0,              // End Collection                    67
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>

#define VID_ZSC			0x4785
#define PID_ODDOR_GEAR		0x7353

#define ODDOR_RDESC_SIZE	68

/* byte 3: the application Usage operand */
#define USAGE_OFFSET		3
#define USAGE_GAMEPAD		0x05
#define USAGE_JOYSTICK		0x04

/* byte 65: the Input item operand for the two phantom 16-bit axes */
#define AXES_INPUT_OFFSET	65
#define INPUT_DATA_VAR_ABS	0x02
#define INPUT_CONSTANT		0x01

HID_BPF_CONFIG(
	HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_ZSC, PID_ODDOR_GEAR),
);

SEC(HID_BPF_RDESC_FIXUP)
int BPF_PROG(hid_fix_rdesc_oddor_gear, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, HID_MAX_DESCRIPTOR_SIZE /* size */);

	if (!data)
		return 0; /* EPERM check */

	if (hctx->size != ODDOR_RDESC_SIZE)
		return 0;

	/* Game Pad -> Joystick: moves the 8 buttons to BTN_TRIGGER..BTN_BASE2. */
	data[USAGE_OFFSET] = USAGE_JOYSTICK;

	/*
	 * Turn the two phantom axes into constant padding so no axis is
	 * created at all. They are declared Usage (0) with logical range
	 * 0..32767 but are hardwired to zero, which the kernel exposes as
	 * ABS_MISC sitting permanently at its minimum -- i.e. an axis that
	 * reads fully deflected forever. All 11479 captured reports had these
	 * four bytes zero, so nothing is lost by discarding them.
	 */
	data[AXES_INPUT_OFFSET] = INPUT_CONSTANT;

	return 0;
}

HID_BPF_OPS(oddor_gear) = {
	.hid_rdesc_fixup = (void *)hid_fix_rdesc_oddor_gear,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	if (ctx->rdesc_size != ODDOR_RDESC_SIZE) {
		ctx->retval = -EINVAL;
		return 0;
	}

	/* Only touch a descriptor that still declares Usage (Game Pad) ... */
	if (ctx->rdesc[USAGE_OFFSET] != USAGE_GAMEPAD) {
		ctx->retval = -EINVAL;
		return 0;
	}

	/* ... and still has the phantom axes declared as real input. */
	if (ctx->rdesc[AXES_INPUT_OFFSET] != INPUT_DATA_VAR_ABS) {
		ctx->retval = -EINVAL;
		return 0;
	}

	ctx->retval = 0;

	return 0;
}

char _license[] SEC("license") = "GPL";
