#include <stddef.h>
#include <stdint.h>
#include "drivers.h"
#include "shell.h"
#include "shell_utils.h"
#include "shell_format.h"
#include "keyboard.h"
#include "network.h"
#include "print.h"

#define SHELL_INPUT_SIZE 256
#define SHELL_MAX_PATH_LENGTH 256
#define SHELL_MAX_NODES 64
#define SHELL_MAX_NAME_LENGTH 32
#define SHELL_MAX_FILE_SIZE 1024
#define SHELL_HISTORY_SIZE 32
#define SHELL_USERNAME_SIZE 32
#define SHELL_PASSWORD_SIZE 64
#define SSH_HOST_SIZE 64
#define SSH_FINGERPRINT_SIZE 48
#define INSTALL_TARGET_SIZE 64
#define INSTALL_TARGET_MAX_COUNT 5
#define INSTALL_LABEL_SIZE 32
#define INSTALL_LOCATION_SIZE 96
#define INSTALL_MANIFEST_SIZE 16384
#define MULTIBOOT_TAG_END 0
#define MULTIBOOT_TAG_BASIC_MEMINFO 4
#define MULTIBOOT_TAG_MMAP 6
#define VFS_DIRECTORY 1
#define VFS_FILE 2
#define SHELL_STYLE_CASTLE 0
#define SHELL_STYLE_BASH 1
#define INSTALL_TARGET_KIND_RAM 1
#define INSTALL_TARGET_KIND_DISK 2
#define INSTALL_TARGET_KIND_PARTITION 3
#define INSTALL_TARGET_KIND_USB_DISK 4
#define INSTALL_TARGET_KIND_USB_PARTITION 5
#define COMMAND_SHELL_CLASSIC 0x01
#define COMMAND_SHELL_BASH 0x02
#define COMMAND_REQUIRES_INSTALL 0x04

struct Command {
    char* name;
    void (*func)(char* args);
    char* description;
    uint8_t flags;
};

struct MultibootTag {
    uint32_t type;
    uint32_t size;
};

struct MultibootBasicMeminfoTag {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct MultibootMmapTag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct MultibootMmapEntry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct VfsNode {
    int used;
    int type;
    int parent;
    char name[SHELL_MAX_NAME_LENGTH];
    char content[SHELL_MAX_FILE_SIZE];
    size_t size;
};

struct InstallTarget {
    int present;
    int writable;
    int kind;
    char path[INSTALL_TARGET_SIZE];
    char label[INSTALL_LABEL_SIZE];
    char location[INSTALL_LOCATION_SIZE];
    uint64_t capacity_bytes;
    uint64_t offset_bytes;
    int installed;
    size_t installed_nodes;
    size_t installed_bytes;
    char installed_user[SHELL_USERNAME_SIZE];
    uint32_t installed_password_hash;
    int installed_shell_style;
    char installed_network_mode[16];
    char installed_ip[NETWORK_MAX_IP_LENGTH];
    char installed_netmask[NETWORK_MAX_IP_LENGTH];
    char installed_gateway[NETWORK_MAX_IP_LENGTH];
    char installed_wifi_ssid[NETWORK_MAX_SSID_LENGTH];
    char manifest[INSTALL_MANIFEST_SIZE];
};

int strcmp(char* a, char* b);

static uint64_t total_ram_bytes = 0;
static struct VfsNode vfs_nodes[SHELL_MAX_NODES];
static struct InstallTarget install_targets[INSTALL_TARGET_MAX_COUNT];
static char shell_history[SHELL_HISTORY_SIZE][SHELL_INPUT_SIZE];
static size_t shell_history_count = 0;
static int current_directory = 0;
static int home_directory = 0;
static int os_installed = 0;
static int bash_installed = 0;
static int default_shell_style = SHELL_STYLE_CASTLE;
static int current_shell_style = SHELL_STYLE_CASTLE;
static char install_username[SHELL_USERNAME_SIZE] = "barnaby";
static int install_password_set = 0;
static uint32_t install_password_hash = 0;
static char install_target[INSTALL_TARGET_SIZE] = "/dev/ram0";
static int ssh_enabled = 0;
static uint16_t ssh_port = 22;
static char ssh_last_host[SSH_HOST_SIZE];
static char ssh_remote_user[SHELL_USERNAME_SIZE] = "barnaby";
static uint32_t ssh_host_key_seed = 0x53485348;
static char ssh_host_key_fingerprint[SSH_FINGERPRINT_SIZE];

static inline void outw(uint16_t port, uint16_t value);
static void cpuid(unsigned int code, unsigned int* a, unsigned int* b, unsigned int* c, unsigned int* d);
static void get_cpu_brand_string(char* name, size_t name_size);
static uint64_t detect_total_ram(uint32_t multiboot_info_addr);
static int vfs_find_child(int parent, const char* name);
static int vfs_create_node(int parent, int type, const char* name);
static int vfs_create_directory(int parent, const char* name);
static int vfs_create_file(int parent, const char* name, const char* content);
static int vfs_is_ancestor(int ancestor_index, int node_index);
static int vfs_is_directory_empty(int node_index);
static void vfs_delete_subtree(int node_index);
static int vfs_move_node(int node_index, int new_parent, const char* new_name);
static int vfs_copy_node(int node_index, int new_parent, const char* new_name);
static int vfs_make_directory_path(const char* path);
static int vfs_resolve_destination(int source_index, const char* destination_path, int* parent, char* leaf_name);
static void vfs_init();
static int vfs_resolve_path(const char* path);
static int vfs_resolve_parent(const char* path, int* parent, char* leaf_name);
static void vfs_build_path(int node_index, char* buffer, size_t buffer_size);
static void shell_print_prompt();
static void shell_render_buffer(size_t start_col,
                                size_t start_row,
                                char* input,
                                size_t length,
                                size_t cursor_index,
                                size_t* rendered_length,
                                int mask_input);
static void shell_read_buffer(char* input, size_t input_size, int mask_input, int allow_history);
static void shell_read_line(char* input, size_t input_size);
static void shell_read_password(char* input, size_t input_size);
static char install_read_choice(char* prompt, char default_choice);
static uint32_t hash_password(char* password);
static void print_install_targets();
static void print_disk_inventory();
static void shell_open_nano(int file_index);
static void shell_print_directory_listing(int directory_index);
static void shell_record_history(const char* input);
static struct Command* find_command(char* name);
static struct Command* find_command_any(char* name);
static int command_is_available(struct Command* command);
static void print_pci_device(struct DriverPciDevice device);
static void print_driver_status();
static void print_network_status();
static void print_ssh_status();
static void print_driver_choices_for_subsystem(char* subsystem);
static int select_driver_for_subsystem(char* subsystem, char* driver_name);
static char* selected_driver_for_subsystem(char* subsystem);
static char* recommended_driver_for_subsystem(char* subsystem);
static void rescan_drivers_and_targets();
static void rescan_drivers_network_and_targets();
static void autoselect_install_drivers(char network_choice);
static void print_current_install_target_and_inventory();
static void print_all_subsystem_driver_choices();
static void persist_driver_preferences();
static void persist_install_target_configuration();
static void persist_ssh_configuration();
static void build_ssh_host_key_fingerprint(char* buffer, size_t buffer_size);
static void refresh_install_targets();
static struct InstallTarget* find_install_target(const char* path);
static struct InstallTarget* get_install_target_by_choice(char choice);
static int visible_install_target_count();
static void print_install_target_summary(struct InstallTarget* target, int selected, int display_index);
static void print_install_target_details(struct InstallTarget* target);
static int interactive_select_driver_for_subsystem(char* subsystem);
static char* interactive_pick_subsystem();
static int maybe_run_config_flow(struct Command* command, char* args);
static void cmd_net_config();
static void cmd_wifi_config();
static void cmd_ssh_config();
static void cmd_disk_config();
static void cmd_drivers_config();
static int write_install_image(struct InstallTarget* target,
                               char network_choice,
                               const char* ip,
                               const char* netmask,
                               const char* gateway,
                               const char* wifi_ssid);
static void cmd_help(char* args);
static void cmd_clear(char* args);
static void cmd_echo(char* args);
static void cmd_man(char* args);
static void cmd_sysinfo(char* args);
static void cmd_cd(char* args);
static void cmd_ls(char* args);
static void cmd_pwd(char* args);
static void cmd_nano(char* args);
static void cmd_bash(char* args);
static void cmd_exit(char* args);
static void cmd_install(char* args);
static void cmd_net(char* args);
static void cmd_ping(char* args);
static void cmd_wifi(char* args);
static void cmd_ssh(char* args);
static void print_wifi_driver_options();
static void print_wifi_status_details();
static void cmd_cat(char* args);
static void cmd_touch(char* args);
static void cmd_mkdir(char* args);
static void cmd_rm(char* args);
static void cmd_cp(char* args);
static void cmd_mv(char* args);
static void cmd_whoami(char* args);
static void cmd_uname(char* args);
static void cmd_history(char* args);
static void cmd_hostname(char* args);
static void cmd_disk(char* args);
static void cmd_drivers(char* args);
static void cmd_pci(char* args);
static void cmd_shutdown(char* args);

static struct Command commands[] = {
    {"help", cmd_help, "shows the available shell commands", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"clear", cmd_clear, "clears the terminal screen", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"echo", cmd_echo, "prints text back to the terminal", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"man", cmd_man, "shows help for a specific command", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"sysinfo", cmd_sysinfo, "shows CPU, RAM, and terminal information", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"cd", cmd_cd, "changes the current virtual directory", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"ls", cmd_ls, "lists files and directories", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"pwd", cmd_pwd, "prints the current virtual directory", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"nano", cmd_nano, "opens a simple in-memory text editor", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"cat", cmd_cat, "prints a file to the terminal", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"touch", cmd_touch, "creates an empty file", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"mkdir", cmd_mkdir, "creates directories", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"rm", cmd_rm, "removes files or directories", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"cp", cmd_cp, "copies files and directories", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"mv", cmd_mv, "moves or renames files and directories", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"whoami", cmd_whoami, "prints the current user", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"uname", cmd_uname, "prints system information", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"history", cmd_history, "shows recent shell commands", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"hostname", cmd_hostname, "prints the system hostname", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"bash", cmd_bash, "starts CastleBash after it has been installed", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"exit", cmd_exit, "returns from CastleBash to the classic shell", COMMAND_SHELL_BASH | COMMAND_REQUIRES_INSTALL},
    {"install", cmd_install, "runs the CastleOS installer", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"net", cmd_net, "configures the network interface", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"ping", cmd_ping, "tests network reachability when packet networking exists", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"wifi", cmd_wifi, "saves a WiFi profile for future wireless drivers", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"ssh", cmd_ssh, "configures SSH access and connection profiles", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"disk", cmd_disk, "shows or selects an install disk or partition", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"drivers", cmd_drivers, "shows loaded and missing hardware drivers", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"pci", cmd_pci, "shows detected PCI hardware summary", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
    {"shutdown", cmd_shutdown, "powers off the machine", COMMAND_SHELL_CLASSIC | COMMAND_SHELL_BASH},
};

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void cpuid(unsigned int code,
                  unsigned int* a,
                  unsigned int* b,
                  unsigned int* c,
                  unsigned int* d) {
    __asm__ volatile (
        "cpuid"
        : "=a" (*a), "=b" (*b), "=c" (*c), "=d" (*d)
        : "a" (code)
    );
}

static void get_cpu_brand_string(char* name, size_t name_size) {
    unsigned int regs[4];
    unsigned int max_extended_leaf = 0;

    copy_string(name, name_size, "Unknown CPU");
    cpuid(0x80000000, &max_extended_leaf, &regs[1], &regs[2], &regs[3]);

    if (max_extended_leaf < 0x80000004 || name_size < 49) {
        return;
    }

    for (int i = 0; i < 3; i++) {
        cpuid(0x80000002 + (unsigned int) i, &regs[0], &regs[1], &regs[2], &regs[3]);
        ((unsigned int*) name)[i * 4 + 0] = regs[0];
        ((unsigned int*) name)[i * 4 + 1] = regs[1];
        ((unsigned int*) name)[i * 4 + 2] = regs[2];
        ((unsigned int*) name)[i * 4 + 3] = regs[3];
    }

    name[48] = '\0';
}

static uint64_t detect_total_ram(uint32_t multiboot_info_addr) {
    uint8_t* boot_info;
    uint32_t total_size;
    uint8_t* end;
    uint8_t* current;
    uint64_t ram_from_mmap = 0;
    uint64_t ram_from_basic_info = 0;

    if (multiboot_info_addr == 0) {
        return 0;
    }

    boot_info = (uint8_t*) (uintptr_t) multiboot_info_addr;
    total_size = *(uint32_t*) boot_info;
    end = boot_info + total_size;
    current = boot_info + 8;

    while (current + sizeof(struct MultibootTag) <= end) {
        struct MultibootTag* tag = (struct MultibootTag*) current;

        if (tag->type == MULTIBOOT_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT_TAG_BASIC_MEMINFO) {
            struct MultibootBasicMeminfoTag* meminfo = (struct MultibootBasicMeminfoTag*) tag;
            ram_from_basic_info = ((uint64_t) meminfo->mem_lower + (uint64_t) meminfo->mem_upper) * 1024;
        } else if (tag->type == MULTIBOOT_TAG_MMAP) {
            struct MultibootMmapTag* mmap_tag = (struct MultibootMmapTag*) tag;
            uint8_t* entry_ptr = current + sizeof(struct MultibootMmapTag);
            uint8_t* entries_end = current + tag->size;

            while (entry_ptr + mmap_tag->entry_size <= entries_end) {
                struct MultibootMmapEntry* entry = (struct MultibootMmapEntry*) entry_ptr;

                if (entry->type == 1) {
                    ram_from_mmap += entry->length;
                }

                entry_ptr += mmap_tag->entry_size;
            }
        }

        current += align_up(tag->size, 8);
    }

    if (ram_from_mmap != 0) {
        return ram_from_mmap;
    }

    return ram_from_basic_info;
}

static int vfs_find_child(int parent, const char* name) {
    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (!vfs_nodes[i].used || vfs_nodes[i].parent != parent) {
            continue;
        }

        if (strcmp(vfs_nodes[i].name, (char*) name) == 0) {
            return i;
        }
    }

