/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * GstD-camera-single-stream-example
 *
 * Description:
 * This application demonstrates controlling a single camera stream
 * using GstD (GStreamer Daemon). It supports live preview, MP4
 * recording, and raw YUV frame dumping. The camera resolution can be
 * configured at runtime via command-line arguments.
 *
 * The application provides a menu-driven interface to create, play,
 * pause, stop, and delete the GStreamer pipeline without restarting
 * the application. It also performs clean pipeline teardown on exit
 * or when system signals are received.
 *
 * Usage:
 * GstD-camera-single-stream-example -o <output_type> [options]
 *
 * Help:
 * GstD-camera-single-stream-example --help
 *
 * Output Types:
 *   0 - Live Preview (waylandsink)
 *   1 - MP4 Encode (H.264 to /opt/output.mp4)
 *   2 - YUV Dump (raw frames to /opt/output.yuv)
 *
 * Parameters:
 *   -o <type>   Output type (required)
 *   -W <width>  Camera frame width (default: 1920)
 *   -H <height> Camera frame height (default: 1080)
 *   -a <ip>     GstD IP address (auto-detected if omitted)
 *   -p <port>   GstD TCP port (auto-selected if omitted)
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <signal.h>

/* --- Configuration ------------------------------------------- */
#define DEFAULT_WIDTH     1920
#define DEFAULT_HEIGHT    1080
#define PIPELINE_NAME     "cam_single"
#define GSTD_BIN          "/usr/bin/gstd"
#define PORT_SEARCH_START 5000
#define PORT_SEARCH_END   5100
#define IP_STR_SIZE    64
#define PORT_STR_SIZE  16
#define LOOPBACK_IP "127.0.0.1"
/* ─── Global state for signal handler ──────────────────────── */
static char g_ip[64]   = "";
static char g_port[16] = "";
static volatile sig_atomic_t g_pipeline_active = 0;
static pid_t g_gstd_pid = -1;

/* ─── Cleanup on signal ─────────────────────────────────────── */
static void handle_exit_signal(int signum)
{
    (void)signum;

    if (g_pipeline_active) {
        printf("\n[INFO] Signal received — tearing down pipeline...\n");

        char cmd[256];

        snprintf(cmd, sizeof(cmd),
                 "gst-client-1.0 -a %s -p %s event_eos " PIPELINE_NAME,
                 g_ip, g_port);
        int ret = system(cmd);
        if (ret != 0)
            fprintf(stderr, "[WARN] EOS failed: %d\n", ret);

        sleep(1);

        snprintf(cmd, sizeof(cmd),
                 "gst-client-1.0 -a %s -p %s pipeline_stop " PIPELINE_NAME,
                 g_ip, g_port);
        ret = system(cmd);
        if (ret != 0)
            fprintf(stderr, "[WARN] Stop failed: %d\n", ret);

        snprintf(cmd, sizeof(cmd),
                 "gst-client-1.0 -a %s -p %s pipeline_delete " PIPELINE_NAME,
                 g_ip, g_port);
        ret = system(cmd);
        if (ret != 0)
            fprintf(stderr, "[WARN] Delete failed: %d\n", ret);

        printf("[INFO] Pipeline teardown complete.\n");
    }

    _exit(0);
}

/* --- Dynamic IP Detection ------------------------------------ */
static void detect_local_ip(char *ip, size_t len)
{
    struct ifaddrs *ifaddr, *ifa;
    char candidate[INET_ADDRSTRLEN];
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        strncpy(ip, "127.0.0.1", len - 1);
        ip[len - 1] = '\0';
        return;
    }

    /* Pass 1: Prefer wireless interfaces (wl*) */
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
            continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        if (strncmp(ifa->ifa_name, "wl", 2) != 0)   /* only wl* interfaces */
            continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, candidate, sizeof(candidate));
        strncpy(ip, candidate, len - 1);
        ip[len - 1] = '\0';
        found = 1;
        break;
    }

    /* Pass 2: Fallback � any non-loopback IPv4 (eth*, usb*, etc.) */
    if (!found) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;

            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sa->sin_addr, candidate, sizeof(candidate));
            strncpy(ip, candidate, len - 1);
            ip[len - 1] = '\0';
            found = 1;
            break;
        }
    }

    /* Pass 3: Last resort */
    if (!found) {
        strncpy(ip, LOOPBACK_IP, len - 1);
        ip[len - 1] = '\0';
    }

    freeifaddrs(ifaddr);
}

