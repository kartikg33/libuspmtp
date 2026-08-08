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
 * Minimal C++ example demonstrating the UspController C++ API directly.
 *
 * Configuration (environment variables take precedence over argv):
 *   USP_SOCKET_PATH       – path to the OB-USPA UNIX domain socket
 *   USP_APP_ENDPOINT_ID   – this application's USP endpoint ID (from_id)
 *   USP_AGENT_ENDPOINT_ID – the USP agent's endpoint ID (to_id)
 *
 * Fallback order: env var → argv[N] → built-in default.
 */

#include "client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

/* Return env var if set and non-empty, otherwise fallback. */
static std::string env_or(const char* var, std::string fallback) {
    const char* v = std::getenv(var);
    return (v && v[0]) ? std::string(v) : std::move(fallback);
}

int main(int argc, char** argv) {
    std::string socket_path       = env_or("USP_SOCKET_PATH",
        (argc > 1) ? argv[1] : "/var/run/usp/broker_agent_path");
    std::string app_endpoint_id   = env_or("USP_APP_ENDPOINT_ID",
        (argc > 2) ? argv[2] : "proto::myapp");
    std::string agent_endpoint_id = env_or("USP_AGENT_ENDPOINT_ID",
        (argc > 3) ? argv[3] : "proto::api-gateway");

    std::cout << "USP Controller C++ example\n"
              << "  socket : " << socket_path       << "\n"
              << "  app    : " << app_endpoint_id   << "\n"
              << "  agent  : " << agent_endpoint_id << "\n\n";

    usp::UspController client(socket_path, app_endpoint_id, agent_endpoint_id);
    client.set_timeout(std::chrono::seconds(10));

    /* ── GET single parameter ───────────────────────────────────────────── */
    {
        auto result = client.get("Device.DeviceInfo.SerialNumber");
        if (auto* resp = std::get_if<usp::GetResponse>(&result)) {
            for (auto& [k, v] : resp->params)
                std::cout << k << " = " << v << "\n";
        } else {
            auto& err = std::get<usp::UspError>(result);
            std::cerr << "GET error: " << err.to_string() << "\n";
        }
    }

    /* ── GET multiple parameters ────────────────────────────────────────── */
    {
        auto result = client.get_many({
            "Device.DeviceInfo.SoftwareVersion",
            "Device.DeviceInfo.HardwareVersion",
        });
        if (auto* resp = std::get_if<usp::GetResponse>(&result)) {
            std::cout << "\nget_many results:\n";
            for (auto& [k, v] : resp->params)
                std::cout << "  " << k << " = " << v << "\n";
            for (auto& e : resp->errors)
                std::cerr << "  ERROR " << e.err_code << ": " << e.err_msg
                          << " (path=" << e.path << ")\n";
        } else {
            auto& err = std::get<usp::UspError>(result);
            std::cerr << "GET_MANY error: " << err.to_string() << "\n";
        }
    }

    /* ── SET a parameter ────────────────────────────────────────────────── */
    {
        auto result = client.set("Device.WiFi.SSID.1.SSID", "MyNetwork");
        if (auto* resp = std::get_if<usp::SetResponse>(&result)) {
            for (auto& p : resp->updated)
                std::cout << "UPDATED: " << p << "\n";
        } else {
            auto& err = std::get<usp::UspError>(result);
            std::cerr << "SET error: " << err.to_string() << "\n";
        }
    }

    return 0;
}
