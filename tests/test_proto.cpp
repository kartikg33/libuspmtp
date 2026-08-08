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
 * tests/test_proto.cpp
 *
 * Unit tests for protobuf encode/decode roundtrips.
 * Run without any network dependency.
 */

#include "usp_proto.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace usp::proto;

/* ── Minimal test harness ────────────────────────────────────────────────── */

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

/* ── PbWriter / PbReader primitives ─────────────────────────────────────── */

static void test_varint_roundtrip() {
    std::puts("test_varint_roundtrip");

    struct Case { uint64_t v; size_t expected_bytes; };
    Case cases[] = {
        {0,         1},
        {1,         1},
        {127,       1},
        {128,       2},
        {16383,     2},
        {16384,     3},
        {UINT64_MAX, 10},
    };

    for (auto& c : cases) {
        PbWriter w;
        w.write_varint(c.v);
        CHECK(w.bytes().size() == c.expected_bytes);

        PbReader r({w.bytes().data(), w.bytes().size()});
        uint64_t v = 99;
        CHECK(r.read_varint(v));
        CHECK_EQ(v, c.v);
        CHECK(!r.has_more());
    }
}

static void test_fixed32_roundtrip() {
    std::puts("test_fixed32_roundtrip");

    uint32_t values[] = {0, 1, 0xDEADBEEFu, UINT32_MAX};
    for (auto v : values) {
        PbWriter w;
        w.write_fixed32_raw(v);
        CHECK(w.bytes().size() == 4);

        PbReader r({w.bytes().data(), w.bytes().size()});
        uint32_t out;
        CHECK(r.read_fixed32(out));
        CHECK_EQ(out, v);
    }
}

static void test_string_field_roundtrip() {
    std::puts("test_string_field_roundtrip");

    std::string values[] = {"", "hello", "Device.DeviceInfo.SerialNumber", std::string(256, 'x')};
    for (auto& s : values) {
        PbWriter w;
        w.write_string_field(3, s);

        PbReader r({w.bytes().data(), w.bytes().size()});
        if (s.empty()) {
            /* Empty strings are not encoded. */
            CHECK(!r.has_more());
            continue;
        }
        uint32_t fn, wt;
        CHECK(r.read_tag(fn, wt));
        CHECK_EQ(fn, 3u);
        CHECK_EQ(wt, 2u);
        std::string out;
        CHECK(r.read_string(out));
        CHECK_EQ(out, s);
        CHECK(!r.has_more());
    }
}

/* ── Record encode/decode ────────────────────────────────────────────────── */

static void test_record_roundtrip() {
    std::puts("test_record_roundtrip");

    NoSessionContextRecord nsc;
    nsc.payload = {0x01, 0x02, 0x03};

    Record rec;
    rec.version = "1.3";
    rec.to_id   = "proto::agent";
    rec.from_id = "proto::controller";
    rec.payload_security = 0;
    rec.record_type = nsc;

    auto encoded = rec.encode();
    CHECK(!encoded.empty());

    auto decoded = Record::decode(encoded);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->version, rec.version);
    CHECK_EQ(decoded->to_id,   rec.to_id);
    CHECK_EQ(decoded->from_id, rec.from_id);
    CHECK_EQ(decoded->payload_security, 0);

    auto* nsc2 = std::get_if<NoSessionContextRecord>(&decoded->record_type);
    CHECK(nsc2 != nullptr);
    CHECK_EQ(nsc2->payload, nsc.payload);
}

/* ── Header ──────────────────────────────────────────────────────────────── */

static void test_header_roundtrip() {
    std::puts("test_header_roundtrip");

    Header h;
    h.msg_id   = "42";
    h.msg_type = MsgType::Get;

    auto encoded = h.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Header::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->msg_id,   h.msg_id);
    CHECK_EQ(decoded->msg_type, h.msg_type);
}

/* ── Get / GetResp ───────────────────────────────────────────────────────── */

static void test_get_roundtrip() {
    std::puts("test_get_roundtrip");

    Get g;
    g.param_paths = {"Device.DeviceInfo.SerialNumber", "Device.WiFi."};
    g.max_depth   = 0;

    auto encoded = g.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Get::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->param_paths, g.param_paths);
    CHECK_EQ(decoded->max_depth,   g.max_depth);
}

