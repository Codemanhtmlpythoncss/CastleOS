#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>

#define NETWORK_MAX_IP_LENGTH 16
#define NETWORK_MAX_SSID_LENGTH 33
#define NETWORK_MAC_LENGTH 6
#define NETWORK_MAX_PACKET_SIZE 1518
#define NETWORK_MAX_DRIVER_NAME 32
#define NETWORK_MAX_WIFI_DRIVER_NAME 32
#define NETWORK_MAX_WIFI_DRIVER_OPTIONS 8
#define NETWORK_HTTP_STATUS_SIZE 96

enum NetworkMode {
    NETWORK_MODE_DOWN = 0,
    NETWORK_MODE_DHCP = 1,
    NETWORK_MODE_STATIC = 2,
};

enum NetworkPingResult {
    NETWORK_PING_OK = 0,
    NETWORK_PING_DOWN = -1,
    NETWORK_PING_NO_PACKET_DRIVER = -2,
    NETWORK_PING_BAD_TARGET = -3,
    NETWORK_PING_ARP_TIMEOUT = -4,
    NETWORK_PING_TIMEOUT = -5,
};

enum NetworkHttpResult {
    NETWORK_HTTP_OK = 0,
    NETWORK_HTTP_DOWN = -1,
    NETWORK_HTTP_NO_PACKET_DRIVER = -2,
    NETWORK_HTTP_BAD_URL = -3,
    NETWORK_HTTP_DNS_FAILED = -4,
    NETWORK_HTTP_ARP_TIMEOUT = -5,
    NETWORK_HTTP_TCP_TIMEOUT = -6,
    NETWORK_HTTP_RESPONSE_TOO_LARGE = -7,
    NETWORK_HTTP_HTTPS_NOT_SUPPORTED = -8,
};

struct NetworkDeviceInfo {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

struct NetworkStatus {
    int initialized;
    int enabled;
    int device_count;
    int packet_driver_ready;
    int packets_sent;
    int packets_received;
    enum NetworkMode mode;
    char packet_driver_name[NETWORK_MAX_DRIVER_NAME];
    char ip[NETWORK_MAX_IP_LENGTH];
    char netmask[NETWORK_MAX_IP_LENGTH];
    char gateway[NETWORK_MAX_IP_LENGTH];
    char dns[NETWORK_MAX_IP_LENGTH];
    int wifi_hardware_present;
    int wifi_profile_saved;
    int wifi_connected;
    int wifi_driver_selected;
    int wifi_driver_option_count;
    char wifi_ssid[NETWORK_MAX_SSID_LENGTH];
    char wifi_selected_driver[NETWORK_MAX_WIFI_DRIVER_NAME];
    char wifi_recommended_driver[NETWORK_MAX_WIFI_DRIVER_NAME];
    uint8_t mac[NETWORK_MAC_LENGTH];
    struct NetworkDeviceInfo first_device;
    struct NetworkDeviceInfo first_wifi_device;
};

void network_init();
void network_wifi_rescan();
int network_enable_dhcp();
void network_set_static(char* ip, char* netmask, char* gateway);
void network_disable();
int network_wifi_connect(char* ssid, char* password);
void network_wifi_disconnect();
int network_wifi_driver_count();
char* network_wifi_driver_name(int index);
int network_wifi_select_driver(char* name);
char* network_wifi_selected_driver();
char* network_wifi_recommended_driver();
struct NetworkStatus network_get_status();
int network_ping(char* target);
int network_send_packet(uint8_t* data, uint16_t length);
int network_receive_packet(uint8_t* buffer, uint16_t* length);
int network_http_get(char* url, char* body, uint16_t body_size, char* status_text, uint16_t status_size);
char* network_driver_state();
char* network_wifi_state();

#endif
