/*
 * Copyright 2026 Kartik Gohil
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * examples/c/basic.c
 *
 * C example demonstrating the libuspmtp C API:
 *
 *   1. GET Device.DeviceInfo. and print all returned parameters.
 *   2. Subscribe to Device.DeviceInfo. for ValueChange notifications.
 *   3. On each notification, re-GET Device.DeviceInfo. and print the
 *      updated parameter values.
 *
 * The notification callback is invoked from the library's internal worker
 * thread and MUST NOT call any usp_controller_* functions (deadlock risk).
 * Instead the callback writes one byte to a self-pipe; the main thread
 * reads from the pipe and performs the follow-up GET safely.
 *
 * Configuration (environment variables take precedence over argv):
 *   USP_SOCKET_PATH       – path to the OB-USPA UNIX domain socket
 *   USP_APP_ENDPOINT_ID   – this application's USP endpoint ID (from_id)
 *   USP_AGENT_ENDPOINT_ID – the USP agent's endpoint ID (to_id)
 *
 * Fallback order: env var → argv[N] → built-in default.
 */

#include "libuspmtp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* pipe, read, write, STDIN_FILENO */
#include <signal.h>   /* signal, SIGINT, SIGTERM */
#include <sys/select.h>

/* ── Globals ───────────────────────────────────────────────────────────── */

/* Self-pipe: callback writes to pipe_wr, main thread reads from pipe_rd. */
static int pipe_rd = -1;
static int pipe_wr = -1;

/* Set to 1 by the SIGINT/SIGTERM handler to request a clean exit. */
static volatile int g_stop = 0;

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Return env var if set and non-empty, otherwise fallback. */
static const char* env_or(const char* var, const char* fallback) {
    const char* v = getenv(var);
    return (v && v[0]) ? v : fallback;
}

/*
 * Print the tab-separated response text produced by usp_controller_get_many.
 *
 * Format per line:
 *   "PARAM\t<path>\t<value>"  – successfully resolved parameter
 *   "ERROR\t<path>\t<code>\t<msg>" – per-path error
 */
static void print_get_result(const char* result) {
    if (!result || result[0] == '\0') {
        printf("  (empty response)\n");
        return;
    }

    /* Work on a mutable copy to tokenise. */
    char buf[65536];
    strncpy(buf, result, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* line = buf;
    char* nl;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Tokenise by tab. */
        char* tok = strtok(line, "\t");
        if (!tok) { line = nl ? nl + 1 : line + strlen(line); continue; }

        if (strcmp(tok, "PARAM") == 0) {
            const char* path  = strtok(NULL, "\t");
            const char* value = strtok(NULL, "\t");
            printf("  %s = %s\n",
                   path  ? path  : "(null)",
                   value ? value : "(null)");
        } else if (strcmp(tok, "ERROR") == 0) {
            const char* path = strtok(NULL, "\t");
            const char* code = strtok(NULL, "\t");
            const char* msg  = strtok(NULL, "\t");
            fprintf(stderr, "  ERROR path=%s code=%s msg=%s\n",
                    path ? path : "?",
                    code ? code : "?",
                    msg  ? msg  : "?");
        }

        line = nl ? nl + 1 : line + strlen(line);
    }
}

/* ── Notification callback ─────────────────────────────────────────────── */

/*
 * Called by the library's worker thread when a notification arrives.
 *
 * user_data is the write end of the self-pipe cast to void*.
 * We write a single byte to wake the main thread; the main thread then
 * performs the follow-up GET.
 *
 * IMPORTANT: do NOT call any usp_controller_* function from here.
 */
static void on_notification(void* user_data) {
    int wr = (int)(intptr_t)user_data;
    char byte = 1;
    /* Ignore errors: if the pipe is full the main thread will still wake. */
    (void)write(wr, &byte, 1);
}

/* ── Signal handler ────────────────────────────────────────────────────── */

