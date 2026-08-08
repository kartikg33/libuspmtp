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
 * Minimal C example demonstrating the libuspcontroller C API.
 *
 * Usage:
 *   ./example_c [socket_path] [app_endpoint_id] [agent_endpoint_id]
 *
 * Defaults:
 *   socket_path       = /var/run/usp/broker_agent_path
 *   app_endpoint_id   = proto::myapp
 *   agent_endpoint_id = proto::api-gateway
 */

#include "libuspcontroller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* socket_path       = (argc > 1) ? argv[1] : "/var/run/usp/broker_agent_path";
    const char* app_endpoint_id   = (argc > 2) ? argv[2] : "proto::myapp";
    const char* agent_endpoint_id = (argc > 3) ? argv[3] : "proto::api-gateway";

    printf("USP Controller C example\n");
    printf("  socket : %s\n", socket_path);
    printf("  app    : %s\n", app_endpoint_id);
    printf("  agent  : %s\n\n", agent_endpoint_id);

    /* Create handle (timeout = 10 seconds). */
    UspControllerHandle* h = usp_controller_new(
        socket_path, app_endpoint_id, agent_endpoint_id, 10);

    if (!h) {
        fprintf(stderr, "Failed to create USP controller handle.\n");
        return 1;
    }

    /* ── GET single parameter ───────────────────────────────────────────── */
    char value[512];
    int rc = usp_controller_get(h,
                                "Device.DeviceInfo.SerialNumber",
                                value, sizeof(value));
    if (rc == USP_FFI_OK) {
        printf("Device.DeviceInfo.SerialNumber = %s\n", value);
    } else {
        char err[512];
        usp_controller_last_error(h, err, sizeof(err));
        fprintf(stderr, "GET failed (rc=%d): %s\n", rc, err);
    }

    /* ── GET multiple parameters ────────────────────────────────────────── */
    const char* paths[] = {
        "Device.DeviceInfo.SoftwareVersion",
        "Device.DeviceInfo.HardwareVersion",
    };
    char result[4096];
    rc = usp_controller_get_many(h, paths, 2, result, sizeof(result));
    if (rc == USP_FFI_OK) {
        printf("\nget_many result:\n%s\n", result);
    } else {
        char err[512];
        usp_controller_last_error(h, err, sizeof(err));
        fprintf(stderr, "GET_MANY failed (rc=%d): %s\n", rc, err);
    }

    /* ── Vendor-defined error code check ───────────────────────────────── */
    printf("usp_error_is_vendor_defined(7800) = %d\n",
           usp_error_is_vendor_defined(7800));
    printf("usp_error_is_vendor_defined(7004) = %d\n",
           usp_error_is_vendor_defined(7004));

    usp_controller_free(h);
    return 0;
}
