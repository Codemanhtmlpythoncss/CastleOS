#include "drivers.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_SERIAL_BUS 0x0C
#define PCI_CLASS_WIRELESS 0x0D
#define PCI_SUBCLASS_USB 0x03
#define PCI_SUBCLASS_ETHERNET 0x00
#define PCI_SUBCLASS_OTHER_NETWORK 0x80
#define PCI_SUBCLASS_IDE 0x01
#define PCI_SUBCLASS_SATA 0x06
#define PCI_SUBCLASS_NVME 0x08
#define PCI_PROGIF_UHCI 0x00
#define PCI_PROGIF_OHCI 0x10
#define PCI_PROGIF_EHCI 0x20
#define PCI_PROGIF_XHCI 0x30
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139
#define RTL8111_DEVICE_ID_1 0x8111
#define RTL8111_DEVICE_ID_2 0x8168
#define RTL8111_DEVICE_ID_3 0x8211
#define RTL8111_DEVICE_ID_4 0x8411
#define PCI_VENDOR_INTEL 0x8086
#define E1000_DEVICE_ID_82540EM 0x100E
#define E1000_DEVICE_ID_82545EM 0x100F
#define E1000_DEVICE_ID_82543GC 0x1004
#define E1000_DEVICE_ID_82541GI 0x1076
#define MEDIATEK_VENDOR_ID 0x14c3
#define MT7922_DEVICE_ID 0x7922
#define DRIVER_OPTION_MAX_COUNT 4

static struct DriverStatus status;
static char network_driver_options[DRIVER_OPTION_MAX_COUNT][DRIVER_NAME_SIZE];
static int network_driver_option_count = 0;
static char network_selected_driver[DRIVER_NAME_SIZE];
static char network_recommended_driver[DRIVER_NAME_SIZE];
static char storage_driver_options[DRIVER_OPTION_MAX_COUNT][DRIVER_NAME_SIZE];
static int storage_driver_option_count = 0;
static char storage_selected_driver[DRIVER_NAME_SIZE];
static char storage_recommended_driver[DRIVER_NAME_SIZE];
static char usb_driver_options[DRIVER_OPTION_MAX_COUNT][DRIVER_NAME_SIZE];
static int usb_driver_option_count = 0;
static char usb_selected_driver[DRIVER_NAME_SIZE];
static char usb_recommended_driver[DRIVER_NAME_SIZE];

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void copy_string(char* destination, uint32_t destination_size, const char* source) {
    uint32_t i = 0;

    if (destination_size == 0) {
        return;
    }

    while (source && source[i] != '\0' && i + 1 < destination_size) {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

static int strcmp_local(const char* a, const char* b) {
    uint32_t i = 0;

    if (!a || !b) {
        return 1;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return (int) a[i] - (int) b[i];
        }

        i++;
    }

    return (int) a[i] - (int) b[i];
}

static void clear_driver_option_set(char options[][DRIVER_NAME_SIZE], int* count, char* recommended) {
    *count = 0;
    recommended[0] = '\0';

    for (int i = 0; i < DRIVER_OPTION_MAX_COUNT; i++) {
        options[i][0] = '\0';
    }
}

static void add_driver_option(char options[][DRIVER_NAME_SIZE], int* count, const char* name) {
    if (!name || name[0] == '\0' || *count >= DRIVER_OPTION_MAX_COUNT) {
        return;
    }

    copy_string(options[*count], DRIVER_NAME_SIZE, name);
    (*count)++;
}

static int restore_selected_driver(char* selected,
                                   char options[][DRIVER_NAME_SIZE],
                                   int count,
                                   const char* previous) {
    for (int i = 0; i < count; i++) {
        if (strcmp_local(previous, options[i]) == 0) {
            copy_string(selected, DRIVER_NAME_SIZE, previous);
            return 1;
        }
    }

    selected[0] = '\0';
    return 0;
}

static int is_supported_e1000(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != PCI_VENDOR_INTEL) {
        return 0;
    }

    return device_id == E1000_DEVICE_ID_82540EM
        || device_id == E1000_DEVICE_ID_82545EM
        || device_id == E1000_DEVICE_ID_82543GC
        || device_id == E1000_DEVICE_ID_82541GI;
}