    return -1;
}

static int vfs_create_node(int parent, int type, const char* name) {
    int index;

    if (name[0] == '\0' || str_length(name) >= SHELL_MAX_NAME_LENGTH) {
        return -1;
    }

    if (vfs_find_child(parent, name) >= 0) {
        return -1;
    }

    for (index = 0; index < SHELL_MAX_NODES; index++) {
        if (!vfs_nodes[index].used) {
            vfs_nodes[index].used = 1;
            vfs_nodes[index].type = type;
            vfs_nodes[index].parent = parent;
            vfs_nodes[index].size = 0;
            vfs_nodes[index].content[0] = '\0';
            copy_string(vfs_nodes[index].name, sizeof(vfs_nodes[index].name), name);
            return index;
        }
    }

    return -1;
}

static int vfs_create_directory(int parent, const char* name) {
    return vfs_create_node(parent, VFS_DIRECTORY, name);
}

static int vfs_create_file(int parent, const char* name, const char* content) {
    int index = vfs_create_node(parent, VFS_FILE, name);

    if (index < 0) {
        return -1;
    }

    if (content) {
        copy_string(vfs_nodes[index].content, sizeof(vfs_nodes[index].content), content);
        vfs_nodes[index].size = str_length(vfs_nodes[index].content);
    }

    return index;
}

static int vfs_is_ancestor(int ancestor_index, int node_index) {
    while (node_index >= 0) {
        if (node_index == ancestor_index) {
            return 1;
        }

        node_index = vfs_nodes[node_index].parent;
    }

    return 0;
}

static int vfs_is_directory_empty(int node_index) {
    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (vfs_nodes[i].used && vfs_nodes[i].parent == node_index) {
            return 0;
        }
    }

    return 1;
}

static void vfs_delete_subtree(int node_index) {
    if (node_index <= 0 || !vfs_nodes[node_index].used) {
        return;
    }

    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (vfs_nodes[i].used && vfs_nodes[i].parent == node_index) {
            vfs_delete_subtree(i);
        }
    }

    vfs_nodes[node_index].used = 0;
    vfs_nodes[node_index].type = 0;
    vfs_nodes[node_index].parent = -1;
    vfs_nodes[node_index].name[0] = '\0';
    vfs_nodes[node_index].content[0] = '\0';
    vfs_nodes[node_index].size = 0;
}

static int vfs_move_node(int node_index, int new_parent, const char* new_name) {
    if (node_index <= 0 || !vfs_nodes[node_index].used || !new_name || new_name[0] == '\0') {
        return 0;
    }

    if (new_parent < 0 || !vfs_nodes[new_parent].used || vfs_nodes[new_parent].type != VFS_DIRECTORY) {
        return 0;
    }

    if (str_length(new_name) >= SHELL_MAX_NAME_LENGTH) {
        return 0;
    }

    if (vfs_nodes[node_index].type == VFS_DIRECTORY && vfs_is_ancestor(node_index, new_parent)) {
        return 0;
    }

    if (vfs_find_child(new_parent, new_name) >= 0
        && (vfs_nodes[node_index].parent != new_parent || strcmp(vfs_nodes[node_index].name, (char*) new_name) != 0)) {
        return 0;
    }

    vfs_nodes[node_index].parent = new_parent;
    copy_string(vfs_nodes[node_index].name, sizeof(vfs_nodes[node_index].name), new_name);
    return 1;
}

static int vfs_copy_node(int node_index, int new_parent, const char* new_name) {
    int new_index;

    if (node_index < 0 || !vfs_nodes[node_index].used) {
        return -1;
    }

    if (new_parent < 0 || !vfs_nodes[new_parent].used || vfs_nodes[new_parent].type != VFS_DIRECTORY) {
        return -1;
    }

    if (vfs_nodes[node_index].type == VFS_DIRECTORY && vfs_is_ancestor(node_index, new_parent)) {
        return -1;
    }

    new_index = vfs_create_node(new_parent, vfs_nodes[node_index].type, new_name);
    if (new_index < 0) {
        return -1;
    }

    if (vfs_nodes[node_index].type == VFS_FILE) {
        copy_string(vfs_nodes[new_index].content, sizeof(vfs_nodes[new_index].content), vfs_nodes[node_index].content);
        vfs_nodes[new_index].size = vfs_nodes[node_index].size;
        return new_index;
    }

    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (!vfs_nodes[i].used || vfs_nodes[i].parent != node_index) {
            continue;
        }

        if (vfs_copy_node(i, new_index, vfs_nodes[i].name) < 0) {
            vfs_delete_subtree(new_index);
            return -1;
        }
    }

    return new_index;
}

static int vfs_make_directory_path(const char* path) {
    int node;
    size_t i;
    char part[SHELL_MAX_NAME_LENGTH];

    if (!path || path[0] == '\0') {
        return -1;
    }

    node = path[0] == '/' ? 0 : current_directory;
    i = path[0] == '/' ? 1 : 0;

    while (1) {
        int child;
        size_t part_length = 0;

        while (path[i] == '/') {
            i++;
        }

        if (path[i] == '\0') {
            return node;
        }

        while (path[i] != '\0' && path[i] != '/') {
            if (part_length + 1 >= sizeof(part)) {
                return -1;
            }

            part[part_length++] = path[i++];
        }

        part[part_length] = '\0';

        if (strcmp(part, ".") == 0) {
            continue;
        }

        if (strcmp(part, "..") == 0) {
            if (vfs_nodes[node].parent >= 0) {
                node = vfs_nodes[node].parent;
            }
            continue;
        }

        child = vfs_find_child(node, part);
        if (child < 0) {
            child = vfs_create_directory(node, part);
        }

        if (child < 0 || vfs_nodes[child].type != VFS_DIRECTORY) {
            return -1;
        }

        node = child;
    }
}

static int vfs_resolve_destination(int source_index, const char* destination_path, int* parent, char* leaf_name) {
    int destination_index;

    if (!destination_path || !parent || !leaf_name) {
        return 0;
    }

    destination_index = vfs_resolve_path(destination_path);
    if (destination_index >= 0) {
        if (vfs_nodes[destination_index].type != VFS_DIRECTORY) {
            return 0;
        }

        *parent = destination_index;
        copy_string(leaf_name, SHELL_MAX_NAME_LENGTH, vfs_nodes[source_index].name);
        return 1;
    }

    return vfs_resolve_parent(destination_path, parent, leaf_name);
}

static void vfs_init() {
    int root_index = 0;
    int etc_index;
    int tmp_index;
    int home_notes;

    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        vfs_nodes[i].used = 0;
        vfs_nodes[i].type = 0;
        vfs_nodes[i].parent = -1;
        vfs_nodes[i].name[0] = '\0';
        vfs_nodes[i].content[0] = '\0';
        vfs_nodes[i].size = 0;
    }

    vfs_nodes[root_index].used = 1;
    vfs_nodes[root_index].type = VFS_DIRECTORY;
    vfs_nodes[root_index].parent = -1;
    vfs_nodes[root_index].name[0] = '\0';
    vfs_nodes[root_index].content[0] = '\0';
    vfs_nodes[root_index].size = 0;

    home_directory = vfs_create_directory(root_index, "home");
    vfs_create_directory(root_index, "bin");
    etc_index = vfs_create_directory(root_index, "etc");
    tmp_index = vfs_create_directory(root_index, "tmp");
    vfs_create_directory(home_directory, "docs");
    home_notes = vfs_create_file(home_directory, "notes.txt",
        "CastleOS virtual filesystem\n"
        "This file lives in memory only.\n"
        "Use nano to edit it.\n");
    vfs_create_file(home_directory, "todo.txt",
        "Build disk drivers\n"
        "Add a real filesystem\n"
        "Keep shipping CastleOS\n");

    if (tmp_index >= 0) {
        vfs_create_file(tmp_index, "session.log", "Temporary shell workspace\n");
    }

    if (etc_index >= 0) {
        vfs_create_file(etc_index, "network.conf", "mode=down\n");
        vfs_create_file(etc_index, "profile", "shell=castle\n");
        vfs_create_file(etc_index, "install.conf", "target=/dev/ram0\n");
        vfs_create_file(etc_index, "wifi.conf", "ssid=\n");
        vfs_create_file(etc_index, "ssh.conf", "enabled=0\nport=22\nuser=barnaby\nlast_host=\nhostkey=\n");
    }

    if (home_notes >= 0) {
        int docs_index = vfs_find_child(home_directory, "docs");

        if (docs_index >= 0) {
            vfs_create_file(docs_index, "readme.txt",
                "cd, ls, pwd, and nano are virtual shell commands for now.\n");
        }
    }

    current_directory = home_directory >= 0 ? home_directory : root_index;
}

static int vfs_resolve_path(const char* path) {
    int node;
    size_t i;
    char part[SHELL_MAX_NAME_LENGTH];

    if (!path || path[0] == '\0') {
        return current_directory;
    }

    node = path[0] == '/' ? 0 : current_directory;
    i = path[0] == '/' ? 1 : 0;

    while (1) {
        size_t part_length = 0;

        while (path[i] == '/') {
            i++;
        }

        if (path[i] == '\0') {
            return node;
        }

        while (path[i] != '\0' && path[i] != '/') {
            if (part_length + 1 >= sizeof(part)) {
                return -1;
            }

            part[part_length++] = path[i++];
        }

        part[part_length] = '\0';

        if (strcmp(part, ".") == 0) {
            continue;
        }

        if (strcmp(part, "..") == 0) {
            if (vfs_nodes[node].parent >= 0) {
                node = vfs_nodes[node].parent;
            }

            continue;
        }

        node = vfs_find_child(node, part);

        if (node < 0) {
            return -1;
        }
    }
}

static int vfs_resolve_parent(const char* path, int* parent, char* leaf_name) {
    int node;
    size_t i;
    char part[SHELL_MAX_NAME_LENGTH];

    if (!path || path[0] == '\0') {
        return 0;
    }

    node = path[0] == '/' ? 0 : current_directory;
    i = path[0] == '/' ? 1 : 0;

    while (1) {
        size_t part_length = 0;

        while (path[i] == '/') {
            i++;
        }

        if (path[i] == '\0') {
            return 0;
        }

        while (path[i] != '\0' && path[i] != '/') {
            if (part_length + 1 >= sizeof(part)) {
                return 0;
            }

            part[part_length++] = path[i++];
        }

        part[part_length] = '\0';

        while (path[i] == '/') {
            i++;
        }

        if (path[i] == '\0') {
            if (strcmp(part, ".") == 0 || strcmp(part, "..") == 0) {
                return 0;
            }

            *parent = node;
            copy_string(leaf_name, SHELL_MAX_NAME_LENGTH, part);
            return 1;
        }

        if (strcmp(part, ".") == 0) {
            continue;
        }

        if (strcmp(part, "..") == 0) {
            if (vfs_nodes[node].parent >= 0) {
                node = vfs_nodes[node].parent;
            }

            continue;
        }

        node = vfs_find_child(node, part);

        if (node < 0 || vfs_nodes[node].type != VFS_DIRECTORY) {
            return 0;
        }
    }
}

static void vfs_build_path(int node_index, char* buffer, size_t buffer_size) {
    int stack[SHELL_MAX_NODES];
    size_t count = 0;

    if (buffer_size == 0) {
        return;
    }

    if (node_index == 0) {
        copy_string(buffer, buffer_size, "/");
        return;
    }

    while (node_index > 0 && count < SHELL_MAX_NODES) {
        stack[count++] = node_index;
        node_index = vfs_nodes[node_index].parent;
    }

    buffer[0] = '\0';
    append_char(buffer, buffer_size, '/');

    while (count > 0) {
        int index = stack[--count];

        append_string(buffer, buffer_size, vfs_nodes[index].name);

        if (count > 0) {
            append_char(buffer, buffer_size, '/');
        }
    }
}

static int visible_install_target_count() {
    int count = 0;

    for (int i = 0; i < INSTALL_TARGET_MAX_COUNT; i++) {
        if (install_targets[i].present) {
            count++;
        }
    }

    return count;
}

static struct InstallTarget* find_install_target(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }

    for (int i = 0; i < INSTALL_TARGET_MAX_COUNT; i++) {
        if (!install_targets[i].present) {
            continue;
        }

        if (strcmp(install_targets[i].path, (char*) path) == 0) {
            return &install_targets[i];
        }
    }

    return 0;
}

static struct InstallTarget* get_install_target_by_choice(char choice) {
    int display_index = 1;

    for (int i = 0; i < INSTALL_TARGET_MAX_COUNT; i++) {
        if (!install_targets[i].present) {
            continue;
        }

        if (choice == (char) ('0' + display_index)) {
            return &install_targets[i];
        }

        display_index++;
    }

    return 0;
}

static char* install_target_kind_name(int kind) {
    if (kind == INSTALL_TARGET_KIND_RAM) {
        return "RAM disk";
    }

    if (kind == INSTALL_TARGET_KIND_PARTITION) {
        return "partition";
    }

    if (kind == INSTALL_TARGET_KIND_USB_DISK) {
        return "USB disk";
    }

    if (kind == INSTALL_TARGET_KIND_USB_PARTITION) {
        return "USB partition";
    }

    return "disk";
}

static uint64_t default_ram_install_capacity() {
    uint64_t minimum = 16ULL * 1024ULL * 1024ULL;
    uint64_t maximum = 128ULL * 1024ULL * 1024ULL;
    uint64_t capacity;

    if (total_ram_bytes == 0) {
        return 32ULL * 1024ULL * 1024ULL;
    }

    capacity = total_ram_bytes / 8;

    if (capacity < minimum) {
        return minimum;
    }

    if (capacity > maximum) {
        return maximum;
    }

    return capacity;
}

static char* storage_target_label(struct DriverPciDevice device) {
    if (device.subclass == 0x01) {
        return "IDE install disk";
    }

    if (device.subclass == 0x06) {
        return "SATA install disk";
    }

    if (device.subclass == 0x08) {
        return "NVMe install disk";
    }

    return "PCI install disk";
}

static char* usb_target_label(struct DriverPciDevice device) {
    if (device.prog_if == 0x30) {
        return "USB xHCI removable disk";
    }

    if (device.prog_if == 0x20) {
        return "USB EHCI removable disk";
    }

    if (device.prog_if == 0x10) {
        return "USB OHCI removable disk";
    }

    if (device.prog_if == 0x00) {
        return "USB UHCI removable disk";
    }

    return "USB removable disk";
}