static void test_get_resp_roundtrip() {
    std::puts("test_get_resp_roundtrip");

    ResolvedPathResult rpr;
    rpr.resolved_path = "Device.DeviceInfo.";
    rpr.result_params = {{"SerialNumber", "ABC123"}, {"ModelName", "XYZ"}};

    RequestedPathResult rr;
    rr.requested_path = "Device.DeviceInfo.";
    rr.err_code = 0;
    rr.resolved_path_results = {rpr};

    GetResp gr;
    gr.req_path_results = {rr};

    auto encoded = gr.encode();
    PbReader reader({encoded.data(), encoded.size()});
    auto decoded = GetResp::decode(reader);
    CHECK(decoded.has_value());
    CHECK(decoded->req_path_results.size() == 1);
    auto& rr2 = decoded->req_path_results[0];
    CHECK_EQ(rr2.requested_path, rr.requested_path);
    CHECK(rr2.resolved_path_results.size() == 1);
    CHECK_EQ(rr2.resolved_path_results[0].result_params, rpr.result_params);
}

/* ── Set / SetResp ───────────────────────────────────────────────────────── */

static void test_set_roundtrip() {
    std::puts("test_set_roundtrip");

    UpdateParamSetting s;
    s.param    = "SSID";
    s.value    = "MyNetwork";
    s.required = true;

    UpdateObject o;
    o.obj_path       = "Device.WiFi.SSID.1.";
    o.param_settings = {s};

    Set set_msg;
    set_msg.allow_partial = false;
    set_msg.update_objs   = {o};

    auto encoded = set_msg.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Set::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->allow_partial, false);
    CHECK(decoded->update_objs.size() == 1);
    auto& o2 = decoded->update_objs[0];
    CHECK_EQ(o2.obj_path, o.obj_path);
    CHECK(o2.param_settings.size() == 1);
    CHECK_EQ(o2.param_settings[0].param,    s.param);
    CHECK_EQ(o2.param_settings[0].value,    s.value);
    CHECK_EQ(o2.param_settings[0].required, s.required);
}

/* ── Notify ──────────────────────────────────────────────────────────────── */

static void test_notify_roundtrip() {
    std::puts("test_notify_roundtrip");

    ValueChange vc;
    vc.param_path  = "Device.DeviceInfo.SerialNumber";
    vc.param_value = "CHANGED";

    Notify n;
    n.subscription_id = "sub-1";
    n.send_resp       = true;
    n.notification    = vc;

    auto encoded = n.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Notify::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->subscription_id, n.subscription_id);
    CHECK_EQ(decoded->send_resp,       n.send_resp);

    auto* vc2 = std::get_if<ValueChange>(&decoded->notification);
    CHECK(vc2 != nullptr);
    CHECK_EQ(vc2->param_path,  vc.param_path);
    CHECK_EQ(vc2->param_value, vc.param_value);
}

/* ── Full Msg encode/decode ──────────────────────────────────────────────── */

static void test_msg_get_roundtrip() {
    std::puts("test_msg_get_roundtrip");

    Get g;
    g.param_paths = {"Device.DeviceInfo.SerialNumber"};

    Request req;
    req.req_type = std::move(g);

    Body body;
    body.msg_body = std::move(req);

    Header h;
    h.msg_id   = "99";
    h.msg_type = MsgType::Get;

    Msg msg;
    msg.header = h;
    msg.body   = body;

    auto encoded = msg.encode();
    auto decoded = Msg::decode(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->header.has_value());
    CHECK_EQ(decoded->header->msg_id, "99");
    CHECK_EQ(decoded->header->msg_type, MsgType::Get);

    auto* req2 = decoded->body
        ? std::get_if<Request>(&decoded->body->msg_body)
        : nullptr;
    CHECK(req2 != nullptr);
    auto* g2 = std::get_if<Get>(&req2->req_type);
    CHECK(g2 != nullptr);
    CHECK(g2->param_paths.size() == 1);
    CHECK_EQ(g2->param_paths[0], "Device.DeviceInfo.SerialNumber");
}

static void test_msg_get_resp_roundtrip() {
    std::puts("test_msg_get_resp_roundtrip");

    ResolvedPathResult rpr;
    rpr.resolved_path = "Device.DeviceInfo.";
    rpr.result_params["SerialNumber"] = "ABC";

    RequestedPathResult rr;
    rr.requested_path = "Device.DeviceInfo.SerialNumber";
    rr.resolved_path_results = {rpr};

    GetResp gr;
    gr.req_path_results = {rr};

    Response resp;
    resp.resp_type = std::move(gr);

    Body body;
    body.msg_body = std::move(resp);

    Header h;
    h.msg_id   = "100";
    h.msg_type = MsgType::GetResp;

    Msg msg;
    msg.header = h;
    msg.body   = body;

    auto encoded = msg.encode();
    auto decoded = Msg::decode(encoded);
    CHECK(decoded.has_value());
    CHECK(decoded->body.has_value());

    auto* resp2 = std::get_if<Response>(&decoded->body->msg_body);
    CHECK(resp2 != nullptr);
    auto* gr2 = std::get_if<GetResp>(&resp2->resp_type);
    CHECK(gr2 != nullptr);
    CHECK(gr2->req_path_results.size() == 1);
    CHECK(gr2->req_path_results[0].resolved_path_results.size() == 1);
    CHECK_EQ(gr2->req_path_results[0].resolved_path_results[0].result_params["SerialNumber"], "ABC");
}