static int is_supported_rtl8111(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != RTL8139_VENDOR_ID) {
        return 0;
    }

    return device_id == RTL8111_DEVICE_ID_1
        || device_id == RTL8111_DEVICE_ID_2
        || device_id == RTL8111_DEVICE_ID_3
        || device_id == RTL8111_DEVICE_ID_4;
}

static int is_supported_mt7922(uint16_t vendor_id, uint16_t device_id) {
    return vendor_id == MEDIATEK_VENDOR_ID && device_id == MT7922_DEVICE_ID;
}

static void configure_network_driver_options() {
    clear_driver_option_set(network_driver_options, &network_driver_option_count, network_recommended_driver);

    if (status.network_devices == 0) {
        network_selected_driver[0] = '\0';
        return;
    }

    if (status.rtl8139_loaded || (status.first_network.vendor_id == RTL8139_VENDOR_ID && status.first_network.device_id == RTL8139_DEVICE_ID)) {
        add_driver_option(network_driver_options, &network_driver_option_count, "rtl8139");
        if (status.first_network.vendor_id == RTL8139_VENDOR_ID && status.first_network.device_id == RTL8139_DEVICE_ID) {
            copy_string(network_recommended_driver, DRIVER_NAME_SIZE, "rtl8139");
        }
    }

    if (status.rtl8111_loaded || is_supported_rtl8111(status.first_network.vendor_id, status.first_network.device_id)) {
        add_driver_option(network_driver_options, &network_driver_option_count, "r8169");
        if (is_supported_rtl8111(status.first_network.vendor_id, status.first_network.device_id)) {
            copy_string(network_recommended_driver, DRIVER_NAME_SIZE, "r8169");
        }
    }

    if (status.e1000_loaded || is_supported_e1000(status.first_network.vendor_id, status.first_network.device_id)) {
        add_driver_option(network_driver_options, &network_driver_option_count, "e1000");
        if (is_supported_e1000(status.first_network.vendor_id, status.first_network.device_id)) {
            copy_string(network_recommended_driver, DRIVER_NAME_SIZE, "e1000");
        }
    }

    add_driver_option(network_driver_options, &network_driver_option_count, "generic-ethernet-pci");

    if (network_recommended_driver[0] == '\0') {
        copy_string(network_recommended_driver, DRIVER_NAME_SIZE, "generic-ethernet-pci");
    }
}

static void configure_storage_driver_options() {
    clear_driver_option_set(storage_driver_options, &storage_driver_option_count, storage_recommended_driver);

    if (status.ide_devices > 0) {
        add_driver_option(storage_driver_options, &storage_driver_option_count, "ata-pio");
        copy_string(storage_recommended_driver, DRIVER_NAME_SIZE, "ata-pio");
    }

    if (status.sata_devices > 0) {
        add_driver_option(storage_driver_options, &storage_driver_option_count, "ahci");
        if (storage_recommended_driver[0] == '\0') {
            copy_string(storage_recommended_driver, DRIVER_NAME_SIZE, "ahci");
        }
    }

    if (status.nvme_devices > 0) {
        add_driver_option(storage_driver_options, &storage_driver_option_count, "nvme");
        if (storage_recommended_driver[0] == '\0') {
            copy_string(storage_recommended_driver, DRIVER_NAME_SIZE, "nvme");
        }
    }

    if (status.usb_controllers > 0) {
        add_driver_option(storage_driver_options, &storage_driver_option_count, "usb-storage");
        if (storage_recommended_driver[0] == '\0') {
            copy_string(storage_recommended_driver, DRIVER_NAME_SIZE, "usb-storage");
        }
    }

    if (status.storage_devices > 0 || status.usb_controllers > 0) {
        add_driver_option(storage_driver_options, &storage_driver_option_count, "generic-storage-pci");
    }
}

