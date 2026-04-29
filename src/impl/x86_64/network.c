#include <stddef.h>
#include <stdint.h>
#include "network.h"
#include "drivers.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_WIRELESS 0x0D
#define PCI_SUBCLASS_ETHERNET 0x00
#define PCI_SUBCLASS_OTHER_NETWORK 0x80
#define PCI_COMMAND_IO 0x0001
#define PCI_COMMAND_MEMORY 0x0002
#define PCI_COMMAND_BUS_MASTER 0x0004

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139
#define E1000_DEVICE_ID_82540EM 0x100E
#define E1000_DEVICE_ID_82545EM 0x100F
#define E1000_DEVICE_ID_82543GC 0x1004
#define E1000_DEVICE_ID_82541GI 0x1076
#define PCI_VENDOR_INTEL 0x8086
#define PCI_VENDOR_BROADCOM 0x14E4
#define PCI_VENDOR_ATHEROS 0x168C
#define PCI_VENDOR_QUALCOMM_ATHEROS 0x17CB
#define PCI_VENDOR_MEDIATEK 0x14C3
#define PCI_VENDOR_RALINK 0x1814
#define PCI_VENDOR_REALTEK 0x10EC
#define RTL8139_RX_BUFFER_SIZE 8192
#define RTL8139_RX_BUFFER_FULL_SIZE (RTL8139_RX_BUFFER_SIZE + 16 + 1500)
#define RTL8139_TX_BUFFER_COUNT 4
#define RTL8139_TX_BUFFER_SIZE 2048
#define E1000_RX_DESC_COUNT 16
#define E1000_TX_DESC_COUNT 16
#define E1000_BUFFER_SIZE 2048

#define RTL_IDR0 0x00
#define RTL_TSD0 0x10
#define RTL_TSAD0 0x20
#define RTL_RBSTART 0x30
#define RTL_COMMAND 0x37
#define RTL_CAPR 0x38
#define RTL_IMR 0x3C
#define RTL_ISR 0x3E
#define RTL_RCR 0x44
#define RTL_CONFIG1 0x52

#define RTL_CMD_BUFE 0x01
#define RTL_CMD_TE 0x04
#define RTL_CMD_RE 0x08
#define RTL_CMD_RST 0x10

#define E1000_IOADDR 0x00
#define E1000_IODATA 0x04
#define E1000_REG_CTRL 0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD 0x0014
#define E1000_REG_RCTL 0x0100
#define E1000_REG_TCTL 0x0400
#define E1000_REG_TIPG 0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH 0x2810
#define E1000_REG_RDT 0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH 0x3810
#define E1000_REG_TDT 0x3818
#define E1000_REG_IMC 0x00D8
#define E1000_REG_RAL0 0x5400
#define E1000_REG_RAH0 0x5404

#define E1000_CTRL_RST 0x04000000
#define E1000_EERD_START 0x00000001
#define E1000_EERD_DONE 0x00000010
#define E1000_RCTL_EN 0x00000002
#define E1000_RCTL_BAM 0x00008000
#define E1000_RCTL_SECRC 0x04000000
#define E1000_TCTL_EN 0x00000002
#define E1000_TCTL_PSP 0x00000008
#define E1000_TCTL_CT_SHIFT 4
#define E1000_TCTL_COLD_SHIFT 12
#define E1000_TX_CMD_EOP 0x01
#define E1000_TX_CMD_IFCS 0x02
#define E1000_TX_CMD_RS 0x08
#define E1000_TX_STATUS_DD 0x01
#define E1000_RX_STATUS_DD 0x01

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP 0x0806
#define IPV4_PROTOCOL_ICMP 1
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define NETWORK_PACKET_DRIVER_NONE 0
#define NETWORK_PACKET_DRIVER_RTL8139 1
#define NETWORK_PACKET_DRIVER_E1000 2

