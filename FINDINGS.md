# ZSC ODDOR-GEAR — Linux fix

**Device:** ZSC ODDOR-GEAR, USB `4785:7353` — 8-speed H-pattern sim racing shifter
**Host:** Fedora 44, kernel `7.1.10-200.fc44.x86_64`, Secure Boot on, lockdown `[integrity]`
**Used with:** BeamNG.drive via Steam
**Status:** working — all 8 gears, buttons 1–8, no axes

---

## 1. Summary

Out of the box the shifter did nothing on Linux: it enumerated on USB but the kernel bound no
driver, so no input device existed at all. Four separate problems had to be fixed.

| # | Problem | Cause | Fix |
|---|---|---|---|
| 1 | No driver bound at all | `bDeviceClass=0xFF` blocks interface-class driver matching | `new_id` on `usbhid`, via systemd unit |
| 2 | Gears 3 and 6 dead | `Usage (Game Pad)` put them on `BTN_C`/`BTN_Z`, which SDL discards | HID-BPF: byte 3 → `Usage (Joystick)` |
| 3 | Phantom stuck axis | Two `Usage (0)` axes hardwired to zero, exposed as `ABS_MISC` at minimum | HID-BPF: byte 65 → `Input (Constant)` |
| 4 | Steam showed A/B/X/Y, missing 2 gears | Stale cached `SDL_GamepadBind` mapping | Deleted from `config.vdf` |

Problems 2 and 3 are both fixed by a single HID-BPF program that patches two bytes of the report
descriptor at load time. Nothing is written to the device; the firmware is untouched.

**Not a problem:** KDE's *Game Controllers* panel drawing a gamepad icon — see section 8.

---

## 2. The device

| Field | Value |
|---|---|
| USB ID | `4785:7353` |
| Strings | Manufacturer `ZSC`, Product `ODDOR-GEAR`, Serial empty |
| Firmware rev | `bcdDevice 1.14` |
| Speed | Full Speed (12 Mbps) |
| Device class | `FF / FF / FF` — Vendor Specific ← **cause of problem 1** |
| Interface 0 | Class `03` (HID), Subclass `01` (Boot), Protocol `05` |
| HID version | `bcdHID 1.01` |
| Endpoints | `0x81` IN and `0x01` OUT, interrupt, 64 bytes, 10 ms |
| Report descriptor | 68 bytes |
| Power | `MaxPower 500mA`, bus powered |

Several of these are off-spec: `bInterfaceProtocol 5` is not a valid Boot protocol (only 0/1/2
are), `bcdHID 1.01` is unusual, and a device whose function is defined by its interfaces should
report `bDeviceClass 0x00`, not `0xFF`. The firmware was evidently only ever validated against
Windows, whose HID stack is far more permissive.

### Report format

Each report is **5 bytes** at ~125 Hz. **Byte 0 is a button bitmask: gear N sets bit N−1.**
Bytes 1–4 are the two phantom axes and are always zero.

| Position | Report |
|---|---|
| Neutral | `00 00 00 00 00` |
| Gear 1 | `01 00 00 00 00` |
| Gear 2 | `02 00 00 00 00` |
| Gear 3 | `04 00 00 00 00` |
| Gear 4 | `08 00 00 00 00` |
| Gear 5 | `10 00 00 00 00` |
| Gear 6 | `20 00 00 00 00` |
| Gear 7 | `40 00 00 00 00` |
| Gear 8 | `80 00 00 00 00` |

Verified by capturing 11,479 reports over 91.8 s while shifting through every gear: each gear
held steady for 438 consecutive identical reports, with zero dropouts, chatter or spurious
values. **The hardware is flawless** — every problem here was on the host side.

---

## 3. Problem 1 — no driver bound

### Symptom

The device enumerated (`New USB device found, idVendor=4785, idProduct=7353`) and then *nothing*.
No `input:` line, no `hid-generic` line, no error. No `/sys/bus/hid/devices` entry, no `hidraw`,
no `/dev/input/event*`. It was the only HID interface out of 15 on the machine with
`Driver=[none]`.

### Cause

`usb_match_one_id_intf()` in `drivers/usb/core/driver.c` contains this rule:

> The interface class, subclass, protocol and number should never be checked for a match if the
> device class is Vendor Specific, unless the match record specifies the Vendor ID.

`usbhid` matches on interface class alone:

```c
{ .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
  .bInterfaceClass = USB_INTERFACE_CLASS_HID }
```

It names no vendor. The device reports `bDeviceClass = 0xFF`. Both halves of the rule are
satisfied, so the match is **skipped entirely** — `usbhid` is never offered the device, no probe
runs, and nothing is logged because nothing failed.

