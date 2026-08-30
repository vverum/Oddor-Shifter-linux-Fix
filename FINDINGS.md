# ZSC ODDOR-GEAR — Linux fix

**Device:** ZSC ODDOR-GEAR, USB `4785:7353` — 8-speed H-pattern sim racing shifter
**Host:** Fedora 44, kernel `7.1.10-200.fc44.x86_64`, Secure Boot on, lockdown `[integrity]`
**Used with:** BeamNG.drive via Steam
**Result:** joystick with buttons 1–8, one per gear, no axes

---

## 1. Summary

Out of the box the shifter did nothing on Linux. It enumerated on USB, but the kernel bound no
driver at all, so no input device existed. Once forced to bind, its report descriptor turned out
to be off-spec in two ways that made it misbehave.

The fix has **two layers**, both installed by `install.sh`:

| Layer | What it corrects | Mechanism |
|---|---|---|
| 1 | Kernel binds no driver — no input device exists | `new_id` on `usbhid`, applied at boot by a systemd unit |
| 2 | Wrong button codes, plus a phantom stuck axis | HID-BPF program patching two descriptor bytes |

The hardware itself is flawless — verified by capturing 11,479 reports across every gear
(appendix B). Every fault was in the firmware's USB/HID declarations or on the host side.

Nothing is written to the device. Nothing requires signing, so Secure Boot and kernel lockdown
are not obstacles.

---

## 2. The device

| Field | Value |
|---|---|
| USB ID | `4785:7353` |
| Strings | Manufacturer `ZSC`, Product `ODDOR-GEAR`, Serial empty |
| Firmware rev | `bcdDevice 1.14` |
| Speed | Full Speed (12 Mbps) |
| Device class | `FF / FF / FF` — Vendor Specific |
| Interface 0 | Class `03` (HID), Subclass `01` (Boot), Protocol `05` |
| HID version | `bcdHID 1.01` |
| Endpoints | `0x81` IN and `0x01` OUT, interrupt, 64 bytes, 10 ms |
| Report descriptor | 68 bytes (appendix A) |
| Power | `MaxPower 500mA`, bus powered |

Several fields are off-spec, consistent with firmware validated only against Windows, whose HID
stack is far more permissive: `bInterfaceProtocol 5` is not a valid Boot protocol (only 0/1/2
are), `bcdHID 1.01` is unusual, and a device whose function is defined by its interfaces should
report `bDeviceClass 0x00`, not `0xFF`.

### Report format

Each report is **5 bytes** at ~125 Hz. **Byte 0 is a button bitmask: gear N sets bit N−1.**
Bytes 1–4 are the phantom axes and are always zero.

| Position | Report | Byte 0 |
|---|---|---|
| Neutral | `00 00 00 00 00` | `0000 0000` |
| Gear 1 | `01 00 00 00 00` | `0000 0001` |
| Gear 2 | `02 00 00 00 00` | `0000 0010` |
| Gear 3 | `04 00 00 00 00` | `0000 0100` |
| Gear 4 | `08 00 00 00 00` | `0000 1000` |
| Gear 5 | `10 00 00 00 00` | `0001 0000` |
| Gear 6 | `20 00 00 00 00` | `0010 0000` |
| Gear 7 | `40 00 00 00 00` | `0100 0000` |
| Gear 8 | `80 00 00 00 00` | `1000 0000` |

This format is unchanged by the fix.

---

## 3. Layer 1 — making the kernel bind a driver

### Why nothing happened

The device enumerated and then produced no `input:` line, no `hid-generic` line, and no error.
No `/sys/bus/hid/devices` entry, no `hidraw`, no `/dev/input/event*`. It was the only HID
interface of 15 on the machine with `Driver=[none]`.

`usb_match_one_id_intf()` in `drivers/usb/core/driver.c` enforces this rule:

> The interface class, subclass, protocol and number should never be checked for a match if the
> device class is Vendor Specific, unless the match record specifies the Vendor ID.

`usbhid` matches on interface class alone and names no vendor:

```c
{ .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
  .bInterfaceClass = USB_INTERFACE_CLASS_HID }
```

The device reports `bDeviceClass = 0xFF`. Both conditions hold, so the match is **skipped
entirely** — usbhid is never offered the device. Nothing is logged because nothing failed; the
match simply never happened.

The rule is deliberate. A vendor-specific device declares that its interfaces may not mean what
the standard says, so the kernel refuses to let *generic* drivers claim them. Windows binds
HIDCLASS from the interface descriptor and ignores the device-level class, which is why the same
firmware works there.