struct E1000TxDescriptor {
    uint64_t address;
    uint16_t length;
    uint8_t cso;
    uint8_t command;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

struct E1000RxDescriptor {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

static struct NetworkStatus status;
static uint16_t rtl_io_base;
static uint16_t rtl_rx_offset;
static uint8_t rtl_current_tx;
static uint16_t e1000_io_base;
static volatile uint8_t* e1000_mmio_base;
static uint8_t packet_driver_kind;
static uint8_t e1000_rx_index;
static uint8_t e1000_tx_index;
static uint32_t local_ip;
static uint32_t local_netmask;
static uint32_t local_gateway;
static char wifi_driver_options[NETWORK_MAX_WIFI_DRIVER_OPTIONS][NETWORK_MAX_WIFI_DRIVER_NAME];

static uint8_t rtl_rx_buffer[RTL8139_RX_BUFFER_FULL_SIZE] __attribute__((aligned(4)));
static uint8_t rtl_tx_buffers[RTL8139_TX_BUFFER_COUNT][RTL8139_TX_BUFFER_SIZE] __attribute__((aligned(4)));
static struct E1000RxDescriptor e1000_rx_descriptors[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static struct E1000TxDescriptor e1000_tx_descriptors[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t e1000_rx_buffers[E1000_RX_DESC_COUNT][E1000_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t e1000_tx_buffers[E1000_TX_DESC_COUNT][E1000_BUFFER_SIZE] __attribute__((aligned(16)));

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile ("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile ("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void memory_copy(uint8_t* destination, uint8_t* source, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        destination[i] = source[i];
    }
}

static void memory_set(uint8_t* destination, uint8_t value, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        destination[i] = value;
    }
}

static int memory_equals(uint8_t* a, uint8_t* b, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }

    return 1;
}

static void copy_string(char* destination, size_t destination_size, char* source) {
    size_t i = 0;

    if (destination_size == 0) {
        return;
    }

    while (source[i] != '\0' && i + 1 < destination_size) {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

static int strcmp_local(char* a, char* b) {
    size_t i = 0;

    if (!a || !b) {
        return 1;
    }

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return a[i] - b[i];
        }

        i++;
    }

    return a[i] - b[i];
}

static int string_equals(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }

        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static void clear_wifi_driver_options() {
    status.wifi_driver_option_count = 0;

    for (int i = 0; i < NETWORK_MAX_WIFI_DRIVER_OPTIONS; i++) {
        wifi_driver_options[i][0] = '\0';
    }
}

static void add_wifi_driver_option(char* name) {
    if (!name || name[0] == '\0' || status.wifi_driver_option_count >= NETWORK_MAX_WIFI_DRIVER_OPTIONS) {
        return;
    }

    copy_string(wifi_driver_options[status.wifi_driver_option_count],
                sizeof(wifi_driver_options[status.wifi_driver_option_count]),
                name);
    status.wifi_driver_option_count++;
}

static int is_wireless_device(uint8_t class_code, uint8_t subclass) {
    return class_code == PCI_CLASS_WIRELESS
        || (class_code == PCI_CLASS_NETWORK && subclass == PCI_SUBCLASS_OTHER_NETWORK);
}

static void configure_wifi_driver_options(uint16_t vendor_id) {
    clear_wifi_driver_options();
    status.wifi_recommended_driver[0] = '\0';

    if (!status.wifi_hardware_present) {
        return;
    }

    if (vendor_id == PCI_VENDOR_ATHEROS || vendor_id == PCI_VENDOR_QUALCOMM_ATHEROS) {
        add_wifi_driver_option("ath9k");
        add_wifi_driver_option("ath10k");
        add_wifi_driver_option("generic-pci-wifi");
        copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "ath9k");
        return;
    }

    if (vendor_id == PCI_VENDOR_INTEL) {
        add_wifi_driver_option("iwlwifi");
        add_wifi_driver_option("generic-pci-wifi");
        copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "iwlwifi");
        return;
    }

    if (vendor_id == PCI_VENDOR_BROADCOM) {
        add_wifi_driver_option("b43");
        add_wifi_driver_option("brcmsmac");
        add_wifi_driver_option("generic-pci-wifi");
        copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "b43");
        return;
    }

    if (vendor_id == PCI_VENDOR_MEDIATEK || vendor_id == PCI_VENDOR_RALINK) {
        add_wifi_driver_option("mt76");
        add_wifi_driver_option("generic-pci-wifi");
        copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "mt76");
        return;
    }

    if (vendor_id == PCI_VENDOR_REALTEK) {
        add_wifi_driver_option("rtlwifi");
        add_wifi_driver_option("rtw88");
        add_wifi_driver_option("generic-pci-wifi");
        copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "rtlwifi");
        return;
    }

    add_wifi_driver_option("generic-pci-wifi");
    add_wifi_driver_option("manual-probe");
    copy_string(status.wifi_recommended_driver, sizeof(status.wifi_recommended_driver), "generic-pci-wifi");
}

static uint16_t read_le16(uint8_t* data) {
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint16_t read_be16(uint8_t* data) {
    return ((uint16_t) data[0] << 8) | (uint16_t) data[1];
}

static uint32_t read_be32(uint8_t* data) {
    return ((uint32_t) data[0] << 24)
        | ((uint32_t) data[1] << 16)
        | ((uint32_t) data[2] << 8)
        | (uint32_t) data[3];
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t) ((value >> 8) & 0xFF);
    data[1] = (uint8_t) (value & 0xFF);
}

static void write_be32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t) ((value >> 24) & 0xFF);
    data[1] = (uint8_t) ((value >> 16) & 0xFF);
    data[2] = (uint8_t) ((value >> 8) & 0xFF);
    data[3] = (uint8_t) (value & 0xFF);
}

static uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t) a << 24) | ((uint32_t) b << 16) | ((uint32_t) c << 8) | (uint32_t) d;
}

static int parse_ipv4(char* text, uint32_t* ip) {
    uint32_t parts[4] = {0, 0, 0, 0};
    int part = 0;
    int saw_digit = 0;

    if (!text || !ip) {
        return 0;
    }

    for (size_t i = 0; ; i++) {
        char character = text[i];

        if (character >= '0' && character <= '9') {
            saw_digit = 1;
            parts[part] = parts[part] * 10 + (uint32_t) (character - '0');

            if (parts[part] > 255) {
                return 0;
            }

            continue;
        }

        if (character == '.' && saw_digit && part < 3) {
            part++;
            saw_digit = 0;
            continue;
        }

        if (character == '\0' && saw_digit && part == 3) {
            *ip = make_ip((uint8_t) parts[0], (uint8_t) parts[1], (uint8_t) parts[2], (uint8_t) parts[3]);
            return 1;
        }

        return 0;
    }
}

static void ip_to_string(uint32_t ip, char* output, size_t output_size) {
    size_t index = 0;

    for (int part = 0; part < 4; part++) {
        uint8_t value = (uint8_t) ((ip >> (24 - part * 8)) & 0xFF);
        char digits[3];
        size_t digit_count = 0;

        if (part > 0 && index + 1 < output_size) {
            output[index++] = '.';
        }

        if (value == 0) {
            if (index + 1 < output_size) {
                output[index++] = '0';
            }
            continue;
        }

        while (value > 0 && digit_count < sizeof(digits)) {
            digits[digit_count++] = (char) ('0' + (value % 10));
            value /= 10;
        }

        while (digit_count > 0 && index + 1 < output_size) {
            output[index++] = digits[--digit_count];
        }
    }

    if (output_size > 0) {
        output[index < output_size ? index : output_size - 1] = '\0';
    }
}

