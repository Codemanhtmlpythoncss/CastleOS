to start docker run "docker run --rm -it -v $(pwd):/root/env castleos-buildenv"
to build run "make build-x86_64"
to emulate outside of docker run:
"qemu-system-x86_64 -drive file=dist/x86_64/CastleOS.iso,file.locking=off,format=raw,media=cdrom,readonly=on -boot d -netdev user,id=n0 -device rtl8139,netdev=n0"

the current working packet driver still targets the QEMU RTL8139 NIC. use "install", enable networking, then try "net status" and "ping 10.0.2.2".
the shell also boots under "-device e1000,netdev=n0" and detects the Intel 8086:100e NIC, but the e1000 packet driver still does not bind in the current tree.

installer notes:
- "install" now asks for username, password, install target, and networking mode
- install writes a full CastleOS install image into the selected target manifest
- install targets come from the live disk inventory such as /dev/ram0, /dev/sda, and /dev/sda1
- "disk status" and "disk list" show the available install targets and which one is selected
- "disk show <path>" prints detailed location, capacity, and installed image metadata for a target
- "disk rescan" refreshes the PCI-backed disk inventory
- "disk target <path>" changes the install target
- USB host-controller detection now exposes modeled removable install targets such as /dev/usb0 and /dev/usb0p1 when USB hardware is present
- "drivers status" shows detected hardware and the current selected driver profiles
- "drivers list" or "drivers list <network|wifi|storage|usb>" shows available driver choices
- "drivers choose <subsystem> <driver>" selects a specific driver profile
- "drivers auto <subsystem>" selects the recommended driver profile
- "wifi scan" rescans PCI for a wireless controller and shows the recommended driver
- "wifi drivers" lists the available driver choices for the detected wireless controller
- "wifi driver <name>" or "wifi driver auto" selects a wireless driver profile
- "wifi connect <ssid>" saves a WiFi profile and asks for a password
- "wifi status" shows the controller, selected driver, and saved WiFi profile state
- configurable shell commands now support numbered interactive flows with:
  - "net config"
  - "wifi config"
  - "disk config"
  - "drivers config"
- "ssh status" shows the SSH service/config state
- "ssh config" opens a numbered SSH setup flow
- "ssh enable", "ssh disable", "ssh port <port>", "ssh user <name>", "ssh host <host>", and "ssh connect <host> [user]" manage SSH state and profiles
- "shutdown" powers the machine off in common emulators

controller-backed disk targets are still modeled until native ATA/AHCI/NVMe drivers land, so persistence is only as real as the current target backend. real WiFi still needs a wireless NIC driver. SSH currently exposes shell/configuration state only; a real SSH transport still needs a TCP/IP stack.