Confirmed by correlation across every HID interface on the machine:

| `bDeviceClass` | Driver | Count |
|---|---|---|
| `00` / `ef` | `usbhid` | 14 |
| **`ff`** (ODDOR-GEAR) | **none** | 1 |

Ruled out during diagnosis: usbhid missing or unloaded; HID quirks (`quirks = 0x0`); kernel
cmdline parameters; custom udev rules; `usbguard` (not installed, `authorized = 1`); a userspace
libusb program claiming the interface; a faulty hub or port.

### The fix

Give usbhid a match record that *does* name the vendor:

```bash
echo "4785 7353" > /sys/bus/usb/drivers/usbhid/new_id
```

`new_id` builds an entry equivalent to `USB_DEVICE(vend, prod)`, whose `match_flags` is
`USB_DEVICE_ID_MATCH_DEVICE` = `MATCH_VENDOR | MATCH_PRODUCT` (verified in
`include/linux/usb.h:992`). With `MATCH_VENDOR` set the guard no longer fires and usbhid binds.
Writing to `new_id` also rescans unbound devices, so it takes effect immediately.

`install.sh` makes this permanent with a systemd oneshot unit. A unit rather than a udev rule,
because `new_id` registers against the **driver**, not the device, and is global for the uptime
— one write at boot is race-free and covers hotplug automatically. usbhid is built into the
kernel (`CONFIG_USB_HID=y`), so `modprobe.d` is not an option.

---

## 4. Layer 2 — correcting the report descriptor

With a driver bound, the shifter worked but reported the wrong things. Two descriptor defects,
each fixed by one byte. The program is `0010-ZSC__ODDOR-GEAR.bpf.c`.

### Byte 3 — `Usage (Game Pad)` → `Usage (Joystick)`

`hid-input` decides where buttons land from the application collection usage. As a Game Pad,
Button N maps to `BTN_GAMEPAD + (N−1)` — a block containing two legacy holes, `BTN_C` (`0x132`)
and `BTN_Z` (`0x135`), which are outside the standard gamepad layout in
`Documentation/input/gamepad.rst`. SDL's GameController layer and Steam Input have no slot for
them and silently discard those inputs, so gears 3 and 6 registered nowhere.

As a Joystick the mapping becomes `BTN_JOYSTICK + (N−1)`: eight contiguous, all-valid codes.

| Gear | evdev code | Kernel name | Seen by SDL / Steam / game |
|---|---|---|---|
| 1 | `0x120` | `BTN_TRIGGER` | Button 1 |
| 2 | `0x121` | `BTN_THUMB` | Button 2 |
| 3 | `0x122` | `BTN_THUMB2` | Button 3 |
| 4 | `0x123` | `BTN_TOP` | Button 4 |
| 5 | `0x124` | `BTN_TOP2` | Button 5 |
| 6 | `0x125` | `BTN_PINKIE` | Button 6 |
| 7 | `0x126` | `BTN_BASE` | Button 7 |
| 8 | `0x127` | `BTN_BASE2` | Button 8 |

(Constants verified in `include/uapi/linux/input-event-codes.h`, lines 366–391.)

Those names are Linux's historical labels for the joystick block. Every layer above evdev —
`joydev`, SDL, Steam, the game — refers to them by **number**, so they present as
**Buttons 1–8**. No gamepad codes remain on the device, so it cannot contend with a wheel or pad
over A/B/X/Y/LB/RB.

Declaring a joystick is also more accurate: an H-pattern shifter is not a gamepad, and real sim
shifters (Logitech G27, Thrustmaster TH8A) present exactly this way.

### Byte 65 — `Input (Data,Var,Abs)` → `Input (Constant)`

The descriptor declares two 16-bit fields with `Usage (0)` (undefined, off-spec) and logical
range 0–32767, hardwired to zero in every report. The kernel collapsed them into one axis:

```
Event code 40 (ABS_MISC)   Value 0   Min 0   Max 32767
```

A value of `0` on a `0..32767` range is the axis **minimum** — i.e. permanently fully deflected,
which testers and games show as a stuck stick. Marking the Input item Constant turns it into
padding, so no axis is created. The report size is unchanged and no data is lost: all 11,479
captured reports had those four bytes zero.

Result: a clean **8-button, 0-axis** joystick.

---

## 5. How the HID-BPF program works

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

### It is not a driver

The object is eBPF bytecode (`ELF 64-bit LSB relocatable, eBPF`), not a kernel module — it never
appears in `lsmod`. The device is driven by stock `usbhid` + `hid-generic`, both built into the
kernel. The program never touches hardware, never sees a report, and binds to nothing; it runs
once per enumeration, edits two bytes, and is done. Upstream's term for this is a **quirk**.