static uint16_t checksum16(uint8_t* data, uint16_t length) {
    uint32_t sum = 0;

    for (uint16_t i = 0; i < length; i += 2) {
        uint16_t word = (uint16_t) data[i] << 8;

        if (i + 1 < length) {
            word |= data[i + 1];
        }

        sum += word;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t) ~sum;
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

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000
        | ((uint32_t) bus << 16)
        | ((uint32_t) slot << 11)
        | ((uint32_t) function << 8)
        | (offset & 0xFC);

    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
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

static uint32_t e1000_read_register(uint16_t reg) {
    if (e1000_io_base != 0) {
        outl((uint16_t) (e1000_io_base + E1000_IOADDR), reg);
        return inl((uint16_t) (e1000_io_base + E1000_IODATA));
    }

    if (e1000_mmio_base != 0) {
        return *(volatile uint32_t*) (e1000_mmio_base + reg);
    }

    return 0;
}

static void e1000_write_register(uint16_t reg, uint32_t value) {
    if (e1000_io_base != 0) {
        outl((uint16_t) (e1000_io_base + E1000_IOADDR), reg);
        outl((uint16_t) (e1000_io_base + E1000_IODATA), value);
        return;
    }

    if (e1000_mmio_base != 0) {
        *(volatile uint32_t*) (e1000_mmio_base + reg) = value;
    }
}

static void set_ip_config(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    local_ip = ip;
    local_netmask = netmask;
    local_gateway = gateway;
    ip_to_string(local_ip, status.ip, sizeof(status.ip));
    ip_to_string(local_netmask, status.netmask, sizeof(status.netmask));
    ip_to_string(local_gateway, status.gateway, sizeof(status.gateway));
}

static void clear_network_status() {
    status.initialized = 1;
    status.enabled = 0;
    status.device_count = 0;
    status.packet_driver_ready = 0;
    status.packets_sent = 0;
    status.packets_received = 0;
    status.mode = NETWORK_MODE_DOWN;
    status.packet_driver_name[0] = '\0';
    status.ip[0] = '\0';
    status.netmask[0] = '\0';
    status.gateway[0] = '\0';
    status.wifi_hardware_present = 0;
    status.wifi_profile_saved = 0;
    status.wifi_connected = 0;
    status.wifi_driver_selected = 0;
    status.wifi_ssid[0] = '\0';
    status.wifi_selected_driver[0] = '\0';
    status.wifi_recommended_driver[0] = '\0';
    memory_set(status.mac, 0, sizeof(status.mac));
    status.first_device.vendor_id = 0;
    status.first_device.device_id = 0;
    status.first_device.bus = 0;
    status.first_device.slot = 0;
    status.first_device.function = 0;
    status.first_device.class_code = 0;
    status.first_device.subclass = 0;
    status.first_device.prog_if = 0;
    status.first_wifi_device.vendor_id = 0;
    status.first_wifi_device.device_id = 0;
    status.first_wifi_device.bus = 0;
    status.first_wifi_device.slot = 0;
    status.first_wifi_device.function = 0;
    status.first_wifi_device.class_code = 0;
    status.first_wifi_device.subclass = 0;
    status.first_wifi_device.prog_if = 0;
    clear_wifi_driver_options();
    rtl_io_base = 0;
    rtl_rx_offset = 0;
    rtl_current_tx = 0;
    e1000_io_base = 0;
    e1000_mmio_base = 0;
    packet_driver_kind = NETWORK_PACKET_DRIVER_NONE;
    e1000_rx_index = 0;
    e1000_tx_index = 0;
    local_ip = 0;
    local_netmask = 0;
    local_gateway = 0;
}

void network_wifi_rescan() {
    struct NetworkStatus existing = status;

    status.wifi_hardware_present = 0;
    status.first_wifi_device.vendor_id = 0;
    status.first_wifi_device.device_id = 0;
    status.first_wifi_device.bus = 0;
    status.first_wifi_device.slot = 0;
    status.first_wifi_device.function = 0;
    status.first_wifi_device.class_code = 0;
    status.first_wifi_device.subclass = 0;
    status.first_wifi_device.prog_if = 0;
    clear_wifi_driver_options();
    status.wifi_recommended_driver[0] = '\0';

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

                if (!is_wireless_device(class_code, subclass)) {
                    continue;
                }

                status.wifi_hardware_present = 1;
                status.first_wifi_device.vendor_id = vendor_id;
                status.first_wifi_device.device_id = device_id;
                status.first_wifi_device.bus = (uint8_t) bus;
                status.first_wifi_device.slot = slot;
                status.first_wifi_device.function = function;
                status.first_wifi_device.class_code = class_code;
                status.first_wifi_device.subclass = subclass;
                status.first_wifi_device.prog_if = prog_if;
                configure_wifi_driver_options(vendor_id);

                if (existing.wifi_driver_selected) {
                    for (int i = 0; i < status.wifi_driver_option_count; i++) {
                        if (strcmp_local(existing.wifi_selected_driver, wifi_driver_options[i]) == 0) {
                            status.wifi_driver_selected = 1;
                            copy_string(status.wifi_selected_driver,
                                        sizeof(status.wifi_selected_driver),
                                        existing.wifi_selected_driver);
                            return;
                        }
                    }
                }

                status.wifi_driver_selected = 0;
                status.wifi_selected_driver[0] = '\0';

                if (status.wifi_recommended_driver[0] != '\0') {
                    network_wifi_select_driver(status.wifi_recommended_driver);
                }
                return;
            }
        }
    }

    status.wifi_driver_selected = 0;
    status.wifi_selected_driver[0] = '\0';
}

