# CastleOS

CastleOS is a small 64-bit hobby operating system with a shell, an installer, and a growing set of device and network commands.

## Contents
- [Quick Start](#quick-start)
- [Build](#build)
- [Run](#run)
- [Shell Commands](#shell-commands)
- [Current Status](#current-status)
- [Notes](#notes)

## Quick Start
1. Start the build container:
   ```bash
   docker run --rm -it -v $(pwd):/root/env castleos-buildenv
   ```
2. Build the kernel and ISO:
   ```bash
   make build-x86_64
   ```
3. Boot it in QEMU:
   ```bash
   qemu-system-x86_64 -drive file=dist/x86_64/CastleOS.iso,file.locking=off,format=raw,media=cdrom,readonly=on -boot d -netdev user,id=n0 -device rtl8139,netdev=n0
   ```

## Build
The repo is built inside the Docker image named `castleos-buildenv`.

## Run
The RTL8139 device is the current working QEMU NIC for packet networking.

```bash
qemu-system-x86_64 -drive file=dist/x86_64/CastleOS.iso,file.locking=off,format=raw,media=cdrom,readonly=on -boot d -netdev user,id=n0 -device rtl8139,netdev=n0
```

## Shell Commands
- `help`
- `install`
- `net status`
- `net dhcp`
- `net static <ip> <netmask> <gateway>`
- `ping <ipv4>`
- `wifi status`
- `wifi scan`
- `wifi drivers`
- `drivers status`
- `disk status`
- `ssh status`
- `shutdown`

## Current Status
- The shell boots and responds.
- `install` opens the install flow.
- `net` works with the QEMU RTL8139 path.
- `ping 10.0.2.2` is the best quick network test.
- `e1000` is detected, but its packet driver still does not bind.
- Real WiFi, real SSH transport, and real ATA/AHCI/NVMe storage drivers still need more work.

## Notes
- External ping over QEMU user networking can be limited.
- Bridged networking is a better bet if you want to reach beyond the guest.
- Some install targets are still modeled until native storage drivers land.