/* --- Dynamic Port Detection ---------------------------------- */
static int find_available_port(int start, int end)
{
    for (int port = start; port < end; port++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = INADDR_ANY;

        int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);

        if (rc == 0) {
            printf("[INFO] Found available TCP port: %d\n", port);
            return port;
        }
    }

    fprintf(stderr, "[ERROR] No available port in range [%d, %d)\n", start, end);
    return -1;
}

/* --- Launch GstD --------------------------------------------- */
static int launch_gstd(const char *ip, int port)
{
    if (access(GSTD_BIN, X_OK) != 0) {
        fprintf(stderr, "[ERROR] %s not found or not executable: %s\n",
                GSTD_BIN, strerror(errno));
        return -1;
    }

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    pid_t pid = fork();
    if (pid < 0) {
        perror("[ERROR] fork");
        return -1;
    }

    if (pid == 0) {
        setsid();
        execl(GSTD_BIN, GSTD_BIN, "-a", ip, "-p", port_str, (char *)NULL);
        perror("[ERROR] execl gstd");
        _exit(1);
    }

    g_gstd_pid = pid;
    printf("[INFO] Launched gstd (pid=%d) with -a %s -p %s\n",
           (int)pid, ip, port_str);
    sleep(2);

    return 0;
}

/* --- GstD command helper ------------------------------------- */
static void run_gstd(const char *ip, const char *port, const char *cmd)
{
    char buf[2048];
    snprintf(buf, sizeof(buf),
             "gst-client-1.0 -a %s -p %s %s", ip, port, cmd);
    printf("[GstD CMD] %s\n", buf);
    int ret = system(buf);
    if (ret != 0) {
        fprintf(stderr, "[WARN] gst-client-1.0 exited with code: %d\n", ret);
    }
}

/* --- Pipeline string builder --------------------------------- */
static void build_pipeline(char *out, size_t sz,
                            int output_type,
                            int width, int height)
{
    const char *caps_fmt =
        "video/x-raw,format=NV12_Q08C,width=%d,height=%d,framerate=30/1";

    char caps[256];
    snprintf(caps, sizeof(caps), caps_fmt, width, height);

    switch (output_type) {
    case 0: /* Preview */
        snprintf(out, sz,
            "pipeline_create " PIPELINE_NAME
            " \"qtiqmmfsrc name=camsrc video_0::type=preview"
            " ! %s"
            " ! waylandsink fullscreen=true async=true sync=false\"",
            caps);
        break;

    case 1: /* MP4 Encode */
        snprintf(out, sz,
            "pipeline_create " PIPELINE_NAME
            " \"qtiqmmfsrc name=camsrc video_0::type=preview"
            " ! %s"
            " ! queue"
            " ! v4l2h264enc capture-io-mode=4 output-io-mode=5 extra-controls=\\\"controls,video_bitrate=6000000;\\\""
            " ! queue"
            " ! h264parse"
            " ! mp4mux"
            " ! queue"
            " ! filesink location=/opt/output.mp4\"",
            caps);
        break;

    case 2: /* YUV Dump */
        snprintf(out, sz,
            "pipeline_create " PIPELINE_NAME
            " \"qtiqmmfsrc name=camsrc video_0::type=preview"
            " ! %s"
            " ! filesink location=/opt/output.yuv\"",
            caps);
        break;

    default:
        fprintf(stderr, "[ERROR] Unknown output type: %d\n", output_type);
        out[0] = '\0';
        break;
    }
}