static int rtl8139_init(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar0 = pci_read32(bus, slot, function, 0x10);
    uint32_t command = pci_read32(bus, slot, function, 0x04);

    if ((bar0 & 0x01) == 0) {
        return 0;
    }

    rtl_io_base = (uint16_t) (bar0 & 0xFFFC);
    pci_write32(bus, slot, function, 0x04, command | PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);

    outb(rtl_io_base + RTL_CONFIG1, 0x00);
    outb(rtl_io_base + RTL_COMMAND, RTL_CMD_RST);

    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(rtl_io_base + RTL_COMMAND) & RTL_CMD_RST) == 0) {
            break;
        }
    }

    for (uint8_t i = 0; i < NETWORK_MAC_LENGTH; i++) {
        status.mac[i] = inb(rtl_io_base + RTL_IDR0 + i);
    }

    outl(rtl_io_base + RTL_RBSTART, (uint32_t) (uintptr_t) rtl_rx_buffer);
    outw(rtl_io_base + RTL_IMR, 0x0000);
    outl(rtl_io_base + RTL_RCR, 0x0000000F | 0x00000080);
    outb(rtl_io_base + RTL_COMMAND, RTL_CMD_RE | RTL_CMD_TE);
    outw(rtl_io_base + RTL_ISR, 0xFFFF);

    status.packet_driver_ready = 1;
    packet_driver_kind = NETWORK_PACKET_DRIVER_RTL8139;
    copy_string(status.packet_driver_name, sizeof(status.packet_driver_name), "rtl8139");
    return 1;
}

