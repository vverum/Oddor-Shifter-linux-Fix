# ZSC ODDOR-GEAR — Linux fix

Fix for ODOR sim racing H shifter (cheap AliExpress shifter).
It also changed it to a joystick with buttons 1 to 8 instead of a gamepad.

I don't know shit about sys programming, so most work was done by claude-opus-5 and checked by me (LGTM).

## Install

```bash
sudo bash install.sh      # idempotent, safe to re-run
sudo bash uninstall.sh    # removes it again
```

## What it does

| Layer | Problem | Fix |
|---|---|---|
| 1 | Device claims `bDeviceClass=0xFF`, so `usbhid` never binds and no input device appears | Writes the VID/PID to `usbhid`'s `new_id` from a systemd unit at boot |
| 2 | Descriptor says `Game Pad`, so gears 3 and 6 land on codes SDL and Steam discard; also declares two phantom axes stuck at full deflection | HID-BPF program patches two descriptor bytes as the kernel reads them |

Nothing is written to the device — its firmware is untouched. Nothing needs
signing, so Secure Boot and kernel lockdown are not obstacles.

## Files

| File | |
|---|---|
| `install.sh` / `uninstall.sh` | installer and its reverse |
| `0010-ZSC__ODDOR-GEAR.bpf.c` | the HID-BPF program |
| `0010-ZSC__ODDOR-GEAR.bpf.o` | prebuilt; rebuild only if the `.c` changes |
| `FINDINGS.md` | full analysis: how each fault was found, what was rejected, how to rebuild |

## Verify

```bash
timeout 3 jstest --normal /dev/input/by-id/usb-ZSC_ODDOR-GEAR-joystick </dev/null
```

Expected: `has 0 axes () and 8 buttons (Trigger, ThumbBtn, ...)`.
If you see `BtnA`/`BtnB`/`BtnC`, the BPF program did not attach.

Tested on Fedora 44, kernel 7.1.10, with BeamNG.drive via Steam.