The rule is deliberate: a vendor-specific device is declaring that its interfaces may not mean
what the standard says, so the kernel refuses to let *generic* drivers claim them. Windows binds
HIDCLASS from the interface descriptor and ignores the device-level class, which is why the same
firmware works there.

Confirmed by correlation — every HID interface on the machine and its device class:

| `bDeviceClass` | Driver | Count |
|---|---|---|
| `00` / `ef` | `usbhid` | 14 |
| **`ff`** (ODDOR-GEAR) | **none** | 1 |

### Fix

Give `usbhid` a match record that *does* name the vendor, satisfying the exemption:

```bash
echo "4785 7353" > /sys/bus/usb/drivers/usbhid/new_id
```

`new_id` builds an entry equivalent to `USB_DEVICE(vend, prod)`, whose `match_flags` is
`USB_DEVICE_ID_MATCH_DEVICE` = `MATCH_VENDOR | MATCH_PRODUCT` (verified in
`include/linux/usb.h:992`). `MATCH_VENDOR` is now set, the guard no longer fires, and `usbhid`
binds. Writing to `new_id` also rescans unbound devices, so it takes effect immediately.

`install.sh` makes this permanent via a systemd oneshot unit. A unit is used rather than a udev
rule because `new_id` registers against the **driver**, not the device, and is global for the
uptime — writing it once at boot is race-free and covers hotplug automatically. `usbhid` is
built into the kernel (`CONFIG_USB_HID=y`), so `modprobe.d` is not an option.

### Ruled out during diagnosis

`usbhid` missing or unloaded; HID quirks (`quirks = 0x0`); kernel cmdline parameters; custom
udev rules; `usbguard` (not installed, `authorized = 1`); a userspace libusb program claiming
the interface; a faulty hub or port.

---

## 4. Problem 2 — gears 3 and 6 dead

Once bound, the shifter worked but **gears 3 and 6 did nothing**, while the others registered as
A, B, X, Y, LB, RB.

### Cause

The descriptor declares `Usage (Game Pad)`, so `hid-input` maps Button *N* to
`BTN_GAMEPAD + (N−1)`:

| Gear | evdev | Name | Status |
|---|---|---|---|
| 1 | `0x130` | `BTN_SOUTH` (A) | ok |
| 2 | `0x131` | `BTN_EAST` (B) | ok |
| **3** | **`0x132`** | **`BTN_C`** | **dead** |
| 4 | `0x133` | `BTN_NORTH` (X) | ok |
| 5 | `0x134` | `BTN_WEST` (Y) | ok |
| **6** | **`0x135`** | **`BTN_Z`** | **dead** |
| 7 | `0x136` | `BTN_TL` (LB) | ok |
| 8 | `0x137` | `BTN_TR` (RB) | ok |

`BTN_C` and `BTN_Z` are legacy leftovers in the `BTN_GAMEPAD` block. They are not part of the
standard layout in `Documentation/input/gamepad.rst`, so SDL's GameController layer and Steam
Input have no slot for them and silently drop those two inputs.

The kernel itself was fine — `joydev` exposed all 8. Only the gamepad-mapping layer above it
discarded two.

### Fix

Declare the device a **Joystick** instead of a Game Pad. `hid-input` then uses
`BTN_JOYSTICK + (N−1)`, a contiguous block where all 8 codes are valid:

| Gear | evdev | Name |
|---|---|---|
| 1 | `0x120` | `BTN_TRIGGER` |
| 2 | `0x121` | `BTN_THUMB` |
| 3 | `0x122` | `BTN_THUMB2` |
| 4 | `0x123` | `BTN_TOP` |
| 5 | `0x124` | `BTN_TOP2` |
| 6 | `0x125` | `BTN_PINKIE` |
| 7 | `0x126` | `BTN_BASE` |
| 8 | `0x127` | `BTN_BASE2` |

(Constants verified in `include/uapi/linux/input-event-codes.h`, lines 366–391.)

These names are Linux's historical labels for the joystick block. Every layer above evdev —
`joydev`, SDL, Steam, the game — refers to them by **number**, so they present as
**Buttons 1–8**. No gamepad codes remain on the device, so it cannot contend with a wheel or pad
over A/B/X/Y/LB/RB.

This is one byte: **offset 3, `0x05` → `0x04`**.

Declaring a joystick is also simply more accurate. An H-pattern shifter is not a gamepad, and
real sim shifters (Logitech G27, Thrustmaster TH8A) present exactly this way.

---

## 5. Problem 3 — phantom stuck axis

### Cause