static void e1000_setup_receive_ring() {
    memory_set((uint8_t*) e1000_rx_descriptors, 0, sizeof(e1000_rx_descriptors));
    memory_set((uint8_t*) e1000_rx_buffers, 0, sizeof(e1000_rx_buffers));

    for (uint16_t i = 0; i < E1000_RX_DESC_COUNT; i++) {
        e1000_rx_descriptors[i].address = (uint64_t) (uintptr_t) e1000_rx_buffers[i];
        e1000_rx_descriptors[i].status = 0;
    }

    e1000_write_register(E1000_REG_RDBAL, (uint32_t) (uintptr_t) e1000_rx_descriptors);
    e1000_write_register(E1000_REG_RDBAH, 0);
    e1000_write_register(E1000_REG_RDLEN, sizeof(e1000_rx_descriptors));
    e1000_write_register(E1000_REG_RDH, 0);
    e1000_write_register(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    e1000_write_register(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    e1000_rx_index = 0;
}

static void e1000_setup_transmit_ring() {
    memory_set((uint8_t*) e1000_tx_descriptors, 0, sizeof(e1000_tx_descriptors));
    memory_set((uint8_t*) e1000_tx_buffers, 0, sizeof(e1000_tx_buffers));

    for (uint16_t i = 0; i < E1000_TX_DESC_COUNT; i++) {
        e1000_tx_descriptors[i].address = (uint64_t) (uintptr_t) e1000_tx_buffers[i];
        e1000_tx_descriptors[i].status = E1000_TX_STATUS_DD;
    }

    e1000_write_register(E1000_REG_TDBAL, (uint32_t) (uintptr_t) e1000_tx_descriptors);
    e1000_write_register(E1000_REG_TDBAH, 0);
    e1000_write_register(E1000_REG_TDLEN, sizeof(e1000_tx_descriptors));
    e1000_write_register(E1000_REG_TDH, 0);
    e1000_write_register(E1000_REG_TDT, 0);
    e1000_write_register(E1000_REG_TCTL,
                         E1000_TCTL_EN
                         | E1000_TCTL_PSP
                         | (15 << E1000_TCTL_CT_SHIFT)
                         | (64 << E1000_TCTL_COLD_SHIFT));
    e1000_write_register(E1000_REG_TIPG, 0x0060200A);
    e1000_tx_index = 0;
}

static uint16_t e1000_read_eeprom(uint8_t address) {
    for (uint32_t attempt = 0; attempt < 100000; attempt++) {
        uint32_t data;

        if (attempt == 0) {
            e1000_write_register(E1000_REG_EERD, E1000_EERD_START | ((uint32_t) address << 8));
        }

        data = e1000_read_register(E1000_REG_EERD);
        if ((data & E1000_EERD_DONE) != 0) {
            return (uint16_t) ((data >> 16) & 0xFFFF);
        }
    }

    return 0;
}

static int e1000_init(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t bar0 = pci_read32(bus, slot, function, 0x10);
    uint32_t bar1 = pci_read32(bus, slot, function, 0x14);
    uint32_t command = pci_read32(bus, slot, function, 0x04);
    uint32_t ral;
    uint32_t rah;

    e1000_io_base = 0;
    e1000_mmio_base = 0;

    if ((bar1 & 0x01) != 0) {
        e1000_io_base = (uint16_t) (bar1 & 0xFFFC);
        pci_write32(bus, slot, function, 0x04, command | PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER);
    } else if ((bar0 & 0x01) == 0) {
        uintptr_t mmio_address = (uintptr_t) (bar0 & 0xFFFFFFF0);

        if (mmio_address >= 0x40000000u) {
            return 0;
        }

        e1000_mmio_base = (volatile uint8_t*) mmio_address;
        pci_write32(bus, slot, function, 0x04, command | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER);
    } else {
        return 0;
    }

    e1000_write_register(E1000_REG_IMC, 0xFFFFFFFF);
    e1000_write_register(E1000_REG_CTRL, e1000_read_register(E1000_REG_CTRL) | E1000_CTRL_RST);

    for (uint32_t i = 0; i < 100000; i++) {
        if ((e1000_read_register(E1000_REG_CTRL) & E1000_CTRL_RST) == 0) {
            break;
        }
    }

    e1000_write_register(E1000_REG_IMC, 0xFFFFFFFF);

    ral = e1000_read_register(E1000_REG_RAL0);
    rah = e1000_read_register(E1000_REG_RAH0);

    status.mac[0] = (uint8_t) (ral & 0xFF);
    status.mac[1] = (uint8_t) ((ral >> 8) & 0xFF);
    status.mac[2] = (uint8_t) ((ral >> 16) & 0xFF);
    status.mac[3] = (uint8_t) ((ral >> 24) & 0xFF);
    status.mac[4] = (uint8_t) (rah & 0xFF);
    status.mac[5] = (uint8_t) ((rah >> 8) & 0xFF);

    if (status.mac[0] == 0 && status.mac[1] == 0 && status.mac[2] == 0
        && status.mac[3] == 0 && status.mac[4] == 0 && status.mac[5] == 0) {
        uint16_t word0 = e1000_read_eeprom(0);
        uint16_t word1 = e1000_read_eeprom(1);
        uint16_t word2 = e1000_read_eeprom(2);

        status.mac[0] = (uint8_t) (word0 & 0xFF);
        status.mac[1] = (uint8_t) ((word0 >> 8) & 0xFF);
        status.mac[2] = (uint8_t) (word1 & 0xFF);
        status.mac[3] = (uint8_t) ((word1 >> 8) & 0xFF);
        status.mac[4] = (uint8_t) (word2 & 0xFF);
        status.mac[5] = (uint8_t) ((word2 >> 8) & 0xFF);
    }

    e1000_setup_receive_ring();
    e1000_setup_transmit_ring();

    if (status.mac[0] == 0 && status.mac[1] == 0 && status.mac[2] == 0
        && status.mac[3] == 0 && status.mac[4] == 0 && status.mac[5] == 0) {
        return 0;
    }

    status.packet_driver_ready = 1;
    packet_driver_kind = NETWORK_PACKET_DRIVER_E1000;
    copy_string(status.packet_driver_name, sizeof(status.packet_driver_name), "e1000");
    return 1;
}

static int network_try_driver_for_device(const char* driver_name,
                                         uint16_t vendor_id,
                                         uint16_t device_id,
                                         uint8_t bus,
                                         uint8_t slot,
                                         uint8_t function) {
    if (!driver_name || driver_name[0] == '\0') {
        return 0;
    }

    if (string_equals(driver_name, "rtl8139")) {
        if (vendor_id == RTL8139_VENDOR_ID && device_id == RTL8139_DEVICE_ID) {
            return rtl8139_init(bus, slot, function);
        }

        return 0;
    }

    if (string_equals(driver_name, "e1000")) {
        if (is_supported_e1000(vendor_id, device_id)) {
            return e1000_init(bus, slot, function);
        }

        return 0;
    }

    if (string_equals(driver_name, "generic-ethernet-pci")) {
        if (vendor_id == RTL8139_VENDOR_ID && device_id == RTL8139_DEVICE_ID && rtl8139_init(bus, slot, function)) {
            return 1;
        }

        if (is_supported_e1000(vendor_id, device_id) && e1000_init(bus, slot, function)) {
            return 1;
        }
    }

    return 0;
}

static int network_packet_available() {
    if (!status.packet_driver_ready) {
        return 0;
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_RTL8139) {
        return (inb(rtl_io_base + RTL_COMMAND) & RTL_CMD_BUFE) == 0;
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_E1000) {
        return (e1000_rx_descriptors[e1000_rx_index].status & E1000_RX_STATUS_DD) != 0;
    }

    return 0;
}

int network_send_packet(uint8_t* data, uint16_t length) {
    uint16_t transmit_length = length;

    if (!status.enabled || !status.packet_driver_ready || !data || length == 0 || length > NETWORK_MAX_PACKET_SIZE) {
        return 0;
    }

    if (transmit_length < 60) {
        transmit_length = 60;
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_RTL8139) {
        uint8_t tx = rtl_current_tx;

        memory_set(rtl_tx_buffers[tx], 0, RTL8139_TX_BUFFER_SIZE);
        memory_copy(rtl_tx_buffers[tx], data, length);

        outl(rtl_io_base + RTL_TSAD0 + tx * 4, (uint32_t) (uintptr_t) rtl_tx_buffers[tx]);
        outl(rtl_io_base + RTL_TSD0 + tx * 4, transmit_length);

        rtl_current_tx = (rtl_current_tx + 1) % RTL8139_TX_BUFFER_COUNT;
        status.packets_sent++;
        return 1;
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_E1000) {
        struct E1000TxDescriptor* descriptor = &e1000_tx_descriptors[e1000_tx_index];
        uint8_t tx = e1000_tx_index;

        if ((descriptor->status & E1000_TX_STATUS_DD) == 0 || transmit_length > E1000_BUFFER_SIZE) {
            return 0;
        }

        memory_set(e1000_tx_buffers[tx], 0, E1000_BUFFER_SIZE);
        memory_copy(e1000_tx_buffers[tx], data, length);
        descriptor->length = transmit_length;
        descriptor->cso = 0;
        descriptor->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
        descriptor->status = 0;
        descriptor->css = 0;
        descriptor->special = 0;

        e1000_tx_index = (uint8_t) ((e1000_tx_index + 1) % E1000_TX_DESC_COUNT);
        e1000_write_register(E1000_REG_TDT, e1000_tx_index);

        for (uint32_t attempt = 0; attempt < 100000; attempt++) {
            if ((descriptor->status & E1000_TX_STATUS_DD) != 0) {
                status.packets_sent++;
                return 1;
            }
        }

        return 0;
    }

    return 0;
}

int network_receive_packet(uint8_t* buffer, uint16_t* length) {
    uint16_t packet_status;
    uint16_t packet_length;
    uint16_t payload_length;

    if (!status.enabled || !status.packet_driver_ready || !buffer || !length || !network_packet_available()) {
        return 0;
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_E1000) {
        struct E1000RxDescriptor* descriptor = &e1000_rx_descriptors[e1000_rx_index];
        uint16_t payload_length;

        if ((descriptor->status & E1000_RX_STATUS_DD) == 0) {
            return 0;
        }

        payload_length = descriptor->length;
        if (payload_length == 0 || payload_length > NETWORK_MAX_PACKET_SIZE) {
            descriptor->status = 0;
            e1000_write_register(E1000_REG_RDT, e1000_rx_index);
            e1000_rx_index = (uint8_t) ((e1000_rx_index + 1) % E1000_RX_DESC_COUNT);
            return 0;
        }

        memory_copy(buffer, e1000_rx_buffers[e1000_rx_index], payload_length);
        descriptor->status = 0;
        e1000_write_register(E1000_REG_RDT, e1000_rx_index);
        e1000_rx_index = (uint8_t) ((e1000_rx_index + 1) % E1000_RX_DESC_COUNT);
        *length = payload_length;
        status.packets_received++;
        return 1;
    }

    packet_status = read_le16(&rtl_rx_buffer[rtl_rx_offset]);
    packet_length = read_le16(&rtl_rx_buffer[rtl_rx_offset + 2]);

    if ((packet_status & 0x01) == 0 || packet_length < 4 || packet_length > NETWORK_MAX_PACKET_SIZE + 4) {
        rtl_rx_offset = 0;
        outw(rtl_io_base + RTL_CAPR, 0);
        outw(rtl_io_base + RTL_ISR, 0xFFFF);
        return 0;
    }

    payload_length = packet_length - 4;

    for (uint16_t i = 0; i < payload_length; i++) {
        uint16_t source_index = (uint16_t) ((rtl_rx_offset + 4 + i) % RTL8139_RX_BUFFER_SIZE);
        buffer[i] = rtl_rx_buffer[source_index];
    }

    rtl_rx_offset = (uint16_t) ((rtl_rx_offset + packet_length + 4 + 3) & ~3);

    if (rtl_rx_offset >= RTL8139_RX_BUFFER_SIZE) {
        rtl_rx_offset -= RTL8139_RX_BUFFER_SIZE;
    }

    outw(rtl_io_base + RTL_CAPR, (uint16_t) (rtl_rx_offset - 16));
    outw(rtl_io_base + RTL_ISR, 0x0001);

    *length = payload_length;
    status.packets_received++;
    return 1;
}

static void build_ethernet_header(uint8_t* packet, uint8_t* destination_mac, uint16_t ether_type) {
    memory_copy(packet, destination_mac, NETWORK_MAC_LENGTH);
    memory_copy(packet + 6, status.mac, NETWORK_MAC_LENGTH);
    write_be16(packet + 12, ether_type);
}

static void send_arp_reply(uint8_t* request) {
    uint8_t reply[42];

    build_ethernet_header(reply, request + 22, ETH_TYPE_ARP);
    write_be16(reply + 14, 1);
    write_be16(reply + 16, ETH_TYPE_IPV4);
    reply[18] = 6;
    reply[19] = 4;
    write_be16(reply + 20, 2);
    memory_copy(reply + 22, status.mac, NETWORK_MAC_LENGTH);
    write_be32(reply + 28, local_ip);
    memory_copy(reply + 32, request + 22, NETWORK_MAC_LENGTH);
    memory_copy(reply + 38, request + 28, 4);

    network_send_packet(reply, sizeof(reply));
}

static void process_incoming_arp(uint8_t* packet, uint16_t length) {
    uint16_t operation;
    uint32_t target_ip;

    if (length < 42) {
        return;
    }

    operation = read_be16(packet + 20);
    target_ip = read_be32(packet + 38);

    if (operation == 1 && target_ip == local_ip) {
        send_arp_reply(packet);
    }
}

static int resolve_mac(uint32_t target_ip, uint8_t* resolved_mac) {
    static uint8_t broadcast_mac[NETWORK_MAC_LENGTH] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t packet[42];
    uint8_t received[NETWORK_MAX_PACKET_SIZE];
    uint16_t received_length;

    build_ethernet_header(packet, broadcast_mac, ETH_TYPE_ARP);
    write_be16(packet + 14, 1);
    write_be16(packet + 16, ETH_TYPE_IPV4);
    packet[18] = 6;
    packet[19] = 4;
    write_be16(packet + 20, 1);
    memory_copy(packet + 22, status.mac, NETWORK_MAC_LENGTH);
    write_be32(packet + 28, local_ip);
    memory_set(packet + 32, 0, NETWORK_MAC_LENGTH);
    write_be32(packet + 38, target_ip);

    network_send_packet(packet, sizeof(packet));

    for (uint32_t attempt = 0; attempt < 300000; attempt++) {
        if (!network_receive_packet(received, &received_length)) {
            continue;
        }

        if (received_length < 42 || read_be16(received + 12) != ETH_TYPE_ARP) {
            continue;
        }

        process_incoming_arp(received, received_length);

        if (read_be16(received + 20) == 2
            && read_be32(received + 28) == target_ip
            && read_be32(received + 38) == local_ip) {
            memory_copy(resolved_mac, received + 22, NETWORK_MAC_LENGTH);
            return 1;
        }
    }

    return 0;
}

static int send_icmp_echo(uint32_t target_ip, uint8_t* target_mac, uint16_t sequence) {
    uint8_t packet[74];
    uint16_t ip_total_length = 60;

    memory_set(packet, 0, sizeof(packet));
    build_ethernet_header(packet, target_mac, ETH_TYPE_IPV4);

    packet[14] = 0x45;
    packet[15] = 0;
    write_be16(packet + 16, ip_total_length);
    write_be16(packet + 18, sequence);
    write_be16(packet + 20, 0);
    packet[22] = 64;
    packet[23] = IPV4_PROTOCOL_ICMP;
    write_be32(packet + 26, local_ip);
    write_be32(packet + 30, target_ip);
    write_be16(packet + 24, checksum16(packet + 14, 20));

    packet[34] = ICMP_ECHO_REQUEST;
    packet[35] = 0;
    write_be16(packet + 36, 0);
    write_be16(packet + 38, 0xCA57);
    write_be16(packet + 40, sequence);

    for (uint8_t i = 0; i < 32; i++) {
        packet[42 + i] = (uint8_t) ('A' + (i % 26));
    }

    write_be16(packet + 36, checksum16(packet + 34, 40));
    return network_send_packet(packet, 14 + ip_total_length);
}

static int wait_for_icmp_reply(uint32_t target_ip, uint16_t sequence) {
    uint8_t received[NETWORK_MAX_PACKET_SIZE];
    uint16_t received_length;

    for (uint32_t attempt = 0; attempt < 600000; attempt++) {
        uint16_t ethernet_type;
        uint16_t ip_header_length;
        uint16_t icmp_offset;

        if (!network_receive_packet(received, &received_length)) {
            continue;
        }

        if (received_length < 42) {
            continue;
        }

        ethernet_type = read_be16(received + 12);

        if (ethernet_type == ETH_TYPE_ARP) {
            process_incoming_arp(received, received_length);
            continue;
        }

        if (ethernet_type != ETH_TYPE_IPV4 || received[23] != IPV4_PROTOCOL_ICMP) {
            continue;
        }

        if (read_be32(received + 26) != target_ip || read_be32(received + 30) != local_ip) {
            continue;
        }

        ip_header_length = (uint16_t) ((received[14] & 0x0F) * 4);
        icmp_offset = 14 + ip_header_length;

        if (received_length < icmp_offset + 8) {
            continue;
        }

        if (received[icmp_offset] == ICMP_ECHO_REPLY
            && read_be16(received + icmp_offset + 4) == 0xCA57
            && read_be16(received + icmp_offset + 6) == sequence) {
            return 1;
        }
    }

    return 0;
}

void network_init() {
    struct NetworkStatus previous = status;
    char* selected_driver;
    char* recommended_driver;
    char* preferred_driver;

    clear_network_status();

    if (previous.wifi_profile_saved) {
        status.wifi_profile_saved = 1;
        copy_string(status.wifi_ssid, sizeof(status.wifi_ssid), previous.wifi_ssid);
    }

    selected_driver = drivers_selected_driver("network");
    recommended_driver = drivers_recommended_driver("network");
    preferred_driver = (selected_driver && selected_driver[0] != '\0')
        ? selected_driver
        : recommended_driver;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint32_t id = pci_read32((uint8_t) bus, slot, function, 0x00);
                uint16_t vendor_id = (uint16_t) (id & 0xFFFF);
                uint16_t device_id = (uint16_t) ((id >> 16) & 0xFFFF);
                uint32_t class_info;
                uint8_t class_code;

                if (vendor_id == 0xFFFF) {
                    continue;
                }

                class_info = pci_read32((uint8_t) bus, slot, function, 0x08);
                class_code = (uint8_t) ((class_info >> 24) & 0xFF);

                if (class_code != PCI_CLASS_NETWORK) {
                    continue;
                }

                if (status.device_count == 0) {
                    status.first_device.vendor_id = vendor_id;
                    status.first_device.device_id = device_id;
                    status.first_device.bus = (uint8_t) bus;
                    status.first_device.slot = slot;
                    status.first_device.function = function;
                    status.first_device.class_code = class_code;
                    status.first_device.subclass = (uint8_t) ((class_info >> 16) & 0xFF);
                    status.first_device.prog_if = (uint8_t) ((class_info >> 8) & 0xFF);
                }

                status.device_count++;

                if (!status.packet_driver_ready) {
                    if (!network_try_driver_for_device(preferred_driver,
                                                       vendor_id,
                                                       device_id,
                                                       (uint8_t) bus,
                                                       slot,
                                                       function)) {
                        network_try_driver_for_device("generic-ethernet-pci",
                                                      vendor_id,
                                                      device_id,
                                                      (uint8_t) bus,
                                                      slot,
                                                      function);
                    }
                }
            }
        }
    }

    network_wifi_rescan();

    if (previous.wifi_driver_selected && !status.wifi_driver_selected) {
        network_wifi_select_driver(previous.wifi_selected_driver);
    }
}