static void build_storage_location(char* buffer, size_t buffer_size, struct DriverPciDevice device) {
    buffer[0] = '\0';
    append_string(buffer, buffer_size, "PCI ");
    append_u64_string(buffer, buffer_size, device.bus);
    append_char(buffer, buffer_size, ':');
    append_u64_string(buffer, buffer_size, device.slot);
    append_char(buffer, buffer_size, '.');
    append_u64_string(buffer, buffer_size, device.function);
    append_string(buffer, buffer_size, " class ");
    append_hex_u8_string(buffer, buffer_size, device.class_code);
    append_char(buffer, buffer_size, '/');
    append_hex_u8_string(buffer, buffer_size, device.subclass);
}

static void build_usb_location(char* buffer, size_t buffer_size, struct DriverPciDevice device) {
    build_storage_location(buffer, buffer_size, device);
    append_string(buffer, buffer_size, " removable bus");
}

static void refresh_install_targets() {
    struct DriverStatus status = drivers_get_status();
    struct InstallTarget* ram_target = &install_targets[0];
    struct InstallTarget* disk_target = &install_targets[1];
    struct InstallTarget* partition_target = &install_targets[2];
    struct InstallTarget* usb_disk_target = &install_targets[3];
    struct InstallTarget* usb_partition_target = &install_targets[4];

    ram_target->present = 1;
    ram_target->writable = 1;
    ram_target->kind = INSTALL_TARGET_KIND_RAM;
    copy_string(ram_target->path, sizeof(ram_target->path), "/dev/ram0");
    copy_string(ram_target->label, sizeof(ram_target->label), "Live memory install");
    copy_string(ram_target->location, sizeof(ram_target->location), "system memory");
    ram_target->capacity_bytes = default_ram_install_capacity();
    ram_target->offset_bytes = 0;

    copy_string(disk_target->path, sizeof(disk_target->path), "/dev/sda");
    copy_string(partition_target->path, sizeof(partition_target->path), "/dev/sda1");

    if (status.first_storage.present) {
        disk_target->present = 1;
        disk_target->writable = 1;
        disk_target->kind = INSTALL_TARGET_KIND_DISK;
        copy_string(disk_target->label, sizeof(disk_target->label), storage_target_label(status.first_storage));
        build_storage_location(disk_target->location, sizeof(disk_target->location), status.first_storage);
        disk_target->capacity_bytes = status.nvme_devices > 0
            ? 512ULL * 1024ULL * 1024ULL
            : 256ULL * 1024ULL * 1024ULL;
        disk_target->offset_bytes = 0;

        partition_target->present = 1;
        partition_target->writable = 1;
        partition_target->kind = INSTALL_TARGET_KIND_PARTITION;
        copy_string(partition_target->label, sizeof(partition_target->label), "Primary install partition");
        copy_string(partition_target->location, sizeof(partition_target->location), disk_target->location);
        partition_target->capacity_bytes = disk_target->capacity_bytes - (16ULL * 1024ULL * 1024ULL);
        partition_target->offset_bytes = 1ULL * 1024ULL * 1024ULL;
    } else {
        disk_target->present = 0;
        disk_target->writable = 0;
        disk_target->kind = INSTALL_TARGET_KIND_DISK;
        disk_target->capacity_bytes = 0;
        disk_target->offset_bytes = 0;
        disk_target->location[0] = '\0';
        disk_target->label[0] = '\0';

        partition_target->present = 0;
        partition_target->writable = 0;
        partition_target->kind = INSTALL_TARGET_KIND_PARTITION;
        partition_target->capacity_bytes = 0;
        partition_target->offset_bytes = 0;
        partition_target->location[0] = '\0';
        partition_target->label[0] = '\0';
    }

    copy_string(usb_disk_target->path, sizeof(usb_disk_target->path), "/dev/usb0");
    copy_string(usb_partition_target->path, sizeof(usb_partition_target->path), "/dev/usb0p1");

    if (status.first_usb.present) {
        usb_disk_target->present = 1;
        usb_disk_target->writable = 1;
        usb_disk_target->kind = INSTALL_TARGET_KIND_USB_DISK;
        copy_string(usb_disk_target->label, sizeof(usb_disk_target->label), usb_target_label(status.first_usb));
        build_usb_location(usb_disk_target->location, sizeof(usb_disk_target->location), status.first_usb);
        usb_disk_target->capacity_bytes = 64ULL * 1024ULL * 1024ULL;
        usb_disk_target->offset_bytes = 0;

        usb_partition_target->present = 1;
        usb_partition_target->writable = 1;
        usb_partition_target->kind = INSTALL_TARGET_KIND_USB_PARTITION;
        copy_string(usb_partition_target->label, sizeof(usb_partition_target->label), "USB removable partition");
        copy_string(usb_partition_target->location, sizeof(usb_partition_target->location), usb_disk_target->location);
        usb_partition_target->capacity_bytes = 56ULL * 1024ULL * 1024ULL;
        usb_partition_target->offset_bytes = 1ULL * 1024ULL * 1024ULL;
    } else {
        usb_disk_target->present = 0;
        usb_disk_target->writable = 0;
        usb_disk_target->kind = INSTALL_TARGET_KIND_USB_DISK;
        usb_disk_target->capacity_bytes = 0;
        usb_disk_target->offset_bytes = 0;
        usb_disk_target->location[0] = '\0';
        usb_disk_target->label[0] = '\0';

        usb_partition_target->present = 0;
        usb_partition_target->writable = 0;
        usb_partition_target->kind = INSTALL_TARGET_KIND_USB_PARTITION;
        usb_partition_target->capacity_bytes = 0;
        usb_partition_target->offset_bytes = 0;
        usb_partition_target->location[0] = '\0';
        usb_partition_target->label[0] = '\0';
    }

    if (!find_install_target(install_target)) {
        copy_string(install_target, sizeof(install_target), ram_target->path);
    }
}

static void print_install_target_summary(struct InstallTarget* target, int selected, int display_index) {
    if (display_index > 0) {
        print_u64((uint64_t) display_index);
        print_str(") ");
    }

    print_str(target->path);

    if (selected) {
        print_str(" [selected]");
    }

    if (target->installed) {
        print_str(" [installed]");
    }

    print_newline();
    print_str("   ");
    print_str(target->label);
    print_str(" - ");
    print_u64(target->capacity_bytes / (1024ULL * 1024ULL));
    print_str(" MiB - ");
    print_str(target->location);

    if (target->offset_bytes > 0) {
        print_str(" - offset ");
        print_u64(target->offset_bytes / (1024ULL * 1024ULL));
        print_str(" MiB");
    }

    print_newline();
}

static void print_install_targets() {
    int display_index = 1;

    print_str("Available install targets");
    print_newline();

    for (int i = 0; i < INSTALL_TARGET_MAX_COUNT; i++) {
        if (!install_targets[i].present) {
            continue;
        }

        print_install_target_summary(&install_targets[i], strcmp(install_targets[i].path, install_target) == 0, display_index);
        display_index++;
    }

    print_u64((uint64_t) display_index);
    print_str(") Custom disk or partition path");
    print_newline();
}

static void print_disk_inventory() {
    print_str("Disk inventory");
    print_newline();

    for (int i = 0; i < INSTALL_TARGET_MAX_COUNT; i++) {
        if (!install_targets[i].present) {
            continue;
        }

        print_install_target_summary(&install_targets[i], strcmp(install_targets[i].path, install_target) == 0, 0);
    }

    if (visible_install_target_count() == 1) {
        print_str("Only the live RAM install target is currently available.");
        print_newline();
    }
}

static void print_install_target_details(struct InstallTarget* target) {
    print_str("Target: ");
    print_str(target->path);
    print_newline();

    print_str("Type: ");
    print_str(install_target_kind_name(target->kind));
    print_newline();

    print_str("Label: ");
    print_str(target->label);
    print_newline();

    print_str("Location: ");
    print_str(target->location);
    print_newline();

    print_str("Capacity: ");
    print_u64(target->capacity_bytes / (1024ULL * 1024ULL));
    print_str(" MiB");
    print_newline();

    print_str("Writable: ");
    print_str(target->writable ? "yes" : "no");
    print_newline();

    print_str("Installed image: ");
    print_str(target->installed ? "yes" : "no");
    print_newline();

    if (!target->installed) {
        return;
    }

    print_str("Installed user: ");
    print_str(target->installed_user);
    print_newline();

    print_str("Installed shell: ");
    print_str(target->installed_shell_style == SHELL_STYLE_BASH ? "CastleBash" : "Castle shell");
    print_newline();

    print_str("Installed networking: ");
    print_str(target->installed_network_mode);
    print_newline();

    if (target->installed_ip[0] != '\0') {
        print_str("IP: ");
        print_str(target->installed_ip);
        print_newline();
    }

    if (target->installed_gateway[0] != '\0') {
        print_str("Gateway: ");
        print_str(target->installed_gateway);
        print_newline();
    }

    if (target->installed_wifi_ssid[0] != '\0') {
        print_str("WiFi SSID: ");
        print_str(target->installed_wifi_ssid);
        print_newline();
    }

    print_str("Captured nodes: ");
    print_u64((uint64_t) target->installed_nodes);
    print_newline();

    print_str("Image bytes: ");
    print_u64((uint64_t) target->installed_bytes);
    print_newline();

    print_str("Install manifest");
    print_newline();
    print_str(target->manifest);
}

static int write_install_image(struct InstallTarget* target,
                               char network_choice,
                               const char* ip,
                               const char* netmask,
                               const char* gateway,
                               const char* wifi_ssid) {
    char path[SHELL_MAX_PATH_LENGTH];

    if (!target || !target->present || !target->writable) {
        return 0;
    }

    target->installed = 1;
    target->installed_nodes = 0;
    copy_string(target->installed_user, sizeof(target->installed_user), install_username);
    target->installed_password_hash = install_password_hash;
    target->installed_shell_style = default_shell_style;
    target->manifest[0] = '\0';
    target->installed_ip[0] = '\0';
    target->installed_netmask[0] = '\0';
    target->installed_gateway[0] = '\0';
    target->installed_wifi_ssid[0] = '\0';

    if (network_choice == '2') {
        copy_string(target->installed_network_mode, sizeof(target->installed_network_mode), "static");
        copy_string(target->installed_ip, sizeof(target->installed_ip), ip);
        copy_string(target->installed_netmask, sizeof(target->installed_netmask), netmask);
        copy_string(target->installed_gateway, sizeof(target->installed_gateway), gateway);
    } else if (network_choice == '3') {
        copy_string(target->installed_network_mode, sizeof(target->installed_network_mode), "wifi");
        copy_string(target->installed_wifi_ssid, sizeof(target->installed_wifi_ssid), wifi_ssid);
    } else if (network_choice == '4') {
        copy_string(target->installed_network_mode, sizeof(target->installed_network_mode), "off");
    } else {
        copy_string(target->installed_network_mode, sizeof(target->installed_network_mode), "dhcp");
    }

    append_string(target->manifest, sizeof(target->manifest), "CastleOS install image\n");
    append_string(target->manifest, sizeof(target->manifest), "target=");
    append_string(target->manifest, sizeof(target->manifest), target->path);
    append_string(target->manifest, sizeof(target->manifest), "\nlabel=");
    append_string(target->manifest, sizeof(target->manifest), target->label);
    append_string(target->manifest, sizeof(target->manifest), "\nlocation=");
    append_string(target->manifest, sizeof(target->manifest), target->location);
    append_string(target->manifest, sizeof(target->manifest), "\nuser=");
    append_string(target->manifest, sizeof(target->manifest), install_username);
    append_string(target->manifest, sizeof(target->manifest), "\nshell=");
    append_string(target->manifest, sizeof(target->manifest),
        default_shell_style == SHELL_STYLE_BASH ? "bash" : "castle");
    append_string(target->manifest, sizeof(target->manifest), "\nnetwork=");
    append_string(target->manifest, sizeof(target->manifest), target->installed_network_mode);

    if (target->installed_ip[0] != '\0') {
        append_string(target->manifest, sizeof(target->manifest), "\nip=");
        append_string(target->manifest, sizeof(target->manifest), target->installed_ip);
        append_string(target->manifest, sizeof(target->manifest), "\nnetmask=");
        append_string(target->manifest, sizeof(target->manifest), target->installed_netmask);
        append_string(target->manifest, sizeof(target->manifest), "\ngateway=");
        append_string(target->manifest, sizeof(target->manifest), target->installed_gateway);
    }

    if (target->installed_wifi_ssid[0] != '\0') {
        append_string(target->manifest, sizeof(target->manifest), "\nwifi_ssid=");
        append_string(target->manifest, sizeof(target->manifest), target->installed_wifi_ssid);
    }

    append_string(target->manifest, sizeof(target->manifest), "\npassword_hash=");
    append_u64_string(target->manifest, sizeof(target->manifest), (uint64_t) install_password_hash);
    append_string(target->manifest, sizeof(target->manifest), "\nfilesystem\n");

    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (!vfs_nodes[i].used) {
            continue;
        }

        vfs_build_path(i, path, sizeof(path));
        target->installed_nodes++;

        if (vfs_nodes[i].type == VFS_DIRECTORY) {
            append_string(target->manifest, sizeof(target->manifest), "dir ");
            append_string(target->manifest, sizeof(target->manifest), path);
            append_string(target->manifest, sizeof(target->manifest), "\n");
            continue;
        }

        append_string(target->manifest, sizeof(target->manifest), "file ");
        append_string(target->manifest, sizeof(target->manifest), path);
        append_string(target->manifest, sizeof(target->manifest), " size=");
        append_u64_string(target->manifest, sizeof(target->manifest), (uint64_t) vfs_nodes[i].size);
        append_string(target->manifest, sizeof(target->manifest), "\n");
        append_string(target->manifest, sizeof(target->manifest), vfs_nodes[i].content);
        append_string(target->manifest, sizeof(target->manifest), "\n.\n");
    }

    target->installed_bytes = str_length(target->manifest);
    return 1;
}

static void shell_print_prompt() {
    char path[SHELL_MAX_PATH_LENGTH];

    vfs_build_path(current_directory, path, sizeof(path));

    if (current_shell_style == SHELL_STYLE_BASH) {
        print_str(install_username);
        print_str("@castleos:");
        print_str(path);
        print_str("$ ");
        return;
    }

    print_str("CastleOS:");
    print_str(path);
    print_str("> ");
}