static void configure_usb_driver_options() {
    clear_driver_option_set(usb_driver_options, &usb_driver_option_count, usb_recommended_driver);

    if (status.usb_controllers == 0) {
        usb_selected_driver[0] = '\0';
        return;
    }

    if (status.xhci_controllers > 0) {
        add_driver_option(usb_driver_options, &usb_driver_option_count, "xhci");
        copy_string(usb_recommended_driver, DRIVER_NAME_SIZE, "xhci");
    }

    if (status.ehci_controllers > 0) {
        add_driver_option(usb_driver_options, &usb_driver_option_count, "ehci");
        if (usb_recommended_driver[0] == '\0') {
            copy_string(usb_recommended_driver, DRIVER_NAME_SIZE, "ehci");
        }
    }

    if (status.ohci_controllers > 0) {
        add_driver_option(usb_driver_options, &usb_driver_option_count, "ohci");
        if (usb_recommended_driver[0] == '\0') {
            copy_string(usb_recommended_driver, DRIVER_NAME_SIZE, "ohci");
        }
    }

    if (status.uhci_controllers > 0) {
        add_driver_option(usb_driver_options, &usb_driver_option_count, "uhci");
        if (usb_recommended_driver[0] == '\0') {
            copy_string(usb_recommended_driver, DRIVER_NAME_SIZE, "uhci");
        }
    }

    add_driver_option(usb_driver_options, &usb_driver_option_count, "generic-usb-host");

    if (usb_recommended_driver[0] == '\0') {
        copy_string(usb_recommended_driver, DRIVER_NAME_SIZE, "generic-usb-host");
    }
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000
        | ((uint32_t) bus << 16)
        | ((uint32_t) slot << 11)
        | ((uint32_t) function << 8)
        | (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void clear_device(struct DriverPciDevice* device) {
    device->present = 0;
    device->vendor_id = 0;
    device->device_id = 0;
    device->bus = 0;
    device->slot = 0;
    device->function = 0;
    device->class_code = 0;
    device->subclass = 0;
    device->prog_if = 0;
}

static void save_device(struct DriverPciDevice* device,
                        uint16_t vendor_id,
                        uint16_t device_id,
                        uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t class_code,
                        uint8_t subclass,
                        uint8_t prog_if) {
    device->present = 1;
    device->vendor_id = vendor_id;
    device->device_id = device_id;
    device->bus = bus;
    device->slot = slot;
    device->function = function;
    device->class_code = class_code;
    device->subclass = subclass;
    device->prog_if = prog_if;
}

static void clear_status() {
    status.pci_devices = 0;
    status.network_devices = 0;
    status.ethernet_devices = 0;
    status.wireless_devices = 0;
    status.storage_devices = 0;
    status.usb_controllers = 0;
    status.uhci_controllers = 0;
    status.ohci_controllers = 0;
    status.ehci_controllers = 0;
    status.xhci_controllers = 0;
    status.ide_devices = 0;
    status.sata_devices = 0;
    status.nvme_devices = 0;
    status.vga_text_loaded = 1;
    status.ps2_keyboard_loaded = 1;
    status.pit_timer_loaded = 1;
    status.rtl8139_loaded = 0;
    status.e1000_loaded = 0;
    status.rtl8111_loaded = 0;
    status.mt7922_loaded = 0;
    status.storage_driver_loaded = 0;
    status.wireless_driver_loaded = 0;
    clear_device(&status.first_network);
    clear_device(&status.first_wireless);
    clear_device(&status.first_storage);
    clear_device(&status.first_usb);
}

static int is_wireless_device(uint8_t class_code, uint8_t subclass) {
    return class_code == PCI_CLASS_WIRELESS
        || (class_code == PCI_CLASS_NETWORK && subclass == PCI_SUBCLASS_OTHER_NETWORK);
}

void drivers_rescan() {
    char previous_network[DRIVER_NAME_SIZE];
    char previous_storage[DRIVER_NAME_SIZE];
    char previous_usb[DRIVER_NAME_SIZE];

    copy_string(previous_network, sizeof(previous_network), network_selected_driver);
    copy_string(previous_storage, sizeof(previous_storage), storage_selected_driver);
    copy_string(previous_usb, sizeof(previous_usb), usb_selected_driver);
    clear_status();

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t id = pci_read32((uint8_t) bus, slot, function, 0x00);
                uint16_t vendor_id = (uint16_t) (id & 0xFFFF);
                uint16_t device_id = (uint16_t) ((id >> 16) & 0xFFFF);
                uint32_t class_info;
                uint8_t class_code;
                uint8_t subclass;
                uint8_t prog_if;

                if (vendor_id == 0xFFFF) {
                    continue;
                }

                class_info = pci_read32((uint8_t) bus, slot, function, 0x08);
                class_code = (uint8_t) ((class_info >> 24) & 0xFF);
                subclass = (uint8_t) ((class_info >> 16) & 0xFF);
                prog_if = (uint8_t) ((class_info >> 8) & 0xFF);
                status.pci_devices++;

                if (class_code == PCI_CLASS_NETWORK || class_code == PCI_CLASS_WIRELESS) {
                    status.network_devices++;

                    if (!status.first_network.present) {
                        save_device(&status.first_network, vendor_id, device_id, (uint8_t) bus, slot, function, class_code, subclass, prog_if);
                    }

                    if (subclass == PCI_SUBCLASS_ETHERNET) {
                        status.ethernet_devices++;
                    }

                    if (is_wireless_device(class_code, subclass)) {
                        status.wireless_devices++;

                        if (!status.first_wireless.present) {
                            save_device(&status.first_wireless, vendor_id, device_id, (uint8_t) bus, slot, function, class_code, subclass, prog_if);
                        }
                    }

                    if (vendor_id == RTL8139_VENDOR_ID && device_id == RTL8139_DEVICE_ID) {
                        status.rtl8139_loaded = 1;
                    } else if (is_supported_rtl8111(vendor_id, device_id)) {
                        status.rtl8111_loaded = 1;
                    } else if (is_supported_e1000(vendor_id, device_id)) {
                        status.e1000_loaded = 1;
                    } else if (is_supported_mt7922(vendor_id, device_id)) {
                        status.mt7922_loaded = 1;
                    }
                }

                if (class_code == PCI_CLASS_STORAGE) {
                    status.storage_devices++;

                    if (!status.first_storage.present) {
                        save_device(&status.first_storage, vendor_id, device_id, (uint8_t) bus, slot, function, class_code, subclass, prog_if);
                    }

                    if (subclass == PCI_SUBCLASS_IDE) {
                        status.ide_devices++;
                    } else if (subclass == PCI_SUBCLASS_SATA) {
                        status.sata_devices++;
                    } else if (subclass == PCI_SUBCLASS_NVME) {
                        status.nvme_devices++;
                    }
                }

                if (class_code == PCI_CLASS_SERIAL_BUS && subclass == PCI_SUBCLASS_USB) {
                    status.usb_controllers++;

                    if (!status.first_usb.present) {
                        save_device(&status.first_usb, vendor_id, device_id, (uint8_t) bus, slot, function, class_code, subclass, prog_if);
                    }

                    if (prog_if == PCI_PROGIF_UHCI) {
                        status.uhci_controllers++;
                    } else if (prog_if == PCI_PROGIF_OHCI) {
                        status.ohci_controllers++;
                    } else if (prog_if == PCI_PROGIF_EHCI) {
                        status.ehci_controllers++;
                    } else if (prog_if == PCI_PROGIF_XHCI) {
                        status.xhci_controllers++;
                    }
                }
            }
        }
    }

    configure_network_driver_options();
    configure_storage_driver_options();
    configure_usb_driver_options();

    if (!restore_selected_driver(network_selected_driver,
                                 network_driver_options,
                                 network_driver_option_count,
                                 previous_network)
        && status.rtl8139_loaded) {
        restore_selected_driver(network_selected_driver,
                                network_driver_options,
                                network_driver_option_count,
                                "rtl8139");
    } else if (!network_selected_driver[0] && status.e1000_loaded) {
        restore_selected_driver(network_selected_driver,
                                network_driver_options,
                                network_driver_option_count,
                                "e1000");
    }

    restore_selected_driver(storage_selected_driver,
                            storage_driver_options,
                            storage_driver_option_count,
                            previous_storage);
    restore_selected_driver(usb_selected_driver,
                            usb_driver_options,
                            usb_driver_option_count,
                            previous_usb);
}