void network_enable_dhcp() {
    status.enabled = status.device_count > 0 && status.packet_driver_ready;
    status.mode = status.enabled ? NETWORK_MODE_DHCP : NETWORK_MODE_DOWN;

    if (status.enabled) {
        set_ip_config(make_ip(10, 0, 2, 15), make_ip(255, 255, 255, 0), make_ip(10, 0, 2, 2));
    } else {
        status.ip[0] = '\0';
        status.netmask[0] = '\0';
        status.gateway[0] = '\0';
    }
}

void network_set_static(char* ip, char* netmask, char* gateway) {
    uint32_t parsed_ip;
    uint32_t parsed_netmask;
    uint32_t parsed_gateway;

    status.enabled = status.device_count > 0 && status.packet_driver_ready;
    status.mode = status.enabled ? NETWORK_MODE_STATIC : NETWORK_MODE_DOWN;

    if (!status.enabled) {
        copy_string(status.ip, sizeof(status.ip), ip);
        copy_string(status.netmask, sizeof(status.netmask), netmask);
        copy_string(status.gateway, sizeof(status.gateway), gateway);
        return;
    }

    if (!parse_ipv4(ip, &parsed_ip) || !parse_ipv4(netmask, &parsed_netmask) || !parse_ipv4(gateway, &parsed_gateway)) {
        status.enabled = 0;
        status.mode = NETWORK_MODE_DOWN;
        return;
    }

    set_ip_config(parsed_ip, parsed_netmask, parsed_gateway);
}

