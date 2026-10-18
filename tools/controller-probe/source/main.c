// SPDX-License-Identifier: GPL-3.0-only
#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PROBE_PORT 6803
#define MAX_CLIENTS 4
#define MAX_AUTH_CODE 64
#define MAX_LINE 256
#define MAX_OUTPUT 24576
#define MAX_TRACE 128
#define MAX_AUTH_FRAMES 64

typedef struct {
    uint64_t frame;
    uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t tc, tx, ty, ta;
} TraceEntry;

typedef struct {
    int fd;
    bool authenticated;
    bool close_after_send;
    unsigned int frame_polls;
    char input[MAX_LINE];
    size_t input_length;
    char output[MAX_OUTPUT];
    size_t output_length;
    size_t output_offset;
} Client;

static char g_code[MAX_AUTH_CODE + 1];
static Client g_clients[MAX_CLIENTS];
static TraceEntry g_trace[MAX_TRACE];
static size_t g_trace_head;
static size_t g_trace_count;
static uint64_t g_frame;
static uint64_t g_buttons;
static int32_t g_lx, g_ly, g_rx, g_ry;
static uint64_t g_previous_buttons;
static int32_t g_previous_lx, g_previous_ly, g_previous_rx, g_previous_ry;
static bool g_have_previous;
static uint32_t g_tc, g_tx, g_ty, g_previous_tc, g_previous_tx, g_previous_ty, g_ta, g_previous_ta;

static char* trim(char* text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'
                      || text[length - 1] == '\r' || text[length - 1] == '\n'))
        text[--length] = '\0';
    return text;
}

