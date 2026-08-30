#!/bin/bash
# ODDOR-GEAR shifter fix -- complete installer. Idempotent; safe to re-run.
# Run as root:  sudo bash install.sh
#
# Applies two independent layers:
#
#   1. usbhid binding. The device reports bDeviceClass=0xFF (vendor specific)
#      while its only interface is HID. Linux's usb_match_one_id_intf() refuses
#      interface-class-only matches against vendor-specific devices unless the
#      match record names a vendor ID, so usbhid never binds and no input
#      device is created at all. Writing to new_id supplies such a record.
#      Installed as a systemd unit so it survives reboots.
#
#   2. HID-BPF descriptor fix. The firmware declares Usage (Game Pad), which
#      puts the 8 gears on BTN_GAMEPAD+(N-1) -- landing gears 3 and 6 on
#      BTN_C/BTN_Z, legacy codes SDL and Steam discard. It also declares two
#      phantom 16-bit axes hardwired to zero, exposed as ABS_MISC reading
#      permanently fully-deflected. Two bytes fix both: the device becomes a
#      Joystick with plain buttons 1-8 and no axes.
#
# See FINDINGS.md for the full analysis.
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
OBJ="$DIR/0010-ZSC__ODDOR-GEAR.bpf.o"
UNIT=/etc/systemd/system/oddor-gear-usbhid.service

[ "$(id -u)" = 0 ] || { echo "!! run as root: sudo bash install.sh"; exit 1; }
[ -f "$OBJ" ] || { echo "!! $OBJ missing (see FINDINGS.md section 14 to rebuild)"; exit 1; }
command -v udev-hid-bpf >/dev/null || { echo "!! udev-hid-bpf not installed"; exit 1; }

echo "### 1/3  usbhid binding (systemd unit)"
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
systemctl enable --now oddor-gear-usbhid.service
echo "    enabled: $(systemctl is-enabled oddor-gear-usbhid.service)"

echo "### 2/3  HID-BPF descriptor fix"
udev-hid-bpf install --force "$OBJ"
udevadm control --reload

echo "### 3/3  re-enumerate and verify"
sys=""
for d in /sys/bus/usb/devices/*/; do
	[ -f "$d/idVendor" ] || continue
	[ "$(cat "$d/idVendor")" = "4785" ] && [ "$(cat "$d/idProduct")" = "7353" ] && sys="${d%/}"
done
if [ -z "$sys" ]; then
	echo "    device not plugged in - it will be fixed automatically when connected."
	exit 0
fi
echo 0 > "$sys/authorized"; sleep 1; echo 1 > "$sys/authorized"; sleep 3

js="$(ls /dev/input/by-id/*ODDOR*-joystick 2>/dev/null | grep -v event | head -1)"
echo
[ -n "$js" ] && timeout 3 jstest --normal "$js" </dev/null 2>/dev/null | head -3
echo
echo "Expected: 0 axes, 8 buttons (Trigger, ThumbBtn, ThumbBtn2, TopBtn,"
echo "          TopBtn2, PinkieBtn, BaseBtn, BaseBtn2) = gears 1-8."