void network_disable() {
    status.enabled = 0;
    status.mode = NETWORK_MODE_DOWN;
    status.ip[0] = '\0';
    status.netmask[0] = '\0';
    status.gateway[0] = '\0';
    local_ip = 0;
    local_netmask = 0;
    local_gateway = 0;
}

int network_wifi_connect(char* ssid, char* password) {
    (void) password;

    if (!ssid || ssid[0] == '\0') {
        return 0;
    }

    copy_string(status.wifi_ssid, sizeof(status.wifi_ssid), ssid);
    status.wifi_profile_saved = 1;
    status.wifi_connected = 0;

    if (!status.wifi_hardware_present || !status.wifi_driver_selected) {
        return 0;
    }

    return 0;
}

void network_wifi_disconnect() {
    status.wifi_connected = 0;
}

struct NetworkStatus network_get_status() {
    return status;
}

int network_wifi_driver_count() {
    return status.wifi_driver_option_count;
}

char* network_wifi_driver_name(int index) {
    if (index < 0 || index >= status.wifi_driver_option_count) {
        return 0;
    }

    return wifi_driver_options[index];
}

int network_wifi_select_driver(char* name) {
    if (!name || name[0] == '\0' || !status.wifi_hardware_present) {
        return 0;
    }

    for (int i = 0; i < status.wifi_driver_option_count; i++) {
        if (strcmp_local(name, wifi_driver_options[i]) == 0) {
            status.wifi_driver_selected = 1;
            copy_string(status.wifi_selected_driver, sizeof(status.wifi_selected_driver), wifi_driver_options[i]);
            status.wifi_connected = 0;
            return 1;
        }
    }

    return 0;
}