static void handle_signal(int sig) {
    (void)sig;
    g_stop = 1;
    /* Unblock the main select() loop. */
    if (pipe_wr >= 0) {
        char byte = 0;
        (void)write(pipe_wr, &byte, 1);
    }
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    const char* arg_socket = (argc > 1) ? argv[1] : "/var/run/usp/broker_agent_path";
    const char* arg_app    = (argc > 2) ? argv[2] : "proto::myapp";
    const char* arg_agent  = (argc > 3) ? argv[3] : "proto::api-gateway";

    const char* socket_path       = env_or("USP_SOCKET_PATH",       arg_socket);
    const char* app_endpoint_id   = env_or("USP_APP_ENDPOINT_ID",   arg_app);
    const char* agent_endpoint_id = env_or("USP_AGENT_ENDPOINT_ID", arg_agent);

    printf("libuspmtp C example\n");
    printf("  socket : %s\n", socket_path);
    printf("  app    : %s\n", app_endpoint_id);
    printf("  agent  : %s\n\n", agent_endpoint_id);

    /* ── Self-pipe for safe cross-thread signalling ──────────────────── */
    int fds[2];
    if (pipe(fds) != 0) {
        perror("pipe");
        return 1;
    }
    pipe_rd = fds[0];
    pipe_wr = fds[1];

    /* ── Signal handling ─────────────────────────────────────────────── */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    /* ── Create USP controller handle ────────────────────────────────── */
    UspControllerHandle* h = usp_controller_new(
        socket_path, app_endpoint_id, agent_endpoint_id, /*timeout_secs=*/10);

    if (!h) {
        fprintf(stderr, "Failed to create USP controller handle.\n");
        return 1;
    }

    /* ── 1. GET Device.DeviceInfo. ───────────────────────────────────── */
    printf("=== GET Device.DeviceInfo. ===\n");
    {
        const char* paths[] = { "Device.DeviceInfo." };
        char result[65536];
        int rc = usp_controller_get_many(h, paths, 1, result, sizeof(result));
        if (rc == USP_FFI_OK) {
            print_get_result(result);
        } else {
            char err[512];
            usp_controller_last_error(h, err, sizeof(err));
            fprintf(stderr, "GET failed (rc=%d): %s\n", rc, err);
        }
    }

    /* ── 2. Subscribe to Device.DeviceInfo. for ValueChange ─────────── */
    printf("\n=== Subscribing to Device.DeviceInfo. (ValueChange) ===\n");
    {
        char result[65536];
        /*
         * subscribe_and_get registers the subscription AND performs an
         * initial GET.  The callback fires on every subsequent notification.
         * user_data = write end of the self-pipe (cast to void*).
         */
        int rc = usp_controller_subscribe_and_get(
            h,
            "Device.DeviceInfo.",
            USP_FFI_SUBSCRIPTION_VALUE_CHANGE,
            on_notification,
            (void*)(intptr_t)pipe_wr,
            result, sizeof(result));

        if (rc == USP_FFI_OK) {
            printf("Subscription active.  Initial GET result:\n");
            print_get_result(result);
        } else {
            char err[512];
            usp_controller_last_error(h, err, sizeof(err));
            fprintf(stderr, "subscribe_and_get failed (rc=%d): %s\n", rc, err);
            fprintf(stderr, "Continuing without subscription.\n");
        }
    }

    /* ── 3. Wait for notifications and re-GET on each one ────────────── */
    printf("\nWaiting for Device.DeviceInfo. change notifications "
           "(Ctrl-C to exit)...\n\n");

    while (!g_stop) {
        fd_set rdset;
        FD_ZERO(&rdset);
        FD_SET(pipe_rd, &rdset);

        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        int n = select(pipe_rd + 1, &rdset, NULL, NULL, &tv);

        if (n < 0) {
            if (g_stop) break;       /* interrupted by signal handler */
            perror("select");
            break;
        }
        if (n == 0) continue;        /* timeout, keep waiting */

        /* Drain the pipe. */
        char drain[64];
        ssize_t nread = read(pipe_rd, drain, sizeof(drain));
        if (nread <= 0) break;

        if (g_stop) break;

        /* ── 3a. Re-GET Device.DeviceInfo. and print updated values ── */
        printf("=== Notification received — re-GET Device.DeviceInfo. ===\n");
        const char* paths[] = { "Device.DeviceInfo." };
        char result[65536];
        int rc = usp_controller_get_many(h, paths, 1, result, sizeof(result));
        if (rc == USP_FFI_OK) {
            print_get_result(result);
        } else {
            char err[512];
            usp_controller_last_error(h, err, sizeof(err));
            fprintf(stderr, "re-GET failed (rc=%d): %s\n", rc, err);
        }
        printf("\n");
    }

    printf("Exiting.\n");
    usp_controller_free(h);
    close(pipe_rd);
    close(pipe_wr);
    return 0;
}
