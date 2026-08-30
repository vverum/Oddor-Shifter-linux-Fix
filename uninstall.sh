#!/bin/bash
# Remove the ODDOR-GEAR fix entirely, returning the device to stock behaviour
# (no driver at all after the next reboot). Run as root: sudo bash uninstall.sh
set -e
[ "$(id -u)" = 0 ] || { echo "!! run as root: sudo bash uninstall.sh"; exit 1; }

echo "### removing HID-BPF fix"
rm -fv /etc/udev-hid-bpf/*ODDOR* /etc/udev/rules.d/*ODDOR* 2>/dev/null || true
udevadm control --reload

echo "### removing usbhid binding unit"
systemctl disable --now oddor-gear-usbhid.service 2>/dev/null || true
rm -fv /etc/systemd/system/oddor-gear-usbhid.service
systemctl daemon-reload

sys=""
for d in /sys/bus/usb/devices/*/; do
	[ -f "$d/idVendor" ] || continue
	[ "$(cat "$d/idVendor")" = "4785" ] && [ "$(cat "$d/idProduct")" = "7353" ] && sys="${d%/}"
done
[ -n "$sys" ] && { echo 0 > "$sys/authorized"; sleep 1; echo 1 > "$sys/authorized"; }
echo "done. The new_id binding stays live until reboot."
