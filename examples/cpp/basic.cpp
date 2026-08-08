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
 * examples/cpp/basic.cpp
 *
 * C++ example demonstrating the UspController C++ API:
 *
 *   1. GET Device.DeviceInfo. and print all returned parameters.
 *   2. Subscribe to Device.DeviceInfo. for ValueChange notifications.
 *   3. On each notification, re-GET Device.DeviceInfo. and print the
 *      updated parameter values.
 *
 * The subscription callback fires from the library's internal worker thread.
 * It must NOT call any UspController methods (deadlock risk).  Instead the
 * callback increments an atomic counter and signals a condition variable;
 * the main thread waits on the condition variable and issues the re-GET.
 *
 * Configuration (environment variables take precedence over argv):
 *   USP_SOCKET_PATH       – path to the OB-USPA UNIX domain socket
 *   USP_APP_ENDPOINT_ID   – this application's USP endpoint ID (from_id)
 *   USP_AGENT_ENDPOINT_ID – the USP agent's endpoint ID (to_id)
 *
 * Fallback order: env var → argv[N] → built-in default.
 */

#include "client.hpp"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <variant>

/* ── Signal handling ──────────────────────────────────────────────────── */

static std::atomic<bool>        g_stop{false};
static std::mutex               g_cv_mutex;
static std::condition_variable  g_cv;
static std::atomic<int>         g_notify_count{0};

static void handle_signal(int /*sig*/) {
    g_stop.store(true, std::memory_order_relaxed);
    g_cv.notify_all();
}

/* ── Helpers ──────────────────────────────────────────────────────────── */

static std::string env_or(const char* var, std::string fallback) {
    const char* v = std::getenv(var);
    return (v && v[0]) ? std::string(v) : std::move(fallback);
}

/* Print a GetResponse to stdout. */
static void print_get_response(const usp::GetResponse& resp) {
    if (resp.params.empty() && resp.errors.empty()) {
        std::cout << "  (empty response)\n";
        return;
    }
    for (auto& [k, v] : resp.params)
        std::cout << "  " << k << " = " << v << "\n";
    for (auto& e : resp.errors)
        std::cerr << "  ERROR " << e.err_code << " (" << e.path << "): "
                  << e.err_msg << "\n";
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    std::string socket_path       = env_or("USP_SOCKET_PATH",
        (argc > 1) ? argv[1] : "/var/run/usp/broker_agent_path");
    std::string app_endpoint_id   = env_or("USP_APP_ENDPOINT_ID",
        (argc > 2) ? argv[2] : "proto::myapp");
    std::string agent_endpoint_id = env_or("USP_AGENT_ENDPOINT_ID",
        (argc > 3) ? argv[3] : "proto::api-gateway");

    std::cout << "libuspmtp C++ example\n"
              << "  socket : " << socket_path       << "\n"
              << "  app    : " << app_endpoint_id   << "\n"
              << "  agent  : " << agent_endpoint_id << "\n\n";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    usp::UspController client(socket_path, app_endpoint_id, agent_endpoint_id);
    client.set_timeout(std::chrono::seconds(10));

    /* ── 1. GET Device.DeviceInfo. ──────────────────────────────────── */
    std::cout << "=== GET Device.DeviceInfo. ===\n";
    {
        auto result = client.get_many({"Device.DeviceInfo."});
        if (auto* resp = std::get_if<usp::GetResponse>(&result)) {
            print_get_response(*resp);
        } else {
            std::cerr << "GET error: "
                      << std::get<usp::UspError>(result).to_string() << "\n";
        }
    }

    /* ── 2. Subscribe to Device.DeviceInfo. for ValueChange ─────────── */
    std::cout << "\n=== Subscribing to Device.DeviceInfo. (ValueChange) ===\n";
    {
        /*
         * Callback: fired from the worker thread on each incoming
         * notification.  Must not call UspController methods.
         * Increments counter and signals the main thread.
         */
        auto callback = []() {
            g_notify_count.fetch_add(1, std::memory_order_relaxed);
            g_cv.notify_one();
        };

        auto result = client.subscribe_and_get(
            "Device.DeviceInfo.",
            usp::SubscriptionNotificationType::ValueChange,
            callback);

        if (auto* resp = std::get_if<usp::GetResponse>(&result)) {
            std::cout << "Subscription active.  Initial GET result:\n";
            print_get_response(*resp);
        } else {
            std::cerr << "subscribe_and_get error: "
                      << std::get<usp::UspError>(result).to_string() << "\n";
            std::cerr << "Continuing without subscription.\n";
        }
    }

    /* ── 3. Wait for notifications and re-GET on each one ────────────── */
    std::cout << "\nWaiting for Device.DeviceInfo. change notifications "
                 "(Ctrl-C to exit)...\n\n";

    int last_seen = 0;
    while (!g_stop.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock(g_cv_mutex);
        g_cv.wait_for(lock, std::chrono::seconds(5), [&] {
            return g_stop.load(std::memory_order_relaxed)
                || g_notify_count.load(std::memory_order_relaxed) != last_seen;
        });

        if (g_stop.load(std::memory_order_relaxed)) break;

        int current = g_notify_count.load(std::memory_order_relaxed);
        if (current == last_seen) continue;   /* spurious wakeup or timeout */
        last_seen = current;
        lock.unlock();

        /* ── 3a. Re-GET Device.DeviceInfo. and print updated values ── */
        std::cout << "=== Notification received — re-GET Device.DeviceInfo. ===\n";
        auto result = client.get_many({"Device.DeviceInfo."});
        if (auto* resp = std::get_if<usp::GetResponse>(&result)) {
            print_get_response(*resp);
        } else {
            std::cerr << "re-GET error: "
                      << std::get<usp::UspError>(result).to_string() << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Exiting.\n";
    return 0;
}