/* ── Add / AddResp ───────────────────────────────────────────────────────── */

static void test_add_roundtrip() {
    std::puts("test_add_roundtrip");

    CreateParamSetting s;
    s.param    = "Enable";
    s.value    = "true";
    s.required = true;

    CreateObject co;
    co.obj_path       = "Device.LocalAgent.Subscription.";
    co.param_settings = {s};

    Add add;
    add.allow_partial = true;
    add.create_objs   = {co};

    auto encoded = add.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Add::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->allow_partial, true);
    CHECK(decoded->create_objs.size() == 1);
    CHECK_EQ(decoded->create_objs[0].obj_path, co.obj_path);
}

/* ── Operate ─────────────────────────────────────────────────────────────── */

static void test_operate_roundtrip() {
    std::puts("test_operate_roundtrip");

    Operate op;
    op.command     = "Device.IP.Interface.1.Reset()";
    op.command_key = "";
    op.send_resp   = true;
    op.input_args["arg1"] = "val1";

    auto encoded = op.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = Operate::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->command,    op.command);
    CHECK_EQ(decoded->send_resp,  op.send_resp);
    CHECK_EQ(decoded->input_args, op.input_args);
}

/* ── Error body ─────────────────────────────────────────────────────────── */

static void test_error_body_roundtrip() {
    std::puts("test_error_body_roundtrip");

    ErrorBody eb;
    eb.err_code = 7004;
    eb.err_msg  = "invalid arguments";

    auto encoded = eb.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = ErrorBody::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->err_code, eb.err_code);
    CHECK_EQ(decoded->err_msg,  eb.err_msg);
}

/* ── Empty / unknown fields ─────────────────────────────────────────────── */

static void test_decode_empty_bytes() {
    std::puts("test_decode_empty_bytes");

    std::span<const uint8_t> empty{};
    auto rec = Record::decode(empty);
    CHECK(rec.has_value()); /* Empty input = default-constructed struct */

    auto msg = Msg::decode(empty);
    CHECK(msg.has_value());
    CHECK(!msg->header.has_value());
    CHECK(!msg->body.has_value());
}

static void test_skip_unknown_fields() {
    std::puts("test_skip_unknown_fields");

    /* Encode a Get message with an unknown field (field 99, varint 42). */
    PbWriter w;
    w.write_string_field(1, "Device.DeviceInfo.");
    /* Unknown field 99, wire type 0 (varint). */
    w.write_varint_field(99, 42);

    auto bytes = w.take();
    PbReader r({bytes.data(), bytes.size()});
    auto g = Get::decode(r);
    CHECK(g.has_value());
    CHECK(g->param_paths.size() == 1);
    CHECK_EQ(g->param_paths[0], "Device.DeviceInfo.");
}

/* ── Map entries ──────────────────────────────────────────────────────────── */

static void test_map_entry_roundtrip() {
    std::puts("test_map_entry_roundtrip");

    ResolvedPathResult rpr;
    rpr.resolved_path = "Device.WiFi.Radio.1.";
    rpr.result_params["Channel"]   = "6";
    rpr.result_params["Bandwidth"] = "80MHz";

    auto encoded = rpr.encode();
    PbReader r({encoded.data(), encoded.size()});
    auto decoded = ResolvedPathResult::decode(r);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->result_params, rpr.result_params);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    test_varint_roundtrip();
    test_fixed32_roundtrip();
    test_string_field_roundtrip();
    test_record_roundtrip();
    test_header_roundtrip();
    test_get_roundtrip();
    test_get_resp_roundtrip();
    test_set_roundtrip();
    test_notify_roundtrip();
    test_msg_get_roundtrip();
    test_msg_get_resp_roundtrip();
    test_add_roundtrip();
    test_operate_roundtrip();
    test_error_body_roundtrip();
    test_decode_empty_bytes();
    test_skip_unknown_fields();
    test_map_entry_roundtrip();

    std::printf("\n%s: %d passed, %d failed\n",
                (g_fail == 0) ? "PASS" : "FAIL",
                g_pass, g_fail);
    return g_fail != 0 ? 1 : 0;
}
