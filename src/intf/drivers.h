#ifndef DRIVERS_H
#define DRIVERS_H

#include <stdint.h>

#define DRIVER_NAME_SIZE 32

struct DriverPciDevice {
    int present;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

struct DriverStatus {
    int pci_devices;
    int network_devices;
    int ethernet_devices;
    int wireless_devices;
    int storage_devices;
    int usb_controllers;
    int uhci_controllers;
    int ohci_controllers;
    int ehci_controllers;
    int xhci_controllers;
    int ide_devices;
    int sata_devices;
    int nvme_devices;
    int vga_text_loaded;
    int ps2_keyboard_loaded;
    int pit_timer_loaded;
    int rtl8139_loaded;
    int e1000_loaded;
    int rtl8111_loaded;
    int mt7922_loaded;
    int rtl8822ce_loaded;
    int usb_hub_stack_modeled;
    int lenovo_14w_profile_suggested;
    int storage_driver_loaded;
    int wireless_driver_loaded;
    struct DriverPciDevice first_network;
    struct DriverPciDevice first_wireless;
    struct DriverPciDevice first_storage;
    struct DriverPciDevice first_usb;
};

void drivers_init();
void drivers_rescan();
struct DriverStatus drivers_get_status();
int drivers_driver_count(char* subsystem);
char* drivers_driver_name(char* subsystem, int index);
char* drivers_selected_driver(char* subsystem);
char* drivers_recommended_driver(char* subsystem);
int drivers_select_driver(char* subsystem, char* name);
char* drivers_storage_state();
char* drivers_wireless_state();
char* drivers_core_state();

#endif