static void shell_render_buffer(size_t start_col,
                                size_t start_row,
                                char* input,
                                size_t length,
                                size_t cursor_index,
                                size_t* rendered_length,
                                int mask_input) {
    size_t screen_cols = print_get_num_cols();
    size_t screen_rows = print_get_num_rows();
    size_t max_cells = screen_cols * screen_rows;
    size_t start = start_row * screen_cols + start_col;
    size_t render_count = length > *rendered_length ? length : *rendered_length;

    for (size_t i = 0; i < render_count && start + i < max_cells; i++) {
        char character = i < length ? input[i] : ' ';

        if (mask_input && i < length) {
            character = '*';
        }

        print_put_at((start + i) % screen_cols, (start + i) / screen_cols, character);
    }

    if (start + cursor_index >= max_cells) {
        cursor_index = max_cells > start ? max_cells - start - 1 : 0;
    }

    print_set_cursor((start + cursor_index) % screen_cols, (start + cursor_index) / screen_cols);
    *rendered_length = length;
}

static void shell_read_buffer(char* input, size_t input_size, int mask_input, int allow_history) {
    char saved_input[SHELL_INPUT_SIZE];
    size_t start_col = print_get_col();
    size_t start_row = print_get_row();
    size_t length = 0;
    size_t cursor_index = 0;
    size_t rendered_length = 0;
    int history_index = -1;

    input[0] = '\0';
    saved_input[0] = '\0';

    while (1) {
        int key = keyboard_getkey();

        if (key == KEY_ENTER) {
            input[length] = '\0';
            shell_render_buffer(start_col, start_row, input, length, length, &rendered_length, mask_input);
            print_newline();
            return;
        }

        if (key == KEY_BACKSPACE) {
            if (cursor_index == 0) {
                continue;
            }

            for (size_t i = cursor_index - 1; i < length; i++) {
                input[i] = input[i + 1];
            }

            cursor_index--;
            length--;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (key == KEY_DELETE) {
            if (cursor_index >= length) {
                continue;
            }

            for (size_t i = cursor_index; i < length; i++) {
                input[i] = input[i + 1];
            }

            length--;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (key == KEY_LEFT) {
            if (cursor_index > 0) {
                cursor_index--;
                shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            }
            continue;
        }

        if (key == KEY_RIGHT) {
            if (cursor_index < length) {
                cursor_index++;
                shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            }
            continue;
        }

        if (key == KEY_HOME) {
            cursor_index = 0;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (key == KEY_END) {
            cursor_index = length;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (allow_history && key == KEY_UP) {
            if (shell_history_count == 0) {
                continue;
            }

            if (history_index < 0) {
                copy_string(saved_input, sizeof(saved_input), input);
                history_index = (int) shell_history_count - 1;
            } else if (history_index > 0) {
                history_index--;
            }

            copy_string(input, input_size, shell_history[history_index]);
            length = str_length(input);
            cursor_index = length;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (allow_history && key == KEY_DOWN) {
            if (history_index < 0) {
                continue;
            }

            if ((size_t) (history_index + 1) < shell_history_count) {
                history_index++;
                copy_string(input, input_size, shell_history[history_index]);
            } else {
                history_index = -1;
                copy_string(input, input_size, saved_input);
            }

            length = str_length(input);
            cursor_index = length;
            shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
            continue;
        }

        if (key < 32 || key > 126 || length + 1 >= input_size) {
            continue;
        }

        for (size_t i = length + 1; i > cursor_index; i--) {
            input[i] = input[i - 1];
        }

        input[cursor_index] = (char) key;
        cursor_index++;
        length++;
        input[length] = '\0';
        history_index = -1;
        shell_render_buffer(start_col, start_row, input, length, cursor_index, &rendered_length, mask_input);
    }
}

static void shell_read_line(char* input, size_t input_size) {
    shell_read_buffer(input, input_size, 0, 1);
}

static void shell_read_password(char* input, size_t input_size) {
    shell_read_buffer(input, input_size, 1, 0);
}

static char install_read_choice(char* prompt, char default_choice) {
    char line[SHELL_INPUT_SIZE];
    char* answer;

    print_str(prompt);
    shell_read_line(line, sizeof(line));

    answer = skip_spaces(line);

    if (!answer) {
        return default_choice;
    }

    return answer[0];
}

static uint32_t hash_password(char* password) {
    uint32_t hash = 2166136261u;

    for (size_t i = 0; password[i] != '\0'; i++) {
        hash ^= (uint8_t) password[i];
        hash *= 16777619u;
    }

    return hash;
}

static void shell_open_nano(int file_index) {
    char path[SHELL_MAX_PATH_LENGTH];

    vfs_build_path(file_index, path, sizeof(path));
    print_clear();
    print_str("CastleOS nano ");
    print_str(path);
    print_newline();
    print_str("ESC = save and exit | BACKSPACE = delete | ENTER = newline");
    print_newline();
    print_newline();
    print_str(vfs_nodes[file_index].content);

    while (1) {
        int key = keyboard_getkey();

        if (key == KEY_ESCAPE) {
            print_clear();
            print_str("Saved ");
            print_str(path);
            return;
        }

        if (key == KEY_BACKSPACE) {
            if (vfs_nodes[file_index].size == 0) {
                continue;
            }

            vfs_nodes[file_index].size--;
            vfs_nodes[file_index].content[vfs_nodes[file_index].size] = '\0';

            print_clear();
            print_str("CastleOS nano ");
            print_str(path);
            print_newline();
            print_str("ESC = save and exit | BACKSPACE = delete | ENTER = newline");
            print_newline();
            print_newline();
            print_str(vfs_nodes[file_index].content);
            continue;
        }

        if (key == KEY_TAB) {
            continue;
        }

        if ((key == KEY_ENTER || (key >= 32 && key <= 126))
            && vfs_nodes[file_index].size + 1 < sizeof(vfs_nodes[file_index].content)) {
            char character = key == KEY_ENTER ? '\n' : (char) key;

            vfs_nodes[file_index].content[vfs_nodes[file_index].size++] = character;
            vfs_nodes[file_index].content[vfs_nodes[file_index].size] = '\0';
            print_char(character);
        }
    }
}

static void shell_print_directory_listing(int directory_index) {
    int found = 0;

    for (int i = 0; i < SHELL_MAX_NODES; i++) {
        if (!vfs_nodes[i].used || vfs_nodes[i].parent != directory_index) {
            continue;
        }

        print_str(vfs_nodes[i].name);

        if (vfs_nodes[i].type == VFS_DIRECTORY) {
            print_char('/');
        }

        print_newline();
        found = 1;
    }

    if (!found) {
        print_str("(empty)");
    }
}

static void shell_record_history(const char* input) {
    if (!input || input[0] == '\0') {
        return;
    }

    if (shell_history_count > 0 && strcmp(shell_history[shell_history_count - 1], (char*) input) == 0) {
        return;
    }

    if (shell_history_count < SHELL_HISTORY_SIZE) {
        copy_string(shell_history[shell_history_count], sizeof(shell_history[shell_history_count]), input);
        shell_history_count++;
        return;
    }

    for (size_t i = 1; i < SHELL_HISTORY_SIZE; i++) {
        copy_string(shell_history[i - 1], sizeof(shell_history[i - 1]), shell_history[i]);
    }

    copy_string(shell_history[SHELL_HISTORY_SIZE - 1],
                sizeof(shell_history[SHELL_HISTORY_SIZE - 1]),
                input);
}

static int command_is_available(struct Command* command) {
    uint8_t shell_flag = current_shell_style == SHELL_STYLE_BASH ? COMMAND_SHELL_BASH : COMMAND_SHELL_CLASSIC;

    if (!(command->flags & shell_flag)) {
        return 0;
    }

    if ((command->flags & COMMAND_REQUIRES_INSTALL) && !os_installed) {
        return 0;
    }

    return 1;
}

static struct Command* find_command_any(char* name) {
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(name, commands[i].name) == 0) {
            return &commands[i];
        }
    }

    return 0;
}

static struct Command* find_command(char* name) {
    struct Command* command = find_command_any(name);

    if (!command || !command_is_available(command)) {
        return 0;
    }

    return command;
}

static void print_pci_device(struct DriverPciDevice device) {
    if (!device.present) {
        print_str("none");
        return;
    }

    print_str("vendor ");
    print_hex_u16(device.vendor_id);
    print_str(" device ");
    print_hex_u16(device.device_id);
    print_str(" at ");
    print_u64(device.bus);
    print_char(':');
    print_u64(device.slot);
    print_char('.');
    print_u64(device.function);
    print_str(" class ");
    print_hex_u8(device.class_code);
    print_char('/');
    print_hex_u8(device.subclass);
}

static void print_driver_status() {
    struct DriverStatus status = drivers_get_status();
    struct NetworkStatus network_status = network_get_status();

    print_str("Core drivers: ");
    print_str(drivers_core_state());
    print_newline();

    print_str("VGA text: ");
    print_str(status.vga_text_loaded ? "loaded" : "missing");
    print_newline();

    print_str("PS/2 keyboard: ");
    print_str(status.ps2_keyboard_loaded ? "loaded" : "missing");
    print_newline();

    print_str("PIT timer: ");
    print_str(status.pit_timer_loaded ? "loaded" : "missing");
    print_newline();

    print_str("Ethernet: ");
    if (network_status.packet_driver_ready) {
        print_str(network_status.packet_driver_name[0] ? network_status.packet_driver_name : "packet driver");
        print_str(" loaded");
    } else if (status.rtl8139_loaded) {
        print_str("RTL8139 detected, packet driver not active");
    } else if (status.e1000_loaded) {
        print_str("e1000 detected, packet driver not active");
    } else {
        print_str("no loaded ethernet driver");
    }
    print_newline();

    print_str("Wireless: ");
    print_str(drivers_wireless_state());
    print_newline();

    print_str("Storage: ");
    print_str(drivers_storage_state());
    print_newline();

    print_str("PCI devices: ");
    print_u64((uint64_t) status.pci_devices);
    print_newline();

    print_str("Network controllers: ");
    print_u64((uint64_t) status.network_devices);
    print_str(" ethernet ");
    print_u64((uint64_t) status.ethernet_devices);
    print_str(" wireless ");
    print_u64((uint64_t) status.wireless_devices);
    print_newline();

    print_str("Storage controllers: ");
    print_u64((uint64_t) status.storage_devices);
    print_str(" ide ");
    print_u64((uint64_t) status.ide_devices);
    print_str(" sata ");
    print_u64((uint64_t) status.sata_devices);
    print_str(" nvme ");
    print_u64((uint64_t) status.nvme_devices);
    print_newline();

    print_str("USB controllers: ");
    print_u64((uint64_t) status.usb_controllers);
    print_str(" uhci ");
    print_u64((uint64_t) status.uhci_controllers);
    print_str(" ohci ");
    print_u64((uint64_t) status.ohci_controllers);
    print_str(" ehci ");
    print_u64((uint64_t) status.ehci_controllers);
    print_str(" xhci ");
    print_u64((uint64_t) status.xhci_controllers);
    print_newline();

    print_str("USB hubs: ");
    print_str(status.usb_hub_stack_modeled ? "host+hub stack modeled" : "no USB host controller detected");
    print_newline();

    print_str("Lenovo 14w profile: ");
    print_str(status.lenovo_14w_profile_suggested ? "suggested" : "not detected");
    print_newline();

    print_str("Driver selections: net ");
    print_str(drivers_selected_driver("network")[0] ? drivers_selected_driver("network") : "(none)");
    print_str(" wifi ");
    print_str(network_status.wifi_driver_selected ? network_status.wifi_selected_driver : "(none)");
    print_str(" storage ");
    print_str(drivers_selected_driver("storage")[0] ? drivers_selected_driver("storage") : "(none)");
    print_str(" usb ");
    print_str(drivers_selected_driver("usb")[0] ? drivers_selected_driver("usb") : "(none)");
}

static void print_network_status() {
    struct NetworkStatus status = network_get_status();

    print_str("Network: ");
    print_str(network_driver_state());
    print_newline();

    print_str("PCI network devices: ");
    print_u64((uint64_t) status.device_count);
    print_newline();

    if (status.device_count > 0) {
        print_str("First NIC: vendor ");
        print_hex_u16(status.first_device.vendor_id);
        print_str(" device ");
        print_hex_u16(status.first_device.device_id);
        print_str(" at ");
        print_u64(status.first_device.bus);
        print_char(':');
        print_u64(status.first_device.slot);
        print_char('.');
        print_u64(status.first_device.function);
        print_newline();
    }

    print_str("Packet driver: ");
    if (status.packet_driver_ready) {
        print_str("ready");
        if (status.packet_driver_name[0]) {
            print_str(" (");
            print_str(status.packet_driver_name);
            print_char(')');
        }
    } else {
        print_str("not loaded");
    }
    print_newline();

    if (status.packet_driver_ready) {
        print_str("MAC: ");
        for (int i = 0; i < NETWORK_MAC_LENGTH; i++) {
            if (i > 0) {
                print_char(':');
            }
            print_hex_u8(status.mac[i]);
        }
        print_newline();
    }

    print_str("Packets sent: ");
    print_u64((uint64_t) status.packets_sent);
    print_newline();
    print_str("Packets received: ");
    print_u64((uint64_t) status.packets_received);
    print_newline();

    print_str("WiFi: ");
    print_str(network_wifi_state());
    if (status.wifi_profile_saved) {
        print_newline();
        print_str("WiFi SSID: ");
        print_str(status.wifi_ssid);
    }
    print_newline();

    print_str("Mode: ");
    if (status.mode == NETWORK_MODE_DHCP) {
        print_str("dhcp");
    } else if (status.mode == NETWORK_MODE_STATIC) {
        print_str("static");
    } else {
        print_str("down");
    }

    if (status.mode == NETWORK_MODE_STATIC) {
        print_newline();
        print_str("IP: ");
        print_str(status.ip);
        print_newline();
        print_str("Netmask: ");
        print_str(status.netmask);
        print_newline();
        print_str("Gateway: ");
        print_str(status.gateway);
    } else if (status.mode == NETWORK_MODE_DHCP) {
        print_newline();
        print_str("IP: ");
        print_str(status.ip);
        print_newline();
        print_str("Netmask: ");
        print_str(status.netmask);
        print_newline();
        print_str("Gateway: ");
        print_str(status.gateway);
    }
}

static char* selected_driver_for_subsystem(char* subsystem) {
    if (strcmp(subsystem, "wifi") == 0) {
        return network_wifi_selected_driver();
    }

    return drivers_selected_driver(subsystem);
}

static char* recommended_driver_for_subsystem(char* subsystem) {
    if (strcmp(subsystem, "wifi") == 0) {
        return network_wifi_recommended_driver();
    }

    return drivers_recommended_driver(subsystem);
}

static int select_driver_for_subsystem(char* subsystem, char* driver_name) {
    if (strcmp(subsystem, "wifi") == 0) {
        return network_wifi_select_driver(driver_name);
    }

    return drivers_select_driver(subsystem, driver_name);
}

static void print_driver_choices_for_subsystem(char* subsystem) {
    int count;
    char* selected;
    char* recommended;

    if (strcmp(subsystem, "wifi") == 0) {
        count = network_wifi_driver_count();
    } else {
        count = drivers_driver_count(subsystem);
    }

    selected = selected_driver_for_subsystem(subsystem);
    recommended = recommended_driver_for_subsystem(subsystem);

    print_str(subsystem);
    print_str(" drivers");
    print_newline();
    print_str("Selected: ");
    print_str(selected && selected[0] ? selected : "(none)");
    print_newline();
    print_str("Recommended: ");
    print_str(recommended && recommended[0] ? recommended : "(none)");

    if (count <= 0) {
        print_newline();
        print_str("No driver choices available");
        return;
    }

    for (int i = 0; i < count; i++) {
        char* option = strcmp(subsystem, "wifi") == 0 ? network_wifi_driver_name(i) : drivers_driver_name(subsystem, i);

        if (!option) {
            continue;
        }

        print_newline();
        print_str(" - ");
        print_str(option);
    }
}

static void rescan_drivers_and_targets() {
    drivers_rescan();
    refresh_install_targets();
}

static void rescan_drivers_network_and_targets() {
    rescan_drivers_and_targets();
    network_init();
}

static void autoselect_install_drivers(char network_choice) {
    char* driver_name;

    drivers_rescan();

    driver_name = drivers_recommended_driver("network");
    if (driver_name && driver_name[0]) {
        drivers_select_driver("network", driver_name);
    }

    driver_name = drivers_recommended_driver("storage");
    if (driver_name && driver_name[0]) {
        drivers_select_driver("storage", driver_name);
    }

    driver_name = drivers_recommended_driver("usb");
    if (driver_name && driver_name[0]) {
        drivers_select_driver("usb", driver_name);
    }

    network_init();

    if (network_choice == '3') {
        driver_name = network_wifi_recommended_driver();
        if (driver_name && driver_name[0]) {
            network_wifi_select_driver(driver_name);
        }
    }

    persist_driver_preferences();
    refresh_install_targets();
}

static void print_current_install_target_and_inventory() {
    print_str("Current install target: ");
    print_str(install_target);
    print_newline();
    print_disk_inventory();
}

static void print_all_subsystem_driver_choices() {
    print_driver_choices_for_subsystem("network");
    print_newline();
    print_newline();
    print_driver_choices_for_subsystem("wifi");
    print_newline();
    print_newline();
    print_driver_choices_for_subsystem("storage");
    print_newline();
    print_newline();
    print_driver_choices_for_subsystem("usb");
}

static void persist_driver_preferences() {
    int etc_directory = vfs_find_child(0, "etc");
    int drivers_file;
    int wifi_file;
    struct NetworkStatus network_status = network_get_status();

    if (etc_directory < 0) {
        return;
    }

    drivers_file = vfs_find_child(etc_directory, "drivers.conf");
    if (drivers_file < 0) {
        drivers_file = vfs_create_file(etc_directory, "drivers.conf", "");
    }

    if (drivers_file >= 0) {
        copy_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), "network=");
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), drivers_selected_driver("network"));
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), "\nstorage=");
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), drivers_selected_driver("storage"));
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), "\nusb=");
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), drivers_selected_driver("usb"));
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), "\nwifi=");
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content),
                      network_status.wifi_driver_selected ? network_status.wifi_selected_driver : "");
        append_string(vfs_nodes[drivers_file].content, sizeof(vfs_nodes[drivers_file].content), "\n");
        vfs_nodes[drivers_file].size = str_length(vfs_nodes[drivers_file].content);
    }

    wifi_file = vfs_find_child(etc_directory, "wifi.conf");
    if (wifi_file >= 0) {
        copy_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), "driver=");
        append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content),
                      network_status.wifi_driver_selected ? network_status.wifi_selected_driver : "");
        append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), "\nssid=");
        append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content),
                      network_status.wifi_profile_saved ? network_status.wifi_ssid : "");
        append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), "\n");
        vfs_nodes[wifi_file].size = str_length(vfs_nodes[wifi_file].content);
    }
}