Consequences: no module signing (so Secure Boot and lockdown `[integrity]` are irrelevant); the
BPF verifier proves memory safety and termination before the kernel will load it; it is portable
across kernel versions via BTF/CO-RE; and no daemon runs.

### The three parts

**`HID_BPF_CONFIG(HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, 0x4785, 0x7353))`** — metadata compiled
into the object telling `udev-hid-bpf` which device this is for.

**`probe()`** — a safety gate. The loader runs it first and only attaches if it returns 0:

```c
if (ctx->rdesc_size != 68)        → refuse   /* not this device */
if (ctx->rdesc[3]  != 0x05)       → refuse   /* already patched */
if (ctx->rdesc[65] != 0x02)       → refuse   /* already patched */
```

It therefore cannot double-apply, nor corrupt a device whose descriptor differs from the one
captured here.

**`hid_fix_rdesc_oddor_gear()`** — the hook, attached via `SEC("struct_ops/hid_rdesc_fixup")`.
Writes the two bytes and returns 0, meaning the descriptor size is unchanged.

### How it loads

`udev-hid-bpf install` copies the object to `/etc/udev-hid-bpf/` and writes a udev rule in
`/etc/udev/rules.d/`. Whenever a HID device with that VID/PID appears, udev runs
`udev-hid-bpf add`, which executes `probe()` and attaches the program — on every plug-in and
every boot.

### What it does not touch

The report data path. Byte 0 is still the bitmask with gear N setting bit N−1; only the meaning
the kernel assigns to those bits changes. No per-event processing, no added latency.

---

## 6. Install, verify, remove

```bash
sudo bash install.sh      # applies both layers; idempotent, safe to re-run
sudo bash uninstall.sh    # removes both layers
```

Ordering handles itself: the unit writes `new_id` → usbhid binds → the bind emits a HID `add`
uevent → `81-hid-bpf.rules` attaches the BPF program.

### Verify

```bash
systemctl is-enabled oddor-gear-usbhid.service   # enabled
ls /etc/udev-hid-bpf/                            # 0010-ZSC__ODDOR-GEAR.bpf.o
timeout 3 jstest --normal /dev/input/by-id/usb-ZSC_ODDOR-GEAR-joystick </dev/null
```

Expected:

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

## 7. Rebuilding from source

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

`hid_bpf.h` is byte-identical between the installed `udev-hid-bpf` 2.2.0-20251121 and upstream
master, so building against master's headers matches the installed loader. The default
(non-`HID_BPF_TRACING`) path uses `SEC("struct_ops/hid_rdesc_fixup")`, the modern API, correct
for kernel 7.1.

---

## Appendix A — report descriptor

Original, 68 bytes, read with `usbhid-dump -d 4785:7353 -e descriptor`. Patched bytes marked.

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

## Appendix B — hardware verification

Captured by reading endpoint `0x81` directly via libusb while shifting through every gear —
possible before any driver was bound, which is how the hardware was cleared early.

- **11,479 reports over 91.8 s**, all 5 bytes, streaming at ~125 Hz
- Each gear held steady for **438 consecutive identical reports** across a 5 s hold — a true
  continuous hold, matching the expected behaviour of an H-pattern shifter
- Zero dropped, corrupt or spurious reports
- Only the 9 expected values (neutral + 8 gears) ever appeared; no chatter, no bounce

The hardware is not at fault for anything in this document.

---

## Appendix C — approaches considered and rejected

**Keep Game Pad, remap onto A/B/X/Y/LB/RB/L3/R3.** All 8 gears work and Steam Input sees them,
but leaves gamepad codes on the shifter. Rejected: those codes are already spoken for by other
sim hardware.

**Keep Game Pad, remap onto LB/RB/LT/RT/Back/Start/L3/R3.** Avoids the four face buttons, the
highest-contention codes. Still gamepad codes. Rejected for the same reason.

Both required replacing the Button `Usage Minimum`/`Maximum` range with eight explicit usages,
growing the descriptor from 68 to 80 bytes. HID-BPF supports resizing (the fixup returns the new
length), so they were workable — just not wanted.

**`BTN_TRIGGER_HAPPY` block (`0x2c0`+).** The genuinely collision-free range, used by button
boxes for extra inputs and reachable via HID Buttons 17+. Rejected: a device reporting only
those codes is unlikely to be classified as either a joystick or a gamepad by SDL.