static bool equal_ascii_case(const char* left, const char* right) {
    while (*left && *right) {
        char a = *left++, b = *right++;
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *left == *right;
}

static bool valid_code(const char* code) {
    const size_t length = strlen(code);
    if (length < 4 || length > MAX_AUTH_CODE) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = code[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

static bool read_pc_settings(void) {
    FILE* file = fopen("sdmc:/switch/Breeze/config.ini", "r");
    if (!file) return false;

    bool in_section = false, found_section = false;
    bool found_enable = false, enabled = false, found_code = false;
    char line[1026];
    char parsed_code[MAX_AUTH_CODE + 1] = {0};
    while (fgets(line, sizeof(line), file)) {
        const size_t length = strlen(line);
        if (length == sizeof(line) - 1 && line[length - 1] != '\n') {
            fclose(file);
            return false;
        }
        char* current = trim(line);
        if (!*current || *current == '#' || *current == ';') continue;
        const size_t current_length = strlen(current);
        if (*current == '[' && current_length >= 2 && current[current_length - 1] == ']') {
            current[current_length - 1] = '\0';
            in_section = equal_ascii_case(trim(current + 1), "PC connect");
            found_section = found_section || in_section;
            continue;
        }
        if (!in_section) continue;
        char* equals = strchr(current, '=');
        if (!equals) continue;
        *equals++ = '\0';
        char* key = trim(current);
        char* value = trim(equals);
        char* comment = strpbrk(value, ";#");
        if (comment) *comment = '\0';
        value = trim(value);
        if (equal_ascii_case(key, "enable")) {
            found_enable = true;
            enabled = equal_ascii_case(value, "true") || equal_ascii_case(value, "yes")
                || equal_ascii_case(value, "on") || strcmp(value, "1") == 0;
        } else if (equal_ascii_case(key, "code")) {
            found_code = true;
            if (strlen(value) > MAX_AUTH_CODE) {
                fclose(file);
                return false;
            }
            memcpy(parsed_code, value, strlen(value) + 1);
        }
    }
    const bool read_error = ferror(file) != 0;
    fclose(file);
    if (read_error || !found_section || !found_enable || !enabled || !found_code
        || !valid_code(parsed_code))
        return false;
    memcpy(g_code, parsed_code, strlen(parsed_code) + 1);
    memset(parsed_code, 0, sizeof(parsed_code));
    return true;
}

static void record_input_change(void) {
    if (g_have_previous && g_buttons == g_previous_buttons && g_lx == g_previous_lx
        && g_ly == g_previous_ly && g_rx == g_previous_rx && g_ry == g_previous_ry
        && g_tc == g_previous_tc && g_tx == g_previous_tx && g_ty == g_previous_ty && g_ta == g_previous_ta)
        return;
    TraceEntry* entry = &g_trace[g_trace_head];
    entry->frame = g_frame;
    entry->buttons = g_buttons;
    entry->lx = g_lx; entry->ly = g_ly;
    entry->rx = g_rx; entry->ry = g_ry;
    entry->tc = g_tc; entry->tx = g_tx; entry->ty = g_ty; entry->ta = g_ta;
    g_trace_head = (g_trace_head + 1) % MAX_TRACE;
    if (g_trace_count < MAX_TRACE) ++g_trace_count;
    g_previous_buttons = g_buttons;
    g_previous_lx = g_lx; g_previous_ly = g_ly;
    g_previous_rx = g_rx; g_previous_ry = g_ry;
    g_previous_tc = g_tc; g_previous_tx = g_tx; g_previous_ty = g_ty; g_previous_ta = g_ta;
    g_have_previous = true;
}

static bool queue_text(Client* client, const char* text) {
    const size_t length = strlen(text);
    if (length > sizeof(client->output) - client->output_length) return false;
    memcpy(client->output + client->output_length, text, length);
    client->output_length += length;
    return true;
}

static bool queue_snapshot(Client* client) {
    char* out = client->output + client->output_length;
    size_t left = sizeof(client->output) - client->output_length;
    int written = snprintf(out, left,
        "{\"frame\":%llu,\"buttons\":\"0x%016llX\",\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"touch_count\":%u,\"touch_x\":%u,\"touch_y\":%u,\"touch_attributes\":%u,\"trace\":[",
        (unsigned long long)g_frame, (unsigned long long)g_buttons,
        g_lx, g_ly, g_rx, g_ry, g_tc, g_tx, g_ty, g_ta);
    if (written < 0 || (size_t)written >= left) return false;
    client->output_length += (size_t)written;

    const size_t first = (g_trace_head + MAX_TRACE - g_trace_count) % MAX_TRACE;
    for (size_t i = 0; i < g_trace_count; ++i) {
        const TraceEntry* entry = &g_trace[(first + i) % MAX_TRACE];
        out = client->output + client->output_length;
        left = sizeof(client->output) - client->output_length;
        written = snprintf(out, left,
            "%s{\"f\":%llu,\"b\":\"0x%016llX\",\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"tc\":%u,\"tx\":%u,\"ty\":%u,\"ta\":%u}",
            i ? "," : "", (unsigned long long)entry->frame,
            (unsigned long long)entry->buttons,
            entry->lx, entry->ly, entry->rx, entry->ry, entry->tc, entry->tx, entry->ty, entry->ta);
        if (written < 0 || (size_t)written >= left) return false;
        client->output_length += (size_t)written;
    }
    out = client->output + client->output_length;
    left = sizeof(client->output) - client->output_length;
    written = snprintf(out, left, "]}\n");
    if (written < 0 || (size_t)written >= left) return false;
    client->output_length += (size_t)written;
    return true;
}

static void process_line(Client* client) {
    client->input[client->input_length] = '\0';
    char* line = trim(client->input);
    if (!client->authenticated) {
        if (strncmp(line, "auth ", 5) == 0 && strcmp(line + 5, g_code) == 0) {
            client->authenticated = true;
            client->frame_polls = 0;
            (void)queue_text(client, "OK auth\n");
        } else {
            (void)queue_text(client, "ERR auth\n");
            client->close_after_send = true;
        }
    } else if (strcmp(line, "snapshot") == 0) {
        if (!queue_snapshot(client)) {
            (void)queue_text(client, "ERR snapshot-too-large\n");
            client->close_after_send = true;
        }
    } else {
        (void)queue_text(client, "ERR command\n");
    }
    client->input_length = 0;
}

static void close_client(Client* client) {
    if (client->fd >= 0) close(client->fd);
    memset(client, 0, sizeof(*client));
    client->fd = -1;
}

static void accept_clients(int listener) {
    for (int count = 0; count < MAX_CLIENTS; ++count) {
        const int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            return;
        }
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd);
            continue;
        }
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i)
            if (g_clients[i].fd < 0) { slot = i; break; }
        if (slot < 0) { close(fd); continue; }
        Client* client = &g_clients[slot];
        memset(client, 0, sizeof(*client));
        client->fd = fd;
        (void)queue_text(client, "PROBE 1\n");
    }
}