static void persist_install_target_configuration() {
    int etc_directory = vfs_find_child(0, "etc");
    int install_file;

    if (etc_directory < 0) {
        return;
    }

    install_file = vfs_find_child(etc_directory, "install.conf");
    if (install_file < 0) {
        install_file = vfs_create_file(etc_directory, "install.conf", "");
    }

    if (install_file >= 0) {
        copy_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), "target=");
        append_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), install_target);
        append_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), "\n");
        vfs_nodes[install_file].size = str_length(vfs_nodes[install_file].content);
    }
}

static void build_ssh_host_key_fingerprint(char* buffer, size_t buffer_size) {
    static char digits[] = "0123456789abcdef";
    uint32_t state = 0xCA57D00Du ^ install_password_hash ^ ssh_host_key_seed ^ ((uint32_t) ssh_port << 8);
    const char* sources[] = {install_username, install_target, ssh_last_host, ssh_remote_user};

    copy_string(buffer, buffer_size, "");

    for (size_t source_index = 0; source_index < sizeof(sources) / sizeof(sources[0]); source_index++) {
        const char* source = sources[source_index];
        size_t index = 0;

        while (source[index] != '\0') {
            state = state * 1664525u + (uint32_t) source[index] + 1013904223u;
            index++;
        }
    }

    for (int i = 0; i < 16; i++) {
        uint8_t byte_value;

        state = state * 1664525u + 1013904223u + (uint32_t) i;
        byte_value = (uint8_t) ((state >> 24) & 0xFF);

        if (i > 0) {
            append_char(buffer, buffer_size, ':');
        }

        append_char(buffer, buffer_size, digits[(byte_value >> 4) & 0xF]);
        append_char(buffer, buffer_size, digits[byte_value & 0xF]);
    }
}

static void persist_ssh_configuration() {
    int etc_directory = vfs_find_child(0, "etc");
    int ssh_file;

    if (etc_directory < 0) {
        return;
    }

    build_ssh_host_key_fingerprint(ssh_host_key_fingerprint, sizeof(ssh_host_key_fingerprint));

    ssh_file = vfs_find_child(etc_directory, "ssh.conf");
    if (ssh_file < 0) {
        ssh_file = vfs_create_file(etc_directory, "ssh.conf", "");
    }

    if (ssh_file >= 0) {
        copy_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "enabled=");
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_enabled ? "1" : "0");
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\nport=");
        append_u64_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_port);
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\nuser=");
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_remote_user);
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\nlast_host=");
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_last_host);
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\nhostkey=");
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_host_key_fingerprint);
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\nseed=");
        append_u64_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), ssh_host_key_seed);
        append_string(vfs_nodes[ssh_file].content, sizeof(vfs_nodes[ssh_file].content), "\n");
        vfs_nodes[ssh_file].size = str_length(vfs_nodes[ssh_file].content);
    }
}

static void print_ssh_status() {
    struct NetworkStatus network_status = network_get_status();

    if (ssh_host_key_fingerprint[0] == '\0') {
        build_ssh_host_key_fingerprint(ssh_host_key_fingerprint, sizeof(ssh_host_key_fingerprint));
    }

    print_str("SSH: ");
    print_str(ssh_enabled ? "enabled" : "disabled");
    print_newline();
    print_str("User: ");
    print_str(ssh_remote_user[0] ? ssh_remote_user : install_username);
    print_newline();
    print_str("Port: ");
    print_u64((uint64_t) ssh_port);
    print_newline();
    print_str("Last host: ");
    print_str(ssh_last_host[0] ? ssh_last_host : "(none)");
    print_newline();
    print_str("Host key: ");
    print_str(ssh_host_key_fingerprint);
    print_newline();
    print_str("Link: ");
    print_str(network_driver_state());
    print_newline();
    print_str("Transport: TCP/IP and SSH session transport are not implemented yet");

    if (network_status.packet_driver_ready) {
        print_newline();
        print_str("Packet driver: ");
        print_str(network_status.packet_driver_name[0] ? network_status.packet_driver_name : "ready");
    }
}

static int interactive_select_driver_for_subsystem(char* subsystem) {
    int count;
    char choice;
    int index;
    char* driver_name;
    char* selected;
    char* recommended;

    if (strcmp(subsystem, "wifi") == 0) {
        count = network_wifi_driver_count();
    } else {
        count = drivers_driver_count(subsystem);
    }

    if (count <= 0 || count > 9) {
        print_str("No numbered driver choices available");
        return 0;
    }

    selected = selected_driver_for_subsystem(subsystem);
    recommended = recommended_driver_for_subsystem(subsystem);

    print_str("Choose ");
    print_str(subsystem);
    print_str(" driver");
    print_newline();
    print_str("Selected: ");
    print_str(selected && selected[0] ? selected : "(none)");
    print_newline();
    print_str("Recommended: ");
    print_str(recommended && recommended[0] ? recommended : "(none)");

    for (int i = 0; i < count; i++) {
        driver_name = strcmp(subsystem, "wifi") == 0 ? network_wifi_driver_name(i) : drivers_driver_name(subsystem, i);

        if (!driver_name) {
            continue;
        }

        print_newline();
        print_u64((uint64_t) (i + 1));
        print_str(") ");
        print_str(driver_name);
    }

    print_newline();
    choice = install_read_choice("Driver choice [1]: ", '1');

    if (choice < '1' || choice >= (char) ('1' + count)) {
        print_str("Invalid driver choice");
        return 0;
    }

    index = choice - '1';
    driver_name = strcmp(subsystem, "wifi") == 0 ? network_wifi_driver_name(index) : drivers_driver_name(subsystem, index);

    if (!driver_name || !select_driver_for_subsystem(subsystem, driver_name)) {
        print_str("Could not select that driver");
        return 0;
    }

    return 1;
}

static char* interactive_pick_subsystem() {
    char choice = install_read_choice("Subsystem 1) Network 2) WiFi 3) Storage 4) USB [1]: ", '1');

    if (choice == '2') {
        return "wifi";
    }

    if (choice == '3') {
        return "storage";
    }

    if (choice == '4') {
        return "usb";
    }

    return "network";
}

static void cmd_net_config() {
    char choice;
    char line[SHELL_INPUT_SIZE];
    char ip[NETWORK_MAX_IP_LENGTH];
    char netmask[NETWORK_MAX_IP_LENGTH];
    char gateway[NETWORK_MAX_IP_LENGTH];
    struct NetworkStatus current = network_get_status();

    copy_string(ip, sizeof(ip), current.ip[0] ? current.ip : "10.0.2.15");
    copy_string(netmask, sizeof(netmask), current.netmask[0] ? current.netmask : "255.255.255.0");
    copy_string(gateway, sizeof(gateway), current.gateway[0] ? current.gateway : "10.0.2.2");

    choice = install_read_choice("Net config 1) Status 2) DHCP 3) Static 4) Down 5) Rescan [1]: ", '1');

    if (choice == '2') {
        if (network_enable_dhcp()) {
            print_str("DHCP lease acquired");
        } else {
            print_str("DHCP failed: no lease received");
        }
        print_newline();
        print_network_status();
        return;
    }

    if (choice == '3') {
        print_str("IP [");
        print_str(ip);
        print_str("]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(ip, sizeof(ip), skip_spaces(line));
        }

        print_str("Netmask [");
        print_str(netmask);
        print_str("]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(netmask, sizeof(netmask), skip_spaces(line));
        }

        print_str("Gateway [");
        print_str(gateway);
        print_str("]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(gateway, sizeof(gateway), skip_spaces(line));
        }

        network_set_static(ip, netmask, gateway);
        print_str("Static network settings saved");
        print_newline();
        print_network_status();
        return;
    }

    if (choice == '4') {
        network_disable();
        print_str("Network interface down");
        return;
    }

    if (choice == '5') {
        drivers_rescan();
        network_init();
        print_str("Driver and PCI network scan complete");
        print_newline();
        print_network_status();
        return;
    }

    print_network_status();
}

static void cmd_wifi_config() {
    char choice = install_read_choice("WiFi config 1) Status 2) Scan 3) Drivers 4) Auto driver 5) Choose driver 6) Connect 7) Disconnect [1]: ", '1');
    char ssid[NETWORK_MAX_SSID_LENGTH];
    char password[SHELL_PASSWORD_SIZE];
    struct NetworkStatus current;

    if (choice == '2') {
        drivers_rescan();
        network_init();
        print_wifi_status_details();
        print_newline();
        print_wifi_driver_options();
        return;
    }

    if (choice == '3') {
        print_wifi_status_details();
        print_newline();
        print_wifi_driver_options();
        return;
    }

    if (choice == '4') {
        current = network_get_status();
        if (current.wifi_recommended_driver[0] == '\0' || !network_wifi_select_driver(current.wifi_recommended_driver)) {
            print_str("wifi: no recommended driver available");
            return;
        }
        persist_driver_preferences();
        print_str("WiFi driver selected");
        print_newline();
        print_wifi_status_details();
        return;
    }

    if (choice == '5') {
        if (!interactive_select_driver_for_subsystem("wifi")) {
            return;
        }
        persist_driver_preferences();
        print_str("WiFi driver selected");
        print_newline();
        print_wifi_status_details();
        return;
    }

    if (choice == '6') {
        print_str("SSID: ");
        shell_read_line(ssid, sizeof(ssid));
        if (!skip_spaces(ssid)) {
            print_str("wifi: SSID required");
            return;
        }

        print_str("WiFi password: ");
        shell_read_password(password, sizeof(password));
        network_wifi_connect(skip_spaces(ssid), password);
        persist_driver_preferences();
        print_str("WiFi profile saved");
        print_newline();
        print_str(network_wifi_state());
        return;
    }

    if (choice == '7') {
        network_wifi_disconnect();
        print_str("WiFi disconnected");
        return;
    }

    print_wifi_status_details();
}