/* --- Banner -------------------------------------------------- */
static void print_banner(const char *ip, const char *port,
                         int output_type, int width, int height)
{
    const char *out_names[] = { "Preview", "MP4 Encode", "YUV Dump" };
    const char *out_str = (output_type >= 0 && output_type <= 2)
                          ? out_names[output_type] : "Unknown";

    printf("\n+------------------------------------------+\n");
    printf(  "�  GstD Camera Single Stream Example       �\n");
    printf(  "�------------------------------------------�\n");
    printf(  "�  GstD IP   : %-28s�\n", ip);
    printf(  "�  GstD Port : %-28s�\n", port);
    printf(  "�  Pipeline  : %-28s�\n", PIPELINE_NAME);
    printf(  "�  Output    : %d (%-24s)�\n", output_type, out_str);
    printf(  "�  Resolution: %dx%-20d�\n", width, height);
    printf(  "+------------------------------------------+\n\n");
}

/* --- Menu ---------------------------------------------------- */
static void print_menu(void)
{
    printf("\n+--------------------------------------+\n");
    printf(  "�     GstD Camera Pipeline Menu        �\n");
    printf(  "�--------------------------------------�\n");
    printf(  "�  1. Create & Play Pipeline           �\n");
    printf(  "�  2. Pause Pipeline                   �\n");
    printf(  "�  3. Resume Pipeline (Play)           �\n");
    printf(  "�  4. Send EOS to Pipeline             �\n");
    printf(  "�  5. Stop Pipeline                    �\n");
    printf(  "�  6. Delete Pipeline                  �\n");
    printf(  "�  7. Full Teardown (EOS+Stop+Delete)  �\n");
    printf(  "�  0. Exit                             �\n");
    printf(  "+--------------------------------------+\n");
    printf("Enter choice: ");
    fflush(stdout);
}

/* --- Usage --------------------------------------------------- */
static void usage(const char *prog)
{
    printf("Usage: %s -o <output_type> [-W <width>] [-H <height>]"
           " [-a <ip>] [-p <port>]\n\n", prog);
    printf("  -o  Output type:\n");
    printf("        0 = Preview (waylandsink)\n");
    printf("        1 = MP4 Encode (filesink /opt/output.mp4)\n");
    printf("        2 = YUV Dump  (filesink /opt/output.yuv)\n");
    printf("  -W  Width  (default: %d)\n", DEFAULT_WIDTH);
    printf("  -H  Height (default: %d)\n", DEFAULT_HEIGHT);
    printf("  -a  GstD IP address (auto-detected if not specified)\n");
    printf("  -p  GstD TCP port   (auto-detected if not specified)\n\n");
}