The descriptor declares two 16-bit fields with `Usage (0)` (undefined, off-spec) and logical
range 0–32767. They are hardwired to zero in every report — all 11,479 captured reports had
those four bytes zero. The kernel collapses them into one axis:

```
Event code 40 (ABS_MISC)   Value 0   Min 0   Max 32767
```

An axis reading `0` on a `0..32767` range is at its **minimum** — i.e. permanently
fully-deflected. Testers and games show that as a stuck stick.

This was present from the very beginning, in *both* gamepad and joystick modes. It was
initially dismissed as cosmetic; that was a mistake, and it is the likely reason the device
looked wrong in several tools regardless of what else changed.

### Fix

Mark that Input item **Constant**, so it becomes padding and no axis is created:

**offset 65, `0x02` (Input Data,Var,Abs) → `0x01` (Input Constant)**

The report size is unchanged and no information is lost. Result: a clean **8-button, 0-axis**
joystick, which is what an H-pattern shifter physically is.

---

## 6. The HID-BPF program

Both descriptor fixes live in `0010-ZSC__ODDOR-GEAR.bpf.c`.

### Where it sits

```
USB device
  → usbhid            claims the interface, fetches the 68-byte report descriptor
  → [HID-BPF hook]    ← the program rewrites the buffer here
  → hid_parse()       builds the report map from the *patched* descriptor
  → hid-input         maps HID usages onto Linux input codes
  → evdev / joydev    /dev/input/event*, /dev/input/js*
  → SDL / Steam / game
```

The kernel fetches the descriptor, then hands the buffer to any attached `hid_rdesc_fixup`
program *before* parsing it. Everything downstream sees the patched version and has no idea the
device said anything different. The patch exists only in kernel memory and is re-applied on
every enumeration.

### The three parts

**`HID_BPF_CONFIG(HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, 0x4785, 0x7353))`** — metadata
compiled into the object telling `udev-hid-bpf` which device this is for.

**`probe()`** — a safety gate. The loader runs it first and only attaches if it returns 0:

```c
if (ctx->rdesc_size != 68)        → refuse   /* not this device */
if (ctx->rdesc[3]  != 0x05)       → refuse   /* already patched */
if (ctx->rdesc[65] != 0x02)       → refuse   /* already patched */
```

So it cannot double-apply, and cannot corrupt a device whose descriptor differs from the one
actually captured here.

**`hid_fix_rdesc_oddor_gear()`** — the hook, attached via `SEC("struct_ops/hid_rdesc_fixup")`.
It writes the two bytes and returns 0, meaning the size is unchanged.

### How it loads

`udev-hid-bpf install` copies the object to `/etc/udev-hid-bpf/` and writes a udev rule in
`/etc/udev/rules.d/`. Whenever a HID device with that VID/PID appears, udev runs
`udev-hid-bpf add`, which executes `probe()` and attaches the program. It re-applies on every
plug-in and every boot with **no daemon running**.

Because it is BPF rather than a kernel module, nothing needs signing — Secure Boot and lockdown
`[integrity]` are not obstacles. (A custom kernel module would have required MOK enrolment.)

### What it does not touch

The report data path. Byte 0 is still the bitmask with gear N setting bit N−1. Only the
*meaning* the kernel assigns to those bits changes. There is no per-event processing and no
latency added.

---

## 7. Problem 4 — Steam showed A/B/X/Y and lost two gears

Steam had cached a gamepad mapping in `~/.local/share/Steam/config/config.vdf` under
`SDL_GamepadBind`, created while the shifter still presented as a Game Pad:

```
03000000854700005373000001010000,ZSC ODDOR-GEAR,a:b0,b:b1,x:b3,y:b4,leftshoulder:b6,rightshoulder:b7,platform:Linux,
```

(The GUID embeds `8547`/`5373` — `4785`/`7353` byte-swapped.)

Steam applied this over whatever the kernel reported, which is why it painted A/B/X/Y/LB/RB on
top of plain buttons. Worse, it lists only six entries: **`b2` and `b5` are absent** — gears 3
and 6. Steam had frozen the original broken `BTN_C`/`BTN_Z` state and would have kept hiding
those gears no matter what was fixed underneath.

**Fix:** close Steam completely (it rewrites the file on exit), then delete that line from the
`SDL_GamepadBind` value. Steam re-detects the device as a plain 8-button joystick. A stray
`sudo bash …` line that had been mis-pasted into the same config value was removed at the same
time.

This is a one-off repair, already applied. A timestamped `config.vdf.bak-*` backup remains in
`~/.local/share/Steam/config/`.

---

## 8. Not a problem — KDE's gamepad icon