static void cmd_ssh_config() {
    char choice = install_read_choice("SSH config 1) Status 2) Enable 3) Disable 4) Set port 5) Set host 6) Set remote user 7) Regenerate host key [1]: ", '1');
    char line[SHELL_INPUT_SIZE];
    uint16_t new_port;

    if (choice == '2') {
        ssh_enabled = 1;
        persist_ssh_configuration();
        print_str("SSH service enabled");
        print_newline();
        print_ssh_status();
        return;
    }

    if (choice == '3') {
        ssh_enabled = 0;
        persist_ssh_configuration();
        print_str("SSH service disabled");
        print_newline();
        print_ssh_status();
        return;
    }

    if (choice == '4') {
        print_str("SSH port [22]: ");
        shell_read_line(line, sizeof(line));
        if (!skip_spaces(line) || !parse_u16_decimal(skip_spaces(line), &new_port) || new_port == 0) {
            print_str("ssh: invalid port");
            return;
        }

        ssh_port = new_port;
        persist_ssh_configuration();
        print_str("SSH port updated");
        print_newline();
        print_ssh_status();
        return;
    }

    if (choice == '5') {
        print_str("SSH host: ");
        shell_read_line(line, sizeof(line));
        if (!skip_spaces(line)) {
            print_str("ssh: host required");
            return;
        }

        copy_string(ssh_last_host, sizeof(ssh_last_host), skip_spaces(line));
        persist_ssh_configuration();
        print_str("SSH host saved");
        print_newline();
        print_ssh_status();
        return;
    }

    if (choice == '6') {
        print_str("Remote user [");
        print_str(ssh_remote_user[0] ? ssh_remote_user : install_username);
        print_str("]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(ssh_remote_user, sizeof(ssh_remote_user), skip_spaces(line));
        }

        persist_ssh_configuration();
        print_str("SSH remote user saved");
        print_newline();
        print_ssh_status();
        return;
    }

    if (choice == '7') {
        ssh_host_key_seed += 0x9E3779B9u;
        persist_ssh_configuration();
        print_str("SSH host key regenerated");
        print_newline();
        print_ssh_status();
        return;
    }

    print_ssh_status();
}

static void cmd_disk_config() {
    char choice = install_read_choice("Disk config 1) Status 2) Choose target 3) Show target 4) Rescan [1]: ", '1');
    char target_choice;
    char line[SHELL_INPUT_SIZE];
    struct InstallTarget* target;

    if (choice == '2') {
        print_install_targets();
        target_choice = install_read_choice("Install target [1]: ", '1');
        target = get_install_target_by_choice(target_choice);

        if (target) {
            copy_string(install_target, sizeof(install_target), target->path);
        } else if (target_choice == (char) ('0' + visible_install_target_count() + 1)) {
            print_str("Custom target [/dev/sdb1]: ");
            shell_read_line(line, sizeof(line));
            if (skip_spaces(line)) {
                copy_string(install_target, sizeof(install_target), skip_spaces(line));
            } else {
                copy_string(install_target, sizeof(install_target), "/dev/sdb1");
            }
        }

        target = find_install_target(install_target);
        if (!target) {
            print_str("disk: target not found");
            print_newline();
            print_disk_inventory();
            return;
        }

        persist_install_target_configuration();
        print_str("Install target set to ");
        print_str(install_target);
        return;
    }

    if (choice == '3') {
        target = find_install_target(install_target);
        if (!target) {
            print_str("disk: current target not found");
            print_newline();
            print_disk_inventory();
            return;
        }

        print_install_target_details(target);
        return;
    }

    if (choice == '4') {
        rescan_drivers_and_targets();
        print_str("Disk inventory refreshed");
        print_newline();
        print_disk_inventory();
        return;
    }

    print_current_install_target_and_inventory();
}

static void cmd_drivers_config() {
    char choice = install_read_choice("Drivers config 1) Status 2) Rescan 3) List subsystem drivers 4) Auto driver 5) Choose driver [1]: ", '1');
    char* subsystem;
    char* driver_name;

    if (choice == '2') {
        rescan_drivers_network_and_targets();
        print_str("Driver scan complete");
        print_newline();
        print_driver_status();
        return;
    }

    if (choice == '3') {
        subsystem = interactive_pick_subsystem();
        print_driver_choices_for_subsystem(subsystem);
        return;
    }

    if (choice == '4') {
        subsystem = interactive_pick_subsystem();
        driver_name = recommended_driver_for_subsystem(subsystem);

        if (!driver_name || !driver_name[0] || !select_driver_for_subsystem(subsystem, driver_name)) {
            print_str("drivers: no recommended driver available");
            return;
        }

        persist_driver_preferences();
        print_str("Auto-selected ");
        print_str(subsystem);
        print_str(" driver: ");
        print_str(selected_driver_for_subsystem(subsystem));
        return;
    }

    if (choice == '5') {
        subsystem = interactive_pick_subsystem();
        if (!interactive_select_driver_for_subsystem(subsystem)) {
            return;
        }

        persist_driver_preferences();
        print_str("Selected ");
        print_str(subsystem);
        print_str(" driver: ");
        print_str(selected_driver_for_subsystem(subsystem));
        return;
    }

    print_driver_status();
}

static int maybe_run_config_flow(struct Command* command, char* args) {
    char* command_args = args;
    char* subcommand;

    if (!command_args) {
        return 0;
    }

    subcommand = next_token(&command_args);
    if (!subcommand || strcmp(subcommand, "config") != 0) {
        return 0;
    }

    if (strcmp(command->name, "net") == 0) {
        cmd_net_config();
        return 1;
    }

    if (strcmp(command->name, "wifi") == 0) {
        cmd_wifi_config();
        return 1;
    }

    if (strcmp(command->name, "ssh") == 0) {
        cmd_ssh_config();
        return 1;
    }

    if (strcmp(command->name, "disk") == 0) {
        cmd_disk_config();
        return 1;
    }

    if (strcmp(command->name, "drivers") == 0) {
        cmd_drivers_config();
        return 1;
    }

    return 0;
}

void shell_init(uint32_t multiboot_info_addr) {
    total_ram_bytes = detect_total_ram(multiboot_info_addr);
    drivers_init();
    network_init();
    vfs_init();
    build_ssh_host_key_fingerprint(ssh_host_key_fingerprint, sizeof(ssh_host_key_fingerprint));
    persist_ssh_configuration();
    refresh_install_targets();
}

static void cmd_help(char* args) {
    (void) args;

    print_str(current_shell_style == SHELL_STYLE_BASH ? "CastleBash commands" : "Classic shell commands");
    print_newline();

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (!command_is_available(&commands[i])) {
            continue;
        }

        print_str(commands[i].name);
        print_str(" - ");
        print_str(commands[i].description);
        print_newline();
    }

    if (current_shell_style == SHELL_STYLE_CASTLE && !os_installed) {
        print_str("Run install to unlock the full CastleBash userland.");
        print_newline();
    }
}

static void cmd_clear(char* args) {
    (void) args;
    print_clear();
}

static void cmd_echo(char* args) {
    if (args) {
        print_str(args);
    }
}

static void cmd_man(char* args) {
    struct Command* command;

    if (!args) {
        print_str("Usage: man <command>");
        return;
    }

    command = find_command(args);

    if (!command) {
        print_str("No manual entry for that command in this shell");
        return;
    }

    print_str(command->name);
    print_str(" - ");
    print_str(command->description);
}

static void cmd_sysinfo(char* args) {
    char cpu_name[49];
    char path[SHELL_MAX_PATH_LENGTH];
    struct DriverStatus driver_status;

    (void) args;

    get_cpu_brand_string(cpu_name, sizeof(cpu_name));
    vfs_build_path(current_directory, path, sizeof(path));
    driver_status = drivers_get_status();

    print_str("CPU: ");
    print_str(cpu_name);
    print_newline();

    print_str("RAM: ");
    if (total_ram_bytes == 0) {
        print_str("unavailable");
    } else {
        print_u64(total_ram_bytes / (1024 * 1024));
        print_str(" MiB");
    }
    print_newline();

    print_str("Terminal: ");
    print_u64(print_get_num_cols());
    print_str("x");
    print_u64(print_get_num_rows());
    print_str(" VGA text mode");
    print_newline();

    print_str("Motherboard: ");
    if (driver_status.first_network.present) {
        print_str("PCI ");
        print_hex_u16(driver_status.first_network.vendor_id);
        print_str(":");
        print_hex_u16(driver_status.first_network.device_id);
    } else {
        print_str("unknown");
    }
    print_newline();

    print_str("PCI devices: ");
    print_u64((uint64_t) driver_status.pci_devices);
    print_newline();

    print_str("Installed: ");
    print_str(os_installed ? "yes" : "no");
    print_newline();

    print_str("Install target: ");
    print_str(install_target);
    print_newline();

    print_str("Password: ");
    print_str(install_password_set ? "set" : "not set");
    print_newline();

    print_str("Network driver: ");
    print_str(drivers_selected_driver("network")[0] ? drivers_selected_driver("network") : "not selected");
    print_newline();

    print_str("WiFi driver: ");
    print_str(network_wifi_selected_driver()[0] ? network_wifi_selected_driver() : "not selected");
    print_newline();

    print_str("Storage driver: ");
    print_str(drivers_selected_driver("storage")[0] ? drivers_selected_driver("storage") : "not selected");
    print_newline();

    print_str("USB driver: ");
    print_str(drivers_selected_driver("usb")[0] ? drivers_selected_driver("usb") : "not selected");
    print_newline();

    print_str("Wireless devices: ");
    print_u64((uint64_t) driver_status.wireless_devices);
    print_newline();

    print_str("Storage devices: ");
    print_u64((uint64_t) driver_status.storage_devices);
    print_newline();

    print_str("USB controllers: ");
    print_u64((uint64_t) driver_status.usb_controllers);
    print_newline();

    print_str("Shell: ");
    print_str(current_shell_style == SHELL_STYLE_BASH ? "CastleBash" : "Castle shell");
    print_newline();

    print_str("Working directory: ");
    print_str(path);
    print_newline();

    print_network_status();
}

static void cmd_cd(char* args) {
    int target;

    if (!args) {
        current_directory = home_directory >= 0 ? home_directory : 0;
        return;
    }

    target = vfs_resolve_path(args);

    if (target < 0) {
        print_str("cd: no such file or directory");
        return;
    }

    if (vfs_nodes[target].type != VFS_DIRECTORY) {
        print_str("cd: not a directory");
        return;
    }

    current_directory = target;
}

static void cmd_ls(char* args) {
    int target = current_directory;

    if (args) {
        target = vfs_resolve_path(args);

        if (target < 0) {
            print_str("ls: no such file or directory");
            return;
        }
    }

    if (vfs_nodes[target].type == VFS_FILE) {
        print_str(vfs_nodes[target].name);
        return;
    }

    shell_print_directory_listing(target);
}

static void cmd_pwd(char* args) {
    char path[SHELL_MAX_PATH_LENGTH];

    (void) args;
    vfs_build_path(current_directory, path, sizeof(path));
    print_str(path);
}

static void cmd_nano(char* args) {
    int file_index;
    int parent;
    char leaf_name[SHELL_MAX_NAME_LENGTH];

    if (!args) {
        print_str("Usage: nano <file>");
        return;
    }

    file_index = vfs_resolve_path(args);

    if (file_index >= 0) {
        if (vfs_nodes[file_index].type != VFS_FILE) {
            print_str("nano: target is a directory");
            return;
        }

        shell_open_nano(file_index);
        return;
    }

    if (!vfs_resolve_parent(args, &parent, leaf_name)) {
        print_str("nano: invalid path");
        return;
    }

    if (vfs_nodes[parent].type != VFS_DIRECTORY) {
        print_str("nano: parent is not a directory");
        return;
    }

    file_index = vfs_create_file(parent, leaf_name, "");

    if (file_index < 0) {
        print_str("nano: could not create file");
        return;
    }

    shell_open_nano(file_index);
}

static void cmd_bash(char* args) {
    (void) args;

    if (current_shell_style == SHELL_STYLE_BASH) {
        print_str("bash: already in CastleBash");
        return;
    }

    current_shell_style = SHELL_STYLE_BASH;
    print_str("CastleBash started");
}

static void cmd_exit(char* args) {
    (void) args;

    if (current_shell_style != SHELL_STYLE_BASH) {
        print_str("exit: already in the classic Castle shell");
        return;
    }

    current_shell_style = SHELL_STYLE_CASTLE;
    print_str("Returned to Castle shell");
}