/* --- main ---------------------------------------------------- */
int main(int argc, char *argv[])
{
    int  output_type  = -1;
    int  width        = DEFAULT_WIDTH;
    int  height       = DEFAULT_HEIGHT;
    char ip[IP_STR_SIZE]       = "";          /* empty = auto-detect */
    char port[PORT_STR_SIZE]     = "";

    /* -- Parse arguments -- */
    for (int i = 1; i < argc; i++) {
    if      (!strcmp(argv[i], "-o") && i+1 < argc) output_type = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-W") && i+1 < argc) width       = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-H") && i+1 < argc) height      = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-a") && i+1 < argc) snprintf(ip, sizeof(ip), "%s", argv[++i]);
    else if (!strcmp(argv[i], "-p") && i+1 < argc) snprintf(port, sizeof(port), "%s", argv[++i]);
    }

    if (output_type < 0 || output_type > 2) {
        fprintf(stderr, "[ERROR] Please specify a valid output type with -o (0-2)\n\n");
        usage(argv[0]);
        return 1;
    }

    /* --- Detect IP if not provided --- */
    if (ip[0] == '\0')
        detect_local_ip(ip, sizeof(ip));

    /* --- Detect port if not provided --- */
    if (port[0] == '\0') {
        int p = find_available_port(5000, 5100);
        if (p == -1) {
            fprintf(stderr, "Error: no available port found in range 5000-5100\n");
            return 1;
        }
        snprintf(port, sizeof(port), "%d", p);
    }

    printf("[INFO] Using IP: %s  Port: %s\n", ip, port);

    /* --- Launch gstd AFTER ip+port are finalized --- */
    launch_gstd(ip, atoi(port));

    print_banner(ip, port, output_type, width, height);

        /* --- Copy to globals for signal handler --- */
    snprintf(g_ip,   sizeof(g_ip),   "%s", ip);
    snprintf(g_port, sizeof(g_port), "%s", port);
    /* --- Register signal handlers --- */
    signal(SIGINT,  handle_exit_signal);   /* Ctrl+C */
    signal(SIGTERM, handle_exit_signal);   /* kill / system shutdown */

    /* -- Menu loop -- */
    int running = 1;
    while (running) {
        print_menu();

        int choice = -1;
        char line[32];
        if (fgets(line, sizeof(line), stdin))
        choice = atoi(line);

        char cmd[2048];

        switch (choice) {

        case 1: /* Create & Play */
            printf("\n[INFO] Creating pipeline: %s\n", PIPELINE_NAME);
            build_pipeline(cmd, sizeof(cmd), output_type, width, height);
            if (cmd[0]) {
                run_gstd(ip, port, cmd);
                printf("\n[INFO] Playing pipeline: %s\n", PIPELINE_NAME);
                snprintf(cmd, sizeof(cmd), "pipeline_play " PIPELINE_NAME);
                run_gstd(ip, port, cmd);
            }
            g_pipeline_active = 1;
            break;

        case 2: /* Pause */
            printf("\n[INFO] Pausing pipeline: %s\n", PIPELINE_NAME);
            snprintf(cmd, sizeof(cmd), "pipeline_pause " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            break;

        case 3: /* Resume / Play */
            printf("\n[INFO] Resuming pipeline: %s\n", PIPELINE_NAME);
            snprintf(cmd, sizeof(cmd), "pipeline_play " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            break;

        case 4: /* EOS */
            printf("\n[INFO] Sending EOS to pipeline: %s\n", PIPELINE_NAME);
            snprintf(cmd, sizeof(cmd), "event_eos " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            break;

        case 5: /* Stop */
            printf("\n[INFO] Stopping pipeline: %s\n", PIPELINE_NAME);
            snprintf(cmd, sizeof(cmd), "pipeline_stop " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            break;

        case 6: /* Delete */
            printf("\n[INFO] Deleting pipeline: %s\n", PIPELINE_NAME);
            snprintf(cmd, sizeof(cmd), "pipeline_delete " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            g_pipeline_active = 0;
            break;

        case 7: /* Full Teardown */
            printf("\n[INFO] Full teardown: EOS -> Stop -> Delete\n");

            printf("[INFO] Sending EOS...\n");
            snprintf(cmd, sizeof(cmd), "event_eos " PIPELINE_NAME);
            run_gstd(ip, port, cmd);
            sleep(1);

            printf("[INFO] Stopping pipeline...\n");
            snprintf(cmd, sizeof(cmd), "pipeline_stop " PIPELINE_NAME);
            run_gstd(ip, port, cmd);

            printf("[INFO] Deleting pipeline...\n");
            snprintf(cmd, sizeof(cmd), "pipeline_delete " PIPELINE_NAME);
            run_gstd(ip, port, cmd);

            printf("[INFO] Teardown complete.\n");
            g_pipeline_active = 0;
            break;

        case 0: /* Exit */
            printf("\n[INFO] Exiting. Goodbye!\n");
            running = 0;
            handle_exit_signal(0);
            break;

        default:
            printf("\n[WARN] Invalid choice. Please enter 0-7.\n");
            break;
        }
    }

    return 0;
}