KDE *Settings → Game Controllers* displayed the shifter with gamepad artwork. Nothing was wrong.
Queried directly, SDL — the same library KDE, Steam and BeamNG all use — reports:

```
name    : ZSC ODDOR-GEAR
vendor  : 0x4785   product: 0x7353
buttons : 8   axes: 0   hats: 0
SDL_IsGameController = False
SDL_JoystickGetType  = 0 (UNKNOWN)
```

SDL does **not** classify it as a gamepad. There is no mapping anywhere that could make it one:
the GUID appears in neither `libSDL2` nor `libSDL3`, there is no `gamecontrollerdb` on the
system or in the user config, and `kcm_gamecontroller.so` ships no mappings of its own.

The panel is simply named "Game Controllers", lists every SDL joystick, and falls back to
generic gamepad artwork when `SDL_JoystickGetType` returns `UNKNOWN`. Changing that would
require SDL to report a more specific type such as `WHEEL`, which SDL derives from a hardcoded
VID/PID list that is not user-configurable — not worth patching for an icon.

In practice the stale display cleared after resetting the Settings window.

---

## 9. Install, verify, remove

```bash
sudo bash install.sh      # applies both layers; idempotent, safe to re-run
sudo bash uninstall.sh    # removes both layers
```

`install.sh` installs the systemd unit for the `usbhid` binding, installs the HID-BPF object,
re-enumerates the device and prints the result. Ordering handles itself: the unit writes
`new_id` → the bind emits a HID `add` uevent → `81-hid-bpf.rules` attaches the BPF.

### Verify

```bash
systemctl is-enabled oddor-gear-usbhid.service   # enabled
ls /etc/udev-hid-bpf/                            # 0010-ZSC__ODDOR-GEAR.bpf.o
jstest --normal /dev/input/by-id/usb-ZSC_ODDOR-GEAR-joystick
```

Expected — note `jstest` blocks until an event arrives, so wrap it in `timeout`:

```
Joystick (ZSC ODDOR-GEAR) has 0 axes ()
and 8 buttons (Trigger, ThumbBtn, ThumbBtn2, TopBtn, TopBtn2, PinkieBtn, BaseBtn, BaseBtn2).
```

Live button numbers as SDL and games see them — shift through the gears, expect 1–8:

```bash
python3 -c "
import struct
f=open('/dev/input/js1','rb')
while True:
    t,v,ty,n=struct.unpack('<IhBB',f.read(8))
    if ty&0x80: continue
    if ty&0x01: print('Button',n+1,'pressed' if v else 'released',flush=True)"
```

### Game side

BeamNG.drive binds the shifter by plain button number, in
`~/.local/share/BeamNG/BeamNG.drive/current/settings/inputmaps/73534785.diff`:

```json
{ "action":"gear1", "control":"button0", ... }
```

---

## 10. Rebuilding from source

The compiled `.bpf.o` is kept alongside its source, so a rebuild is only needed if the `.bpf.c`
is edited. The upstream clone was deleted after use; recreate it for the headers and bundled
`vmlinux.h`:

```bash
sudo dnf install -y --disablerepo='*' --enablerepo=fedora --enablerepo=updates \
    clang bpftool libbpf-devel
git clone --depth 1 https://gitlab.freedesktop.org/libevdev/udev-hid-bpf.git

clang -std=gnu11 -fno-stack-protector -O2 -target bpf -g -c \
      -fms-extensions -Wno-microsoft-anon-tag -D__x86_64__ \
      -I udev-hid-bpf/src/bpf -idirafter /usr/include \
      0010-ZSC__ODDOR-GEAR.bpf.c -o /tmp/oddor.unstripped.o
bpftool gen object 0010-ZSC__ODDOR-GEAR.bpf.o /tmp/oddor.unstripped.o

sudo bash install.sh
```

Flags mirror upstream's `src/bpf/meson.build`. `vmlinux.h` ships in the repo, so `bpftool` is
needed only for `gen object`, not BTF generation.

Scoping `dnf` to the Fedora repos matters on this host: it has `cursor`, three copr repos and
RPM Fusion enabled, and a bare `dnf install -y` would silently auto-import third-party GPG keys
during metadata refresh. All three packages come from `fedora`/`updates`.

Version note: `hid_bpf.h` is byte-identical between the installed `udev-hid-bpf`
2.2.0-20251121 and upstream master, so building against master's headers matches the installed
loader. The default (non-`HID_BPF_TRACING`) path uses `SEC("struct_ops/hid_rdesc_fixup")`, the
modern API, correct for kernel 7.1.

---

## Appendix A — the report descriptor

Original, 68 bytes, read with `usbhid-dump -d 4785:7353 -e descriptor`. The two patched bytes
are marked.