void drivers_init() {
    drivers_rescan();
}

struct DriverStatus drivers_get_status() {
    return status;
}

static int subsystem_equals(char* subsystem, const char* name) {
    return strcmp_local(subsystem, name) == 0;
}

int drivers_driver_count(char* subsystem) {
    if (subsystem_equals(subsystem, "network")) {
        return network_driver_option_count;
    }

    if (subsystem_equals(subsystem, "storage")) {
        return storage_driver_option_count;
    }

    if (subsystem_equals(subsystem, "usb")) {
        return usb_driver_option_count;
    }

    return 0;
}

char* drivers_driver_name(char* subsystem, int index) {
    if (index < 0) {
        return 0;
    }

    if (subsystem_equals(subsystem, "network")) {
        return index < network_driver_option_count ? network_driver_options[index] : 0;
    }

    if (subsystem_equals(subsystem, "storage")) {
        return index < storage_driver_option_count ? storage_driver_options[index] : 0;
    }

    if (subsystem_equals(subsystem, "usb")) {
        return index < usb_driver_option_count ? usb_driver_options[index] : 0;
    }

    return 0;
}

char* drivers_selected_driver(char* subsystem) {
    if (subsystem_equals(subsystem, "network")) {
        return network_selected_driver;
    }

    if (subsystem_equals(subsystem, "storage")) {
        return storage_selected_driver;
    }

    if (subsystem_equals(subsystem, "usb")) {
        return usb_selected_driver;
    }

    return 0;
}

