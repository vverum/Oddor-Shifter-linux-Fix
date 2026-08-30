#!/bin/bash
# ZSC ODDOR-GEAR (USB 4785:7353) -- complete installer. Idempotent, safe to re-run.
#
#   sudo bash install.sh
#
# Leaves the shifter working as a joystick with buttons 1-8, one per gear.
# Two independent layers are installed; both are required.
#
#   1. usbhid binding, as a systemd unit.
#      The device reports bDeviceClass=0xFF (vendor specific) while its only
#      interface is HID. Linux refuses interface-class-only driver matches
#      against vendor-specific devices unless the match record names a vendor
#      ID, so usbhid never binds and no input device exists at all. Writing
#      the VID/PID to usbhid's new_id supplies such a record. This is done at
#      boot because new_id registers against the driver, not the device, so a
#      single write covers hotplug for the rest of the uptime.
#
#   2. HID-BPF descriptor fix.
#      Corrects two bytes of the report descriptor as the kernel reads it, so
#      the 8 gears land on joystick buttons 1-8 instead of gamepad codes (two
#      of which the SDL/Steam layer discards), and the phantom stuck axis is
#      removed. See 0010-ZSC__ODDOR-GEAR.bpf.c for detail.
#
# Ordering handles itself: the unit writes new_id -> usbhid binds -> the bind
# emits a HID "add" uevent -> 81-hid-bpf.rules attaches the BPF program.
#
# Nothing is written to the device; its firmware is untouched. Nothing needs
# signing, so Secure Boot and kernel lockdown are not obstacles.
#
# Reverse with uninstall.sh. Full analysis in FINDINGS.md.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OBJ="$DIR/0010-ZSC__ODDOR-GEAR.bpf.o"
UNIT=/etc/systemd/system/oddor-gear-usbhid.service
SERVICE=oddor-gear-usbhid.service

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || die "must run as root:  sudo bash install.sh"
[ -f "$OBJ" ] || die "$OBJ is missing.
       Rebuild it with the commands under \"Rebuilding from source\" in FINDINGS.md."
command -v udev-hid-bpf >/dev/null || die "udev-hid-bpf is not installed (dnf install udev-hid-bpf)"

echo "==> ZSC ODDOR-GEAR (4785:7353) install"
echo

echo "[1/3] usbhid binding"
cat > "$UNIT" <<'UNITEOF'
[Unit]
Description=Register ODDOR-GEAR (4785:7353) with usbhid
After=systemd-udev-settle.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'echo "4785 7353" > /sys/bus/usb/drivers/usbhid/new_id || true'

[Install]
WantedBy=multi-user.target
UNITEOF
systemctl daemon-reload
systemctl enable --now "$SERVICE" >/dev/null 2>&1 || true
echo "      unit  : $UNIT"
echo "      state : $(systemctl is-enabled $SERVICE 2>&1), $(systemctl is-active $SERVICE 2>&1)"
echo

echo "[2/3] HID-BPF descriptor fix"
udev-hid-bpf install --force "$OBJ" >/dev/null 2>&1 \
	|| die "udev-hid-bpf install failed; re-run without >/dev/null to see why"
udevadm control --reload
RULE="$(ls /etc/udev/rules.d/ 2>/dev/null | grep -i oddor | head -1)"
echo "      object: /etc/udev-hid-bpf/$(basename "$OBJ")"
echo "      rule  : /etc/udev/rules.d/${RULE:-<none found>}"
echo

echo "[3/3] applying to the connected device"
SYS=""
for d in /sys/bus/usb/devices/*/; do
	[ -f "$d/idVendor" ] || continue
	[ "$(cat "$d/idVendor")" = "4785" ] && [ "$(cat "$d/idProduct")" = "7353" ] && SYS="${d%/}"
done
if [ -z "$SYS" ]; then
	echo "      shifter is not connected"
	echo
	echo "==> Installed. The fix will apply automatically when you plug it in."
	exit 0
fi

# Re-enumerate so udev re-runs and attaches the BPF program.
echo 0 > "$SYS/authorized"; sleep 1; echo 1 > "$SYS/authorized"; sleep 3

DRIVER="$(basename "$(readlink "$SYS:1.0/driver" 2>/dev/null)" 2>/dev/null || true)"
KERNEL="$(journalctl -k --no-pager 2>/dev/null | grep -i 'ODDOR-GEAR\] on' | tail -1 \
          | grep -o 'Joystick\|Gamepad' || true)"
JS="$(ls /dev/input/by-id/*ODDOR*-joystick 2>/dev/null | grep -v event | head -1 || true)"
# jstest blocks until an event arrives, so it must be run under timeout.
REPORT="$(timeout 3 jstest --normal "$JS" </dev/null 2>/dev/null | head -3 | tail -2 | tr '\n' ' ' || true)"

echo "      driver: ${DRIVER:-none}"
echo "      kernel: reports device as ${KERNEL:-unknown}"
echo "      node  : ${JS:-none}"
echo "      jstest: ${REPORT:-no reading}"
echo

if [ "$KERNEL" = "Joystick" ] && [ "${REPORT#*0 axes}" != "$REPORT" ] \
   && [ "${REPORT#*BtnA}" = "$REPORT" ]; then
	echo "==> OK. 8 buttons, no axes - gears 1-8 report as Buttons 1-8."
else
	echo "==> PROBLEM: the device is not in the expected state."
	echo "    Wanted: kernel says Joystick, jstest says 0 axes and 8 Btn/Thumb/Top/Base names."
	echo "    If jstest shows BtnA/BtnB/BtnC..., the BPF program did not attach:"
	echo "      journalctl -k -n 20    # look for the hid-generic line"
	exit 1
fi