```
05 01        Usage Page (Generic Desktop)             0
09 05        Usage (Game Pad)                         2   ← byte 3 → 0x04 (Joystick)
a1 01        Collection (Application)                 4
15 00          Logical Minimum (0)                    6
25 ff          Logical Maximum (255)                  8
75 08          Report Size (8)                       10
95 40          Report Count (64)                     12
09 01          Usage (Pointer)                       14
b1 02          Feature (Data,Var,Abs)                16
09 01          Usage (Pointer)                       18
91 02          Output (Data,Var,Abs)                 20
09 01          Usage (Pointer)                       22
a1 00          Collection (Physical)                 24
05 09            Usage Page (Button)                 26
19 01            Usage Minimum (1)                   28
29 08            Usage Maximum (8)                   30
15 00            Logical Minimum (0)                 32
25 01            Logical Maximum (1)                 34
75 01            Report Size (1)                     36
95 08            Report Count (8)                    38
81 02            Input (Data,Var,Abs)                40   ← the 8 gears, byte 0 of each report
75 00            Report Size (0)                     42
95 00            Report Count (0)                    44
81 01            Input (Cnst,Arr,Abs)                46
05 01            Usage Page (Generic Desktop)        48
09 00            Usage (Undefined)                   50   ← off-spec
09 00            Usage (Undefined)                   52   ← off-spec
16 00 00         Logical Minimum (0)                 54
26 ff 7f         Logical Maximum (32767)             57
75 10            Report Size (16)                    60
95 02            Report Count (2)                    62
81 02            Input (Data,Var,Abs)                64   ← byte 65 → 0x01 (Constant)
c0             End Collection                        66
c0           End Collection                          67
```

Input report = 8 bits (buttons) + 0 (padding) + 32 bits (phantom axes) = 40 bits = 5 bytes,
matching the captured reports exactly.

---

## Appendix B — approaches tried and rejected

**Keep Game Pad, remap onto A/B/X/Y/LB/RB/L3/R3.** All 8 gears work and Steam Input sees them,
but leaves gamepad codes on the shifter. Rejected: those codes are already spoken for by other
sim hardware.

**Keep Game Pad, remap onto LB/RB/LT/RT/Back/Start/L3/R3.** Avoids the four face buttons, the
highest-contention codes. Still gamepad codes. Rejected for the same reason.

Both of the above required replacing the Button `Usage Minimum`/`Maximum` range with eight
explicit usages, growing the descriptor from 68 to 80 bytes. HID-BPF supports resizing (the
fixup returns the new length), so they were workable — just not wanted.

**`BTN_TRIGGER_HAPPY` block (`0x2c0`+).** The genuinely collision-free range, used by button
boxes for extra inputs and reachable via HID Buttons 17+. Rejected: a device reporting only
those codes is unlikely to be classified as either a joystick or a gamepad by SDL.

**Custom kernel module with `.report_fixup`.** Rejected: would require MOK enrolment under
Secure Boot. HID-BPF achieves the same with no signing.

**`SDL_GAMECONTROLLER_IGNORE_DEVICES=0x4785/0x7353` as a per-game Steam launch option.**
Rejected as a permanent fix because it is per-game. When tested it also appeared to make the
shifter invisible to the game, which led to a working assumption that the game enumerated only
GameController devices — and to two rejected gamepad-based designs above. **That assumption was
wrong.** The final joystick configuration works correctly in BeamNG, which reads it by plain
button number. The earlier test result was misleading, most likely because Steam's stale
mapping (section 7) was still in play at the time.

---

## Appendix C — gotchas worth remembering

**`jstest` blocks forever.** `joydev` only emits events on *change*, so with the shifter in
neutral `jstest` sits in `read()` and never returns. Piping to `head` does not help — it never
writes again, so it never receives `SIGPIPE`. Always wrap it in `timeout` with stdin from
`/dev/null`.

**`/dev/input/by-id/*ODDOR*-joystick` matches two nodes.** `*-event-joystick` is the evdev node
and sorts first; the legacy `js` node is `*-joystick`. Filtering out `event` matters, or
`jstest` reports nonsense (`Joystick (Unknown) has 2 axes and 2 buttons`).

**Steam rewrites `config.vdf` on exit.** Any edit must be made with Steam fully closed.

**Enumeration warning.** The device sometimes fails its first enumeration attempt at low speed
(`device descriptor read/64, error -32`) before succeeding at full speed, and
`input irq status -75` (`EOVERFLOW`) has been seen during re-enumeration. Both appear benign and
the device streams flawlessly once up — but if it ever drops out mid-session, suspect the cable
or port.
