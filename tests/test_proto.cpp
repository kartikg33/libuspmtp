/*
 * Copyright 2026 Kartik Gohil
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Unit tests for USP wrapper encode/decode through the official protobuf C++
 * runtime and the vendored TR-369 USP 1.5 schemas.
 */

#include "usp_proto.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace usp::proto;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            std::fprintf(stderr, "FAIL: %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            ++g_fail; \
        } else { ++g_pass; } \
    } while (0)

static void test_record_roundtrip() {
    std::puts("test_record_roundtrip");

    NoSessionContextRecord nsc;
    nsc.payload = {0x01, 0x02, 0x03};

    Record rec;
    rec.version = "1.5";
    rec.to_id = "proto::agent";
    rec.from_id = "proto::controller";
    rec.payload_security = 0;
    rec.record_type = nsc;

    auto encoded = rec.encode();
    CHECK(!encoded.empty());

    auto decoded = Record::decode(encoded);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->version, rec.version);
    CHECK_EQ(decoded->to_id, rec.to_id);
    CHECK_EQ(decoded->from_id, rec.from_id);
    CHECK_EQ(decoded->payload_security, 0);

    auto* decoded_nsc = std::get_if<NoSessionContextRecord>(&decoded->record_type);
    CHECK(decoded_nsc != nullptr);
    CHECK_EQ(decoded_nsc->payload, nsc.payload);
}

static void test_msg_get_roundtrip() {
    std::puts("test_msg_get_roundtrip");

    Get get;
    get.param_paths = {"Device.DeviceInfo.SerialNumber", "Device.WiFi."};

    Request request;
    request.req_type = get;

    Body body;
    body.msg_body = request;

    Msg msg;
    msg.header = Header{"42", MsgType::Get};
    msg.body = body;

    auto decoded = Msg::decode(msg.encode());
    CHECK(decoded.has_value());
    CHECK(decoded->header.has_value());
    CHECK_EQ(decoded->header->msg_id, "42");
    CHECK_EQ(decoded->header->msg_type, MsgType::Get);
    CHECK(decoded->body.has_value());

    auto* decoded_req = std::get_if<Request>(&decoded->body->msg_body);
    CHECK(decoded_req != nullptr);
    auto* decoded_get = decoded_req ? std::get_if<Get>(&decoded_req->req_type) : nullptr;
    CHECK(decoded_get != nullptr);
    if (decoded_get) {
        CHECK_EQ(decoded_get->param_paths, get.param_paths);
    }
}

static void test_msg_response_roundtrip() {
    std::puts("test_msg_response_roundtrip");

    GetResp get_resp;
    RequestedPathResult requested;
    requested.requested_path = "Device.DeviceInfo.";
    ResolvedPathResult resolved;
    resolved.resolved_path = "Device.DeviceInfo.";
    resolved.result_params = {{"SerialNumber", "ABC123"}, {"ModelName", "XYZ"}};
    requested.resolved_path_results = {resolved};
    get_resp.req_path_results = {requested};

    Response response;
    response.resp_type = get_resp;

    Body body;
    body.msg_body = response;

    Msg msg;
    msg.header = Header{"43", MsgType::GetResp};
    msg.body = body;

    auto decoded = Msg::decode(msg.encode());
    CHECK(decoded.has_value());

    auto* decoded_resp = std::get_if<Response>(&decoded->body->msg_body);
    CHECK(decoded_resp != nullptr);
    auto* decoded_get_resp = decoded_resp ? std::get_if<GetResp>(&decoded_resp->resp_type) : nullptr;
    CHECK(decoded_get_resp != nullptr);
    if (decoded_get_resp) {
        CHECK(decoded_get_resp->req_path_results.size() == 1);
        if (decoded_get_resp->req_path_results.size() == 1) {
            const auto& decoded_results = decoded_get_resp->req_path_results[0].resolved_path_results;
            CHECK(decoded_results.size() == 1);
            if (decoded_results.size() == 1) {
                CHECK_EQ(decoded_results[0].result_params, resolved.result_params);
            }
        }
    }
}

static void test_notify_usp_1_5_tags() {
    std::puts("test_notify_usp_1_5_tags");

    Msg msg;
    msg.header = Header{"44", MsgType::Notify};

    auto native_notify = Notify{};
    native_notify.subscription_id = "sub";
    native_notify.send_resp = true;
    native_notify.notification = ValueChange{"Path.", "one"};

    Request request;
    request.req_type = native_notify;
    Body body;
    body.msg_body = request;
    msg.body = body;

    auto encoded = msg.encode();
    CHECK(!encoded.empty());

    auto decoded = Msg::decode(encoded);
    CHECK(decoded.has_value());
    auto* decoded_req = std::get_if<Request>(&decoded->body->msg_body);
    CHECK(decoded_req != nullptr);
    auto* decoded_notify = decoded_req ? std::get_if<Notify>(&decoded_req->req_type) : nullptr;
    CHECK(decoded_notify != nullptr);
    if (decoded_notify) {
        CHECK_EQ(decoded_notify->subscription_id, "sub");
        CHECK_EQ(decoded_notify->send_resp, true);
        CHECK(std::get_if<ValueChange>(&decoded_notify->notification) != nullptr);
    }
}

int main() {
    test_record_roundtrip();
    test_msg_get_roundtrip();
    test_msg_response_roundtrip();
    test_notify_usp_1_5_tags();

    std::printf("test_proto: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