static void cmd_install(char* args) {
    char line[SHELL_INPUT_SIZE];
    char password[SHELL_PASSWORD_SIZE];
    char password_confirm[SHELL_PASSWORD_SIZE];
    char network_choice;
    char target_choice;
    char confirm_choice;
    struct InstallTarget* selected_target;
    int bin_directory;
    int etc_directory;
    int profile_file;
    int network_file;
    int install_file;
    int wifi_file;
    char ip[NETWORK_MAX_IP_LENGTH] = "10.0.2.15";
    char netmask[NETWORK_MAX_IP_LENGTH] = "255.255.255.0";
    char gateway[NETWORK_MAX_IP_LENGTH] = "10.0.2.2";
    char wifi_ssid[NETWORK_MAX_SSID_LENGTH];
    char wifi_password[SHELL_PASSWORD_SIZE];
    int dhcp_configured = 0;

    (void) args;
    wifi_ssid[0] = '\0';
    wifi_password[0] = '\0';

    print_clear();
    print_str("CastleOS installer");
    print_newline();
    print_str("This installer writes a CastleOS system image into the selected target.");
    print_newline();
    print_str("Controller-backed disks are modeled until native storage drivers arrive.");
    print_newline();
    print_newline();

    print_str("Username [barnaby]: ");
    shell_read_line(line, sizeof(line));
    if (skip_spaces(line)) {
        copy_string(install_username, sizeof(install_username), skip_spaces(line));
    }

    print_str("Password: ");
    shell_read_password(password, sizeof(password));
    print_str("Confirm password: ");
    shell_read_password(password_confirm, sizeof(password_confirm));

    if (password[0] == '\0') {
        print_str("Install cancelled: password cannot be empty");
        return;
    }

    if (strcmp(password, password_confirm) != 0) {
        print_str("Install cancelled: passwords did not match");
        return;
    }

    print_install_targets();
    target_choice = install_read_choice("Install target [1]: ", '1');
    selected_target = get_install_target_by_choice(target_choice);

    if (selected_target) {
        copy_string(install_target, sizeof(install_target), selected_target->path);
    } else if (target_choice == (char) ('0' + visible_install_target_count() + 1)) {
        print_str("Custom target [/dev/sdb1]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(install_target, sizeof(install_target), skip_spaces(line));
        } else {
            copy_string(install_target, sizeof(install_target), "/dev/sdb1");
        }
    } else {
        copy_string(install_target, sizeof(install_target), "/dev/ram0");
    }

    selected_target = find_install_target(install_target);

    if (!selected_target) {
        print_str("Install cancelled: target is not available in the current disk inventory");
        print_newline();
        print_disk_inventory();
        return;
    }

    print_str("Default shell: CastleBash");
    print_newline();
    network_choice = install_read_choice("Networking 1) Ethernet DHCP 2) Ethernet Static 3) WiFi 4) Off [1]: ", '1');

    if (network_choice == '2') {
        print_str("IP [10.0.2.15]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(ip, sizeof(ip), skip_spaces(line));
        }

        print_str("Netmask [255.255.255.0]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(netmask, sizeof(netmask), skip_spaces(line));
        }

        print_str("Gateway [10.0.2.2]: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(gateway, sizeof(gateway), skip_spaces(line));
        }
    } else if (network_choice == '3') {
        print_str("WiFi network scanning is not implemented yet on the current wireless stack.");
        print_newline();
        print_str("WiFi SSID: ");
        shell_read_line(line, sizeof(line));
        if (skip_spaces(line)) {
            copy_string(wifi_ssid, sizeof(wifi_ssid), skip_spaces(line));
        }

        print_str("WiFi password: ");
        shell_read_password(wifi_password, sizeof(wifi_password));
    }

    print_newline();
    print_str("Install summary");
    print_newline();
    print_str("User: ");
    print_str(install_username);
    print_newline();
    print_str("Default shell: ");
    print_str("CastleBash");
    print_newline();
    print_str("Target: ");
    print_str(install_target);
    print_newline();
    print_str("Password: set");
    print_newline();
    print_str("Networking: ");
    if (network_choice == '2') {
        print_str("static ");
        print_str(ip);
    } else if (network_choice == '3') {
        print_str("wifi dhcp ");
        print_str(wifi_ssid[0] ? wifi_ssid : "(no ssid)");
    } else if (network_choice == '4') {
        print_str("off");
    } else {
        print_str("dhcp");
    }
    print_newline();

    confirm_choice = install_read_choice("Install now? y/N: ", 'n');
    if (confirm_choice != 'y' && confirm_choice != 'Y') {
        print_str("Install cancelled");
        return;
    }

    os_installed = 1;
    bash_installed = 1;
    install_password_set = 1;
    install_password_hash = hash_password(password);
    default_shell_style = SHELL_STYLE_BASH;
    current_shell_style = default_shell_style;

    bin_directory = vfs_find_child(0, "bin");
    if (bin_directory >= 0 && vfs_find_child(bin_directory, "bash") < 0) {
        vfs_create_file(bin_directory, "bash", "CastleBash kernel shell\n");
    }

    etc_directory = vfs_find_child(0, "etc");
    if (etc_directory >= 0) {
        profile_file = vfs_find_child(etc_directory, "profile");
        if (profile_file >= 0) {
            copy_string(vfs_nodes[profile_file].content, sizeof(vfs_nodes[profile_file].content),
                default_shell_style == SHELL_STYLE_BASH ? "shell=bash\n" : "shell=castle\n");
            vfs_nodes[profile_file].size = str_length(vfs_nodes[profile_file].content);
        }

        network_file = vfs_find_child(etc_directory, "network.conf");
        if (network_file >= 0) {
            if (network_choice == '2') {
                copy_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "mode=static\n");
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "ip=");
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), ip);
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "\nnetmask=");
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), netmask);
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "\ngateway=");
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), gateway);
                append_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "\n");
            } else if (network_choice == '3') {
                copy_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "mode=wifi-dhcp\n");
            } else if (network_choice == '4') {
                copy_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "mode=down\n");
            } else {
                copy_string(vfs_nodes[network_file].content, sizeof(vfs_nodes[network_file].content), "mode=dhcp\n");
            }
            vfs_nodes[network_file].size = str_length(vfs_nodes[network_file].content);
        }

        install_file = vfs_find_child(etc_directory, "install.conf");
        if (install_file < 0) {
            install_file = vfs_create_file(etc_directory, "install.conf", "");
        }

        if (install_file >= 0) {
            copy_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), "target=");
            append_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), install_target);
            append_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), "\npassword_hash=");
            append_string(vfs_nodes[install_file].content, sizeof(vfs_nodes[install_file].content), "set\n");
            vfs_nodes[install_file].size = str_length(vfs_nodes[install_file].content);
        }

        wifi_file = vfs_find_child(etc_directory, "wifi.conf");
        if (wifi_file >= 0 && wifi_ssid[0] != '\0') {
            copy_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), "ssid=");
            append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), wifi_ssid);
            append_string(vfs_nodes[wifi_file].content, sizeof(vfs_nodes[wifi_file].content), "\npassword=set\n");
            vfs_nodes[wifi_file].size = str_length(vfs_nodes[wifi_file].content);
        }
    }

    if (!ssh_remote_user[0] || strcmp(ssh_remote_user, "barnaby") == 0) {
        copy_string(ssh_remote_user, sizeof(ssh_remote_user), install_username);
    }
    persist_ssh_configuration();

    autoselect_install_drivers(network_choice);

    if (network_choice == '3') {
        struct NetworkStatus install_network_status = network_get_status();

        if (!install_network_status.wifi_driver_selected) {
            print_str("Install warning: no recommended WiFi driver could be auto-selected for detected hardware");
            print_newline();
        }
    }

    if (network_choice == '2') {
        network_set_static(ip, netmask, gateway);
    } else if (network_choice == '3') {
        network_wifi_connect(wifi_ssid, wifi_password);
        dhcp_configured = network_enable_dhcp();
    } else if (network_choice == '4') {
        network_disable();
    } else {
        dhcp_configured = network_enable_dhcp();
    }

    if (!write_install_image(selected_target, network_choice, ip, netmask, gateway, wifi_ssid)) {
        print_str("Install failed: could not write the target image");
        return;
    }

    print_clear();
    print_str("CastleOS install complete");
    print_newline();
    print_str("Installed bash: /bin/bash");
    print_newline();
    print_str("Default shell: ");
    print_str(default_shell_style == SHELL_STYLE_BASH ? "CastleBash" : "Castle shell");
    print_newline();
    print_str("Install target: ");
    print_str(install_target);
    print_newline();
    print_str("Install image bytes: ");
    print_u64((uint64_t) selected_target->installed_bytes);
    print_newline();
    if (network_choice == '1' && !dhcp_configured) {
        print_str("DHCP warning: no lease was acquired during install");
        print_newline();
    }
    print_network_status();
}

static void cmd_net(char* args) {
    char* command;

    if (!args) {
        print_network_status();
        return;
    }

    command = next_token(&args);

    if (!command || strcmp(command, "status") == 0) {
        print_network_status();
        return;
    }

    if (strcmp(command, "rescan") == 0) {
        drivers_rescan();
        network_init();
        print_str("Driver and PCI network scan complete");
        print_newline();
        print_network_status();
        return;
    }

    if (strcmp(command, "up") == 0 || strcmp(command, "dhcp") == 0) {
        if (network_enable_dhcp()) {
            print_str("DHCP lease acquired");
        } else {
            print_str("DHCP failed: no lease received");
        }
        print_newline();
        print_network_status();
        return;
    }

    if (strcmp(command, "down") == 0) {
        network_disable();
        print_str("Network interface down");
        return;
    }

    if (strcmp(command, "static") == 0) {
        char* ip = next_token(&args);
        char* netmask = next_token(&args);
        char* gateway = next_token(&args);

        if (!ip || !netmask || !gateway) {
            print_str("Usage: net static <ip> <netmask> <gateway>");
            return;
        }

        network_set_static(ip, netmask, gateway);
        print_str("Static network settings saved");
        print_newline();
        print_network_status();
        return;
    }

    print_str("Usage: net [status|rescan|up|dhcp|down|static|config]");
}

static void cmd_ping(char* args) {
    int result;

    if (!args) {
        print_str("Usage: ping <ipv4>");
        return;
    }

    result = network_ping(args);

    if (result == NETWORK_PING_DOWN) {
        print_str("ping: network is down");
        return;
    }

    if (result == NETWORK_PING_NO_PACKET_DRIVER) {
        print_str("ping: no packet driver loaded");
        return;
    }

    if (result == NETWORK_PING_BAD_TARGET) {
        print_str("ping: target must be an IPv4 address");
        return;
    }

    if (result == NETWORK_PING_ARP_TIMEOUT) {
        print_str("ping: ARP timeout");
        return;
    }

    if (result == NETWORK_PING_TIMEOUT) {
        print_str("ping: ICMP timeout");
        return;
    }

    print_str("ping: reply received");
}

static void print_wifi_driver_options() {
    int count = network_wifi_driver_count();

    if (count <= 0) {
        print_str("No driver choices available");
        return;
    }

    print_str("Driver choices:");

    for (int i = 0; i < count; i++) {
        char* name = network_wifi_driver_name(i);

        if (!name) {
            continue;
        }

        print_newline();
        print_str(" - ");
        print_str(name);
    }
}

static void print_wifi_status_details() {
    struct NetworkStatus status = network_get_status();

    print_str("WiFi: ");
    print_str(network_wifi_state());

    if (!status.wifi_hardware_present) {
        return;
    }

    print_newline();
    print_str("Controller: vendor ");
    print_hex_u16(status.first_wifi_device.vendor_id);
    print_str(" device ");
    print_hex_u16(status.first_wifi_device.device_id);
    print_str(" at ");
    print_u64(status.first_wifi_device.bus);
    print_char(':');
    print_u64(status.first_wifi_device.slot);
    print_char('.');
    print_u64(status.first_wifi_device.function);

    if (status.wifi_recommended_driver[0] != '\0') {
        print_newline();
        print_str("Recommended driver: ");
        print_str(status.wifi_recommended_driver);
    }

    if (status.wifi_driver_selected) {
        print_newline();
        print_str("Selected driver: ");
        print_str(status.wifi_selected_driver);
    }

    if (status.wifi_profile_saved) {
        print_newline();
        print_str("SSID: ");
        print_str(status.wifi_ssid);
    }
}

static void cmd_wifi(char* args) {
    char* command;
    char* driver_name;
    char ssid[NETWORK_MAX_SSID_LENGTH];
    char password[SHELL_PASSWORD_SIZE];

    if (!args) {
        print_wifi_status_details();
        return;
    }

    command = next_token(&args);

    if (!command || strcmp(command, "status") == 0) {
        print_wifi_status_details();
        return;
    }

    if (strcmp(command, "scan") == 0) {
        drivers_rescan();
        network_init();
        print_wifi_status_details();
        print_newline();
        print_wifi_driver_options();
        return;
    }

    if (strcmp(command, "drivers") == 0) {
        print_wifi_status_details();
        print_newline();
        print_wifi_driver_options();
        return;
    }

    if (strcmp(command, "driver") == 0) {
        if (!args || !skip_spaces(args)) {
            print_wifi_status_details();
            print_newline();
            print_wifi_driver_options();
            return;
        }

        driver_name = skip_spaces(args);

        if (strcmp(driver_name, "auto") == 0) {
            struct NetworkStatus status = network_get_status();

            if (status.wifi_recommended_driver[0] == '\0' || !network_wifi_select_driver(status.wifi_recommended_driver)) {
                print_str("wifi: no recommended driver available");
                return;
            }
        } else if (!network_wifi_select_driver(driver_name)) {
            print_str("wifi: unknown driver choice");
            print_newline();
            print_wifi_driver_options();
            return;
        }
        persist_driver_preferences();

        print_str("WiFi driver selected");
        print_newline();
        print_wifi_status_details();
        return;
    }

    if (strcmp(command, "disconnect") == 0) {
        network_wifi_disconnect();
        print_str("WiFi disconnected");
        return;
    }

    if (strcmp(command, "connect") != 0) {
        print_str("Usage: wifi [status|scan|drivers|driver <name>|driver auto|connect <ssid>|disconnect|config]");
        return;
    }

    if (args && skip_spaces(args)) {
        copy_string(ssid, sizeof(ssid), skip_spaces(args));
    } else {
        print_str("SSID: ");
        shell_read_line(ssid, sizeof(ssid));
    }

    if (!skip_spaces(ssid)) {
        print_str("wifi: SSID required");
        return;
    }

    print_str("WiFi password: ");
    shell_read_password(password, sizeof(password));

    network_wifi_connect(skip_spaces(ssid), password);
    persist_driver_preferences();

    print_str("WiFi profile saved");
    print_newline();
    print_str(network_wifi_state());
}

static void cmd_ssh(char* args) {
    char* command;
    char* value;
    uint16_t new_port;

    if (!args) {
        print_ssh_status();
        return;
    }

    command = next_token(&args);

    if (!command || strcmp(command, "status") == 0) {
        print_ssh_status();
        return;
    }

    if (strcmp(command, "enable") == 0) {
        ssh_enabled = 1;
        persist_ssh_configuration();
        print_str("SSH service enabled");
        print_newline();
        print_ssh_status();
        return;
    }

    if (strcmp(command, "disable") == 0) {
        ssh_enabled = 0;
        persist_ssh_configuration();
        print_str("SSH service disabled");
        print_newline();
        print_ssh_status();
        return;
    }

    if (strcmp(command, "port") == 0) {
        value = args ? next_token(&args) : 0;

        if (!value || !parse_u16_decimal(value, &new_port) || new_port == 0) {
            print_str("Usage: ssh port <1-65535>");
            return;
        }

        ssh_port = new_port;
        persist_ssh_configuration();
        print_str("SSH port updated");
        return;
    }

    if (strcmp(command, "user") == 0) {
        value = args ? skip_spaces(args) : 0;

        if (!value || value[0] == '\0') {
            print_str("Usage: ssh user <name>");
            return;
        }

        copy_string(ssh_remote_user, sizeof(ssh_remote_user), value);
        persist_ssh_configuration();
        print_str("SSH remote user updated");
        return;
    }

    if (strcmp(command, "host") == 0) {
        value = args ? skip_spaces(args) : 0;

        if (!value || value[0] == '\0') {
            print_str("Usage: ssh host <hostname-or-ip>");
            return;
        }

        copy_string(ssh_last_host, sizeof(ssh_last_host), value);
        persist_ssh_configuration();
        print_str("SSH host updated");
        return;
    }

    if (strcmp(command, "hostkey") == 0) {
        value = args ? next_token(&args) : 0;

        if (value && strcmp(value, "regenerate") == 0) {
            ssh_host_key_seed += 0x9E3779B9u;
            persist_ssh_configuration();
            print_str("SSH host key regenerated");
            print_newline();
        }

        print_ssh_status();
        return;
    }

    if (strcmp(command, "connect") == 0) {
        struct NetworkStatus network_status = network_get_status();
        char* host = args ? next_token(&args) : 0;
        char* remote_user = args ? next_token(&args) : 0;

        if (!host) {
            print_str("Usage: ssh connect <hostname-or-ip> [user]");
            return;
        }

        copy_string(ssh_last_host, sizeof(ssh_last_host), host);
        if (remote_user) {
            copy_string(ssh_remote_user, sizeof(ssh_remote_user), remote_user);
        }

        persist_ssh_configuration();
        print_str("SSH profile saved for ");
        print_str(ssh_remote_user);
        print_char('@');
        print_str(ssh_last_host);
        print_newline();

        if (!network_status.packet_driver_ready || !network_status.enabled) {
            print_str("ssh: packet link is not ready yet");
            return;
        }

        print_str("ssh: TCP/IP and SSH session transport are not implemented yet");
        return;
    }

    if (strcmp(command, "config") == 0) {
        cmd_ssh_config();
        return;
    }

    print_str("Usage: ssh [status|enable|disable|port <port>|user <name>|host <host>|connect <host> [user]|hostkey [regenerate]|config]");
}