static void service_client(Client* client) {
    if (client->fd < 0) return;
    if (++client->frame_polls > MAX_AUTH_FRAMES) {
        close_client(client);
        return;
    }

    if (client->output_offset < client->output_length) {
        const ssize_t sent = send(client->fd, client->output + client->output_offset,
            client->output_length - client->output_offset, 0);
        if (sent > 0) client->output_offset += (size_t)sent;
        else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close_client(client);
            return;
        }
        if (client->output_offset == client->output_length) {
            client->output_offset = client->output_length = 0;
            if (client->close_after_send) { close_client(client); return; }
        }
        if (client->output_length != 0) return;
    }

    char incoming[512];
    const ssize_t received = recv(client->fd, incoming, sizeof(incoming), 0);
    if (received == 0) { close_client(client); return; }
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            close_client(client);
        return;
    }
    for (ssize_t i = 0; i < received && client->fd >= 0; ++i) {
        const char c = incoming[i];
        if (c == '\n') {
            process_line(client);
            if (client->output_length > client->output_offset) break;
        } else if (c != '\r') {
            if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) {
                (void)queue_text(client, "ERR invalid-line\n");
                client->close_after_send = true;
                break;
            }
            if (client->input_length + 1 >= sizeof(client->input)) {
                (void)queue_text(client, "ERR line-too-long\n");
                client->close_after_send = true;
                break;
            }
            client->input[client->input_length++] = c;
        }
    }

    if (client->output_offset < client->output_length) {
        const ssize_t sent = send(client->fd, client->output + client->output_offset,
            client->output_length - client->output_offset, 0);
        if (sent > 0) client->output_offset += (size_t)sent;
        else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close_client(client);
            return;
        }
        if (client->output_offset == client->output_length) {
            client->output_offset = client->output_length = 0;
            if (client->close_after_send) close_client(client);
        }
    }
}

static int open_listener(void) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(PROBE_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0 || listen(fd, MAX_CLIENTS) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void update_pad(PadState* pad) {
    padUpdate(pad);
    g_buttons = padGetButtons(pad);
    const HidAnalogStickState left = padGetStickPos(pad, 0);
    const HidAnalogStickState right = padGetStickPos(pad, 1);
    g_lx = left.x; g_ly = left.y;
    g_rx = right.x; g_ry = right.y;
    HidTouchScreenState touch = {0};
    g_tc = g_tx = g_ty = g_ta = 0;
    if (hidGetTouchScreenStates(&touch, 1) > 0 && touch.count) {
        g_tc = touch.count; g_tx = touch.touches[0].x; g_ty = touch.touches[0].y; g_ta = touch.touches[0].attributes;
    }
    ++g_frame;
    record_input_change();
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    for (int i = 0; i < MAX_CLIENTS; ++i) g_clients[i].fd = -1;

    if (!read_pc_settings()) {
        printf("SwitchU Controller Probe\n\nPC connect config is missing, disabled, or invalid.\nTCP 6803 remains closed.\n");
        while (appletMainLoop()) { consoleUpdate(NULL); svcSleepThread(50000000ULL); }
        consoleExit(NULL);
        return 1;
    }

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        printf("socket initialization failed: 0x%08X\n", rc);
        while (appletMainLoop()) { consoleUpdate(NULL); svcSleepThread(50000000ULL); }
        consoleExit(NULL);
        memset(g_code, 0, sizeof(g_code));
        return 1;
    }

    const int listener = open_listener();
    if (listener < 0) {
        printf("TCP 6803 listener failed.\n");
        socketExit();
        while (appletMainLoop()) { consoleUpdate(NULL); svcSleepThread(50000000ULL); }
        consoleExit(NULL);
        memset(g_code, 0, sizeof(g_code));
        return 1;
    }

    hidInitializeTouchScreen();
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);
    printf("SwitchU Controller Probe\nPort 6803 | read-only HID | trace %d\n", MAX_TRACE);

    while (appletMainLoop()) {
        update_pad(&pad);
        accept_clients(listener);
        for (int i = 0; i < MAX_CLIENTS; ++i) service_client(&g_clients[i]);
        if ((g_frame % 10) == 0) {
            printf("\x1b[4;0HFrame: %llu    Trace: %u/128    Auth clients: ",
                (unsigned long long)g_frame, (unsigned)g_trace_count);
            unsigned int authenticated = 0;
            for (int i = 0; i < MAX_CLIENTS; ++i)
                authenticated += g_clients[i].fd >= 0 && g_clients[i].authenticated;
            printf("%u/4\nButtons: 0x%016llX\nLeft:  %6d %6d\nRight: %6d %6d\nTouch: %u at (%u, %u)      \n",
                authenticated, (unsigned long long)g_buttons,
                g_lx, g_ly, g_rx, g_ry, g_tc, g_tx, g_ty);
            consoleUpdate(NULL);
        }
        svcSleepThread(16000000ULL);
    }

    for (int i = 0; i < MAX_CLIENTS; ++i) close_client(&g_clients[i]);
    close(listener);
    socketExit();
    memset(g_code, 0, sizeof(g_code));
    consoleExit(NULL);
    return 0;
}