**Custom kernel module with `.report_fixup`.** Rejected: would require MOK enrolment under
Secure Boot. HID-BPF achieves the same with no signing.

**Rewriting the BPF program in Rust.** Not practical. HID-BPF uses `struct_ops` plus kfuncs plus
BTF matching kernel types, which is where the C/libbpf toolchain does heavy lifting that Rust
BPF tooling does not replicate; `bpfel-unknown-none` is also tier 3, with no precompiled `core`
on stable. The payoff would be nil — the program is two byte-writes and three equality checks,
with no memory management, concurrency or parsing, and the BPF verifier already provides
stronger guarantees than the compiler would. Note the loader, `udev-hid-bpf`, *is* Rust;
upstream deliberately splits Rust loader / C programs.

**`SDL_GAMECONTROLLER_IGNORE_DEVICES=0x4785/0x7353` as a per-game Steam launch option.**
Rejected as a permanent fix because it is per-game. When tested it also appeared to make the
shifter invisible to the game, which led to a working assumption that the game enumerated only
GameController devices — and to the two rejected gamepad designs above. **That assumption was
wrong.** The final joystick configuration works correctly in BeamNG, which reads it by plain
button number. The earlier result was misleading, most likely because Steam's stale mapping
(appendix D) was still in play at the time.

---

## Appendix D — host-side repairs and non-issues

### Steam's stale mapping (one-off repair, already applied)

Steam had cached a gamepad mapping in `~/.local/share/Steam/config/config.vdf` under
`SDL_GamepadBind`, created while the shifter still presented as a Game Pad:

```
03000000854700005373000001010000,ZSC ODDOR-GEAR,a:b0,b:b1,x:b3,y:b4,leftshoulder:b6,rightshoulder:b7,platform:Linux,
```

(The GUID embeds `8547`/`5373` — `4785`/`7353` byte-swapped.)

Steam applied this over whatever the kernel reported, painting A/B/X/Y/LB/RB on top of plain
buttons. It also lists only six entries — **`b2` and `b5` are absent**, i.e. gears 3 and 6.
Steam had frozen the original broken state and would have kept hiding those gears regardless of
what was fixed underneath.

Repaired by closing Steam completely (it rewrites the file on exit) and deleting that line. A
stray `sudo bash …` line mis-pasted into the same config value was removed at the same time. A
timestamped `config.vdf.bak-*` backup remains in `~/.local/share/Steam/config/`.

**If the symptom ever returns, check here first** — a cached mapping overrides the kernel.

### KDE's gamepad icon (not a fault)

KDE *Settings → Game Controllers* displayed the shifter with gamepad artwork. Queried directly,
SDL — the same library KDE, Steam and BeamNG all use — reports:

```
name    : ZSC ODDOR-GEAR
buttons : 8   axes: 0   hats: 0
SDL_IsGameController = False
SDL_JoystickGetType  = 0 (UNKNOWN)
```

SDL does not classify it as a gamepad, and no mapping exists that could: the GUID appears in
neither `libSDL2` nor `libSDL3`, there is no `gamecontrollerdb` on the system or in the user
config, and `kcm_gamecontroller.so` ships no mappings of its own.

The panel is simply named "Game Controllers", lists every SDL joystick, and falls back to
generic gamepad artwork when `SDL_JoystickGetType` returns `UNKNOWN`. Changing that would need
SDL to report a specific type such as `WHEEL`, which SDL derives from a hardcoded VID/PID list
that is not user-configurable. The stale display cleared after resetting the Settings window.

---

## Appendix E — gotchas

**`jstest` blocks forever.** `joydev` only emits events on *change*, so with the shifter in
neutral `jstest` sits in `read()` and never returns. Piping to `head` does not help — it never
writes again, so it never receives `SIGPIPE`. Always wrap it in `timeout` with stdin from
`/dev/null`.

**`/dev/input/by-id/*ODDOR*-joystick` matches two nodes.** `*-event-joystick` is the evdev node
and sorts first; the legacy `js` node is `*-joystick`. Filter out `event`, or `jstest` reports
nonsense (`Joystick (Unknown) has 2 axes and 2 buttons`).

**Steam rewrites `config.vdf` on exit.** Any edit must be made with Steam fully closed.

**Enumeration warnings.** The device sometimes fails its first enumeration at low speed
(`device descriptor read/64, error -32`) before succeeding at full speed, and
`input irq status -75` (`EOVERFLOW`) has been seen during re-enumeration. Both appear benign and
the device streams flawlessly once up — but if it ever drops out mid-session, suspect the cable
or port.