char* drivers_recommended_driver(char* subsystem) {
    if (subsystem_equals(subsystem, "network")) {
        return network_recommended_driver;
    }

    if (subsystem_equals(subsystem, "storage")) {
        return storage_recommended_driver;
    }

    if (subsystem_equals(subsystem, "usb")) {
        return usb_recommended_driver;
    }

    return 0;
}

int drivers_select_driver(char* subsystem, char* name) {
    int count = drivers_driver_count(subsystem);
    char* selected = drivers_selected_driver(subsystem);

    if (!name || !selected) {
        return 0;
    }

    for (int i = 0; i < count; i++) {
        char* option = drivers_driver_name(subsystem, i);

        if (option && strcmp_local(option, name) == 0) {
            copy_string(selected, DRIVER_NAME_SIZE, option);
            return 1;
        }
    }

    return 0;
}

char* drivers_storage_state() {
    if (status.storage_devices == 0) {
        return "no storage controller detected";
    }

    if (status.storage_driver_loaded) {
        return "storage driver loaded";
    }

    if (status.sata_devices > 0) {
        return "AHCI/SATA controller detected, storage driver not implemented yet";
    }

    if (status.ide_devices > 0) {
        return "IDE controller detected, ATA driver not implemented yet";
    }

    if (status.nvme_devices > 0) {
        return "NVMe controller detected, NVMe driver not implemented yet";
    }

    if (status.usb_controllers > 0) {
        return "USB storage path modeled from host controller detection, mass-storage driver not implemented yet";
    }

    return "storage controller detected, no driver for it yet";
}

char* drivers_wireless_state() {
    if (status.wireless_devices == 0) {
        return "no wireless controller detected";
    }

    if (status.wireless_driver_loaded) {
        return "wireless driver loaded";
    }

    return "wireless controller detected, chipset driver and firmware loader not implemented yet";
}

char* drivers_core_state() {
    return "VGA text, PS/2 keyboard, PIT timer";
}