static void cmd_cat(char* args) {
    char* path;
    int printed_any = 0;

    if (!args) {
        print_str("Usage: cat <file> [...]");
        return;
    }

    while ((path = next_token(&args)) != 0) {
        int node = vfs_resolve_path(path);

        if (node < 0) {
            print_str("cat: no such file");
            return;
        }

        if (vfs_nodes[node].type != VFS_FILE) {
            print_str("cat: target is not a file");
            return;
        }

        if (printed_any) {
            print_newline();
        }

        print_str(vfs_nodes[node].content);
        printed_any = 1;
    }
}

static void cmd_touch(char* args) {
    char* path;

    if (!args) {
        print_str("Usage: touch <file> [...]");
        return;
    }

    while ((path = next_token(&args)) != 0) {
        int node = vfs_resolve_path(path);

        if (node >= 0) {
            if (vfs_nodes[node].type != VFS_FILE) {
                print_str("touch: target is not a file");
                return;
            }
            continue;
        }

        {
            int parent;
            char leaf_name[SHELL_MAX_NAME_LENGTH];

            if (!vfs_resolve_parent(path, &parent, leaf_name)) {
                print_str("touch: invalid path");
                return;
            }

            if (vfs_create_file(parent, leaf_name, "") < 0) {
                print_str("touch: could not create file");
                return;
            }
        }
    }
}

static void cmd_mkdir(char* args) {
    char* path;
    int create_parents = 0;

    if (!args) {
        print_str("Usage: mkdir [-p] <directory> [...]");
        return;
    }

    path = next_token(&args);
    if (path && strcmp(path, "-p") == 0) {
        create_parents = 1;
        path = next_token(&args);
    }

    if (!path) {
        print_str("Usage: mkdir [-p] <directory> [...]");
        return;
    }

    while (path) {
        if (create_parents) {
            if (vfs_make_directory_path(path) < 0) {
                print_str("mkdir: could not create path");
                return;
            }
        } else {
            int existing = vfs_resolve_path(path);

            if (existing >= 0) {
                print_str("mkdir: target already exists");
                return;
            }

            {
                int parent;
                char leaf_name[SHELL_MAX_NAME_LENGTH];

                if (!vfs_resolve_parent(path, &parent, leaf_name)) {
                    print_str("mkdir: invalid path");
                    return;
                }

                if (vfs_create_directory(parent, leaf_name) < 0) {
                    print_str("mkdir: could not create directory");
                    return;
                }
            }
        }

        path = next_token(&args);
    }
}

static void cmd_rm(char* args) {
    char* path;
    int recursive = 0;

    if (!args) {
        print_str("Usage: rm [-r] <path> [...]");
        return;
    }

    path = next_token(&args);
    if (path && (strcmp(path, "-r") == 0 || strcmp(path, "-rf") == 0 || strcmp(path, "-fr") == 0)) {
        recursive = 1;
        path = next_token(&args);
    }

    if (!path) {
        print_str("Usage: rm [-r] <path> [...]");
        return;
    }

    while (path) {
        int node = vfs_resolve_path(path);

        if (node <= 0) {
            print_str("rm: target not found");
            return;
        }

        if (vfs_is_ancestor(node, current_directory)) {
            print_str("rm: refusing to remove the current working directory path");
            return;
        }

        if (vfs_nodes[node].type == VFS_DIRECTORY && !recursive && !vfs_is_directory_empty(node)) {
            print_str("rm: directory not empty");
            return;
        }

        vfs_delete_subtree(node);
        path = next_token(&args);
    }
}

static void cmd_cp(char* args) {
    char* source_path;
    char* destination_path;
    int recursive = 0;
    int source;
    int parent;
    char leaf_name[SHELL_MAX_NAME_LENGTH];

    if (!args) {
        print_str("Usage: cp [-r] <source> <destination>");
        return;
    }

    source_path = next_token(&args);
    if (source_path && strcmp(source_path, "-r") == 0) {
        recursive = 1;
        source_path = next_token(&args);
    }

    destination_path = next_token(&args);

    if (!source_path || !destination_path) {
        print_str("Usage: cp [-r] <source> <destination>");
        return;
    }

    source = vfs_resolve_path(source_path);
    if (source < 0) {
        print_str("cp: source not found");
        return;
    }

    if (vfs_nodes[source].type == VFS_DIRECTORY && !recursive) {
        print_str("cp: use -r to copy directories");
        return;
    }

    if (!vfs_resolve_destination(source, destination_path, &parent, leaf_name)) {
        print_str("cp: invalid destination");
        return;
    }

    if (vfs_copy_node(source, parent, leaf_name) < 0) {
        print_str("cp: copy failed");
    }
}

static void cmd_mv(char* args) {
    char* source_path;
    char* destination_path;
    int source;
    int parent;
    char leaf_name[SHELL_MAX_NAME_LENGTH];

    if (!args) {
        print_str("Usage: mv <source> <destination>");
        return;
    }

    source_path = next_token(&args);
    destination_path = next_token(&args);

    if (!source_path || !destination_path) {
        print_str("Usage: mv <source> <destination>");
        return;
    }

    source = vfs_resolve_path(source_path);
    if (source <= 0) {
        print_str("mv: source not found");
        return;
    }

    if (vfs_is_ancestor(source, current_directory)) {
        print_str("mv: refusing to move the current working directory path");
        return;
    }

    if (!vfs_resolve_destination(source, destination_path, &parent, leaf_name)) {
        print_str("mv: invalid destination");
        return;
    }

    if (!vfs_move_node(source, parent, leaf_name)) {
        print_str("mv: move failed");
    }
}

static void cmd_whoami(char* args) {
    (void) args;
    print_str(install_username);
}

static void cmd_uname(char* args) {
    if (!args || strcmp(args, "-s") == 0) {
        print_str("CastleOS");
        return;
    }

    if (strcmp(args, "-a") == 0) {
        print_str("CastleOS castleos 0.1 x86_64");
        return;
    }

    print_str("Usage: uname [-s|-a]");
}

static void cmd_history(char* args) {
    (void) args;

    for (size_t i = 0; i < shell_history_count; i++) {
        print_u64((uint64_t) (i + 1));
        print_str("  ");
        print_str(shell_history[i]);
        if (i + 1 < shell_history_count) {
            print_newline();
        }
    }
}

static void cmd_hostname(char* args) {
    (void) args;
    print_str("castleos");
}

static void cmd_disk(char* args) {
    char* command;
    struct InstallTarget* target;

    if (!args) {
        print_current_install_target_and_inventory();
        return;
    }

    command = next_token(&args);

    if (!command || strcmp(command, "status") == 0 || strcmp(command, "list") == 0) {
        print_current_install_target_and_inventory();
        return;
    }

    if (strcmp(command, "show") == 0) {
        if (!args || !skip_spaces(args)) {
            print_str("Usage: disk show <disk-or-partition>");
            return;
        }

        target = find_install_target(skip_spaces(args));

        if (!target) {
            print_str("disk: target not found");
            print_newline();
            print_disk_inventory();
            return;
        }

        print_install_target_details(target);
        return;
    }

    if (strcmp(command, "rescan") == 0) {
        rescan_drivers_and_targets();
        print_str("Disk inventory refreshed");
        print_newline();
        print_disk_inventory();
        return;
    }

    if (strcmp(command, "target") != 0) {
        print_str("Usage: disk [status|list|show <path>|rescan|target <disk-or-partition>|config]");
        return;
    }

    if (!args || !skip_spaces(args)) {
        print_str("Usage: disk target <disk-or-partition>");
        return;
    }

    target = find_install_target(skip_spaces(args));

    if (!target) {
        print_str("disk: target not found");
        print_newline();
        print_disk_inventory();
        return;
    }

    copy_string(install_target, sizeof(install_target), target->path);
    persist_install_target_configuration();

    print_str("Install target set to ");
    print_str(install_target);
}

static void cmd_drivers(char* args) {
    char* command = args ? next_token(&args) : 0;
    char* subsystem;
    char* driver_name;
    struct DriverStatus status;

    if (command && strcmp(command, "rescan") == 0) {
        rescan_drivers_network_and_targets();
        print_str("Driver scan complete");
        print_newline();
    } else if (command && strcmp(command, "list") == 0) {
        subsystem = args ? next_token(&args) : 0;

        if (subsystem) {
            print_driver_choices_for_subsystem(subsystem);
            return;
        }

        print_all_subsystem_driver_choices();
        return;
    } else if (command && (strcmp(command, "choose") == 0 || strcmp(command, "select") == 0)) {
        subsystem = args ? next_token(&args) : 0;
        driver_name = args ? next_token(&args) : 0;

        if (!subsystem || !driver_name) {
            print_str("Usage: drivers choose <network|wifi|storage|usb> <driver>");
            return;
        }

        if (!select_driver_for_subsystem(subsystem, driver_name)) {
            print_str("drivers: invalid driver choice");
            print_newline();
            print_driver_choices_for_subsystem(subsystem);
            return;
        }

        persist_driver_preferences();
        print_str("Selected ");
        print_str(subsystem);
        print_str(" driver: ");
        print_str(selected_driver_for_subsystem(subsystem));
        return;
    } else if (command && strcmp(command, "auto") == 0) {
        subsystem = args ? next_token(&args) : 0;
        driver_name = subsystem ? recommended_driver_for_subsystem(subsystem) : 0;

        if (!subsystem) {
            print_str("Usage: drivers auto <network|wifi|storage|usb>");
            return;
        }

        if (!driver_name || !driver_name[0] || !select_driver_for_subsystem(subsystem, driver_name)) {
            print_str("drivers: no recommended driver available");
            return;
        }

        persist_driver_preferences();
        print_str("Auto-selected ");
        print_str(subsystem);
        print_str(" driver: ");
        print_str(selected_driver_for_subsystem(subsystem));
        return;
    } else if (command && strcmp(command, "status") != 0) {
        print_str("Usage: drivers [status|rescan|list [subsystem]|choose <subsystem> <driver>|auto <subsystem>|config]");
        return;
    }

    status = drivers_get_status();
    print_driver_status();

    if (status.first_storage.present) {
        print_newline();
        print_str("First storage controller: ");
        print_pci_device(status.first_storage);
    }

    if (status.first_network.present) {
        print_newline();
        print_str("First network controller: ");
        print_pci_device(status.first_network);
    }

    if (status.first_wireless.present) {
        print_newline();
        print_str("First wireless controller: ");
        print_pci_device(status.first_wireless);
    }

    if (status.first_usb.present) {
        print_newline();
        print_str("First USB controller: ");
        print_pci_device(status.first_usb);
    }
}

static void cmd_pci(char* args) {
    struct DriverStatus status = drivers_get_status();

    (void) args;

    print_str("PCI devices detected: ");
    print_u64((uint64_t) status.pci_devices);
    print_newline();

    print_str("Network devices: ");
    print_u64((uint64_t) status.network_devices);
    print_newline();

    if (status.first_network.present) {
        print_str("First network device: ");
        print_pci_device(status.first_network);
        print_newline();
    }

    print_str("Wireless devices: ");
    print_u64((uint64_t) status.wireless_devices);
    print_newline();

    if (status.first_wireless.present) {
        print_str("First wireless device: ");
        print_pci_device(status.first_wireless);
        print_newline();
    }

    print_str("Storage devices: ");
    print_u64((uint64_t) status.storage_devices);
    print_newline();

    if (status.first_storage.present) {
        print_str("First storage device: ");
        print_pci_device(status.first_storage);
        print_newline();
    }
}

static void cmd_shutdown(char* args) {
    (void) args;

    print_str("Shutting down CastleOS...");

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outw(0x600, 0x0034);

    __asm__ volatile ("cli");

    while (1) {
        __asm__ volatile ("hlt");
    }
}

void shell_run() {
    char input[SHELL_INPUT_SIZE];
    char raw_input[SHELL_INPUT_SIZE];

    print_newline();
    shell_print_prompt();

    while (1) {
        char* cursor;
        char* command;
        char* args = 0;
        struct Command* matched_command = 0;
        struct Command* hidden_command = 0;

        shell_read_line(input, sizeof(input));
        copy_string(raw_input, sizeof(raw_input), input);
        cursor = input;
        command = next_token(&cursor);

        if (!command) {
            shell_print_prompt();
            continue;
        }

        shell_record_history(raw_input);
        args = skip_spaces(cursor);
        matched_command = find_command(command);

        if (matched_command) {
            if (!maybe_run_config_flow(matched_command, args)) {
                matched_command->func(args);
            }
        } else {
            hidden_command = find_command_any(command);

            if (hidden_command && (hidden_command->flags & COMMAND_REQUIRES_INSTALL) && !os_installed) {
                print_str("Install CastleOS and launch CastleBash to use that command");
            } else if (hidden_command && current_shell_style != SHELL_STYLE_BASH && (hidden_command->flags & COMMAND_SHELL_BASH)) {
                print_str("Launch CastleBash with bash to use that command");
            } else {
                print_str(current_shell_style == SHELL_STYLE_BASH ? "bash: command not found" : "Unknown command");
            }
        }

        print_newline();
        shell_print_prompt();
    }
}
