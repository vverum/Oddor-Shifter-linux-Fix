// SPDX-License-Identifier: GPL-2.0-only
/*
 * ZSC ODDOR-GEAR (USB 4785:7353) -- 8-speed H-pattern sim racing shifter.
 *
 * Presents the shifter as a joystick with buttons 1-8, one per gear.
 *
 * The firmware's report descriptor has two defects, both corrected here by
 * patching a single byte each as the kernel reads the descriptor. The report
 * data itself is untouched: byte 0 of every report stays a button bitmask
 * with gear N setting bit N-1.
 *
 *   byte 3   Usage (Game Pad) -> Usage (Joystick)
 *
 *     As a Game Pad, hid-input maps Button N to BTN_GAMEPAD + (N-1). That
 *     puts gears 3 and 6 on BTN_C (0x132) and BTN_Z (0x135) -- legacy codes
 *     outside the standard gamepad layout, which SDL and Steam discard, so
 *     those two gears register nowhere. As a Joystick the mapping becomes
 *     BTN_JOYSTICK + (N-1): BTN_TRIGGER..BTN_BASE2, eight contiguous valid
 *     codes that every layer above evdev numbers 1-8. No gamepad codes
 *     remain, so the shifter cannot contend with a wheel or pad for A/B/X/Y.
 *
 *   byte 65  Input (Data,Var,Abs) -> Input (Constant)
 *
 *     The descriptor declares two 16-bit fields with Usage (0) and logical
 *     range 0..32767 that are hardwired to zero in every report. The kernel
 *     exposes them as ABS_MISC sitting at its minimum -- an axis reading
 *     permanently fully deflected. Marking the item Constant makes it
 *     padding, so no axis is created. All 11479 captured reports had these
 *     bytes zero, so nothing is lost.
 *
 * Result: a clean 8-button, 0-axis joystick, which is what an H-pattern
 * shifter physically is.
 *
 * probe() refuses to attach unless the descriptor is exactly 68 bytes and
 * both target bytes still hold their original values, so the program cannot
 * double-apply or match an unexpected device.
 *
 * Install with install.sh. Full analysis in FINDINGS.md.
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