char* network_wifi_selected_driver() {
    return status.wifi_driver_selected ? status.wifi_selected_driver : "";
}

char* network_wifi_recommended_driver() {
    return status.wifi_recommended_driver;
}

int network_ping(char* target) {
    uint32_t target_ip;
    uint32_t next_hop_ip;
    uint8_t next_hop_mac[NETWORK_MAC_LENGTH];
    uint16_t sequence = 1;

    if (!status.enabled) {
        return NETWORK_PING_DOWN;
    }

    if (!status.packet_driver_ready) {
        return NETWORK_PING_NO_PACKET_DRIVER;
    }

    if (!parse_ipv4(target, &target_ip)) {
        return NETWORK_PING_BAD_TARGET;
    }

    next_hop_ip = ((target_ip & local_netmask) == (local_ip & local_netmask)) ? target_ip : local_gateway;

    if (!resolve_mac(next_hop_ip, next_hop_mac)) {
        return NETWORK_PING_ARP_TIMEOUT;
    }

    if (!send_icmp_echo(target_ip, next_hop_mac, sequence)) {
        return NETWORK_PING_TIMEOUT;
    }

    if (!wait_for_icmp_reply(target_ip, sequence)) {
        return NETWORK_PING_TIMEOUT;
    }

    return NETWORK_PING_OK;
}

char* network_driver_state() {
    if (status.device_count == 0) {
        return "no PCI network controller found";
    }

    if (!status.packet_driver_ready) {
        return "network device found, no packet driver for it yet";
    }

    if (!status.enabled) {
        if (packet_driver_kind == NETWORK_PACKET_DRIVER_E1000) {
            return "e1000 packet driver ready, interface down";
        }

        return "RTL8139 packet driver ready, interface down";
    }

    if (packet_driver_kind == NETWORK_PACKET_DRIVER_E1000) {
        return "e1000 packet driver ready";
    }

    return "RTL8139 packet driver ready";
}

char* network_wifi_state() {
    if (!status.wifi_hardware_present) {
        return "no WiFi controller detected";
    }

    if (!status.wifi_driver_selected) {
        return "WiFi controller detected, choose a driver with wifi driver <name>";
    }

    if (!status.wifi_profile_saved) {
        return "WiFi driver selected, no profile configured";
    }

    if (!status.wifi_connected) {
        return "WiFi profile saved, driver selected, data path not implemented yet";
    }

    return "WiFi connected";
}
