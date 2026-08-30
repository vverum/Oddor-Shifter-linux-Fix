#!/bin/bash
# Remove the ZSC ODDOR-GEAR fix installed by install.sh.
#
#   sudo bash uninstall.sh
#
# Removes both layers: the HID-BPF descriptor fix and the systemd unit that
# binds usbhid to the device. The running new_id registration cannot be undone
# without a reboot, so the shifter keeps working (as a gamepad, with gears 3
# and 6 dead) until then; after the next reboot it returns fully to stock,
# meaning no driver binds to it at all.
set -e
UNIT=/etc/systemd/system/oddor-gear-usbhid.service
SERVICE=oddor-gear-usbhid.service

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "must run as root:  sudo bash uninstall.sh"

echo "==> ZSC ODDOR-GEAR (4785:7353) uninstall"
echo

echo "[1/3] HID-BPF descriptor fix"
REMOVED=0
for f in /etc/udev-hid-bpf/*ODDOR* /etc/udev/rules.d/*ODDOR*; do
	[ -e "$f" ] || continue
	rm -f "$f" && echo "      removed $f" && REMOVED=1
done
[ "$REMOVED" = 1 ] || echo "      nothing to remove (was not installed)"
udevadm control --reload
echo

echo "[2/3] usbhid binding unit"
if [ -e "$UNIT" ]; then
	systemctl disable --now "$SERVICE" >/dev/null 2>&1 || true
	rm -f "$UNIT" && echo "      removed $UNIT"
	systemctl daemon-reload
else
	echo "      nothing to remove (was not installed)"
fi
echo

echo "[3/3] re-enumerating device"
SYS=""
for d in /sys/bus/usb/devices/*/; do
	[ -f "$d/idVendor" ] || continue
	[ "$(cat "$d/idVendor")" = "4785" ] && [ "$(cat "$d/idProduct")" = "7353" ] && SYS="${d%/}"
done
if [ -n "$SYS" ]; then
	echo 0 > "$SYS/authorized"; sleep 1; echo 1 > "$SYS/authorized"; sleep 2
	echo "      done"
else
	echo "      shifter is not connected"
fi
echo

echo "==> Uninstalled."
echo "    Until you reboot, the shifter keeps working as a gamepad (gears 3 and 6 dead),"
echo "    because the running new_id registration cannot be revoked."
echo "    After a reboot no driver binds to it at all - back to fully stock."
