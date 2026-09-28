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

static void test_new_request_roundtrips() {
    std::puts("test_new_request_roundtrips");

    /* Each new request type must survive Msg encode → decode as its own
     * variant member (decode maps them to empty structs, mirroring the
     * existing Set/Add handling). */
    auto check_request = [](Request req, MsgType type) {
        Body body;
        body.msg_body = std::move(req);
        Msg msg;
        msg.header = Header{"50", type};
        msg.body = std::move(body);
        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        if (!decoded.has_value()) return;
        auto* r = std::get_if<Request>(&decoded->body->msg_body);
        CHECK(r != nullptr);
        if (r) CHECK(r->req_type.index() != 0);
    };

    {
        Delete d;
        d.obj_paths = {"Device.X.1."};
        Request req;
        req.req_type = d;
        check_request(std::move(req), MsgType::Delete);
    }
    {
        Register r;
        r.reg_paths = {"Device.X."};
        Request req;
        req.req_type = r;
        check_request(std::move(req), MsgType::Register);
    }
    {
        GetSupportedDM g;
        g.obj_paths = {"Device."};
        Request req;
        req.req_type = g;
        check_request(std::move(req), MsgType::GetSupportedDm);
    }
    {
        GetInstances g;
        g.obj_paths = {"Device.X."};
        Request req;
        req.req_type = g;
        check_request(std::move(req), MsgType::GetInstances);
    }
    {
        GetSupportedProtocol g;
        g.controller_versions = "1.5";
        Request req;
        req.req_type = std::move(g);
        check_request(std::move(req), MsgType::GetSupportedProto);
    }
}

static void test_new_response_roundtrips() {
    std::puts("test_new_response_roundtrips");

    /* DeleteResp with an unaffected-path error. */
    {
        DeleteOperationStatus st;
        DeleteOperationSuccess ok;
        ok.affected_paths = {"Device.X.1."};
        ok.unaffected_errors = {{"Device.X.2.", 7026, "nope"}};
        st.oper_status = std::move(ok);

        DeletedObjectResult dor;
        dor.requested_path = "Device.X.1.";
        dor.oper_status = st;

        DeleteResp dr;
        dr.deleted_obj_results = {dor};

        Response resp;
        resp.resp_type = std::move(dr);

        Body body;
        body.msg_body = std::move(resp);

        Msg msg;
        msg.header = Header{"52", MsgType::DeleteResp};
        msg.body = std::move(body);

        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        auto* r = std::get_if<Response>(&decoded->body->msg_body);
        auto* d = r ? std::get_if<DeleteResp>(&r->resp_type) : nullptr;
        CHECK(d != nullptr);
        if (d && d->deleted_obj_results.size() == 1) {
            const auto& res = d->deleted_obj_results[0];
            CHECK(res.oper_status.has_value());
            if (res.oper_status) {
                auto* ok2 = std::get_if<DeleteOperationSuccess>(
                    &res.oper_status->oper_status);
                CHECK(ok2 != nullptr);
                if (ok2) {
                    CHECK_EQ(ok2->affected_paths,
                             std::vector<std::string>{"Device.X.1."});
                    CHECK_EQ(ok2->unaffected_errors.size(), 1u);
                    if (!ok2->unaffected_errors.empty())
                        CHECK_EQ(ok2->unaffected_errors[0].err_code, 7026u);
                }
            }
        }
    }

    /* RegisterResp success. */
    {
        RegisterOperationStatus st;
        st.oper_status = RegisterOperationSuccess{"Device.Y."};

        RegisteredPathResult rpr;
        rpr.requested_path = "Device.Y.";
        rpr.oper_status = st;

        RegisterResp rr;
        rr.registered_path_results = {rpr};

        Response resp;
        resp.resp_type = std::move(rr);

        Body body;
        body.msg_body = std::move(resp);

        Msg msg;
        msg.header = Header{"53", MsgType::RegisterResp};
        msg.body = std::move(body);

        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        auto* r = std::get_if<Response>(&decoded->body->msg_body);
        auto* rr2 = r ? std::get_if<RegisterResp>(&r->resp_type) : nullptr;
        CHECK(rr2 != nullptr);
        if (rr2 && rr2->registered_path_results.size() == 1) {
            const auto& res = rr2->registered_path_results[0];
            CHECK(res.oper_status.has_value());
            if (res.oper_status) {
                auto* ok = std::get_if<RegisterOperationSuccess>(
                    &res.oper_status->oper_status);
                CHECK(ok != nullptr);
                if (ok) CHECK_EQ(ok->registered_path, "Device.Y.");
            }
        }
    }

    /* GetSupportedDMResp with params/commands/events/key sets. */
    {
        SupportedParamInfo param{"SerialNumber", 0, 8, 1};
        SupportedCommandInfo cmd{"Reset", {"Arg"}, {"Out"}, 1};
        SupportedEventInfo event{"Boot", {"Cause"}};

        SupportedObjectInfo obj;
        obj.path = "Device.DeviceInfo.";
        obj.multi_instance = false;
        obj.params = {param};
        obj.commands = {cmd};
        obj.events = {event};
        obj.unique_key_sets = {{"SerialNumber"}};

        SupportedDMResult result;
        result.requested_path = "Device.DeviceInfo.";
        result.objects = {obj};

        GetSupportedDMResp gr;
        gr.results = {result};

        Response resp;
        resp.resp_type = std::move(gr);

        Body body;
        body.msg_body = std::move(resp);

        Msg msg;
        msg.header = Header{"54", MsgType::GetSupportedDmResp};
        msg.body = std::move(body);

        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        auto* r = std::get_if<Response>(&decoded->body->msg_body);
        auto* g = r ? std::get_if<GetSupportedDMResp>(&r->resp_type) : nullptr;
        CHECK(g != nullptr);
        if (g && g->results.size() == 1 && g->results[0].objects.size() == 1) {
            const auto& o = g->results[0].objects[0];
            CHECK_EQ(o.params.size(), 1u);
            CHECK_EQ(o.commands.size(), 1u);
            CHECK_EQ(o.events.size(), 1u);
            CHECK_EQ(o.unique_key_sets,
                     std::vector<std::vector<std::string>>{{"SerialNumber"}});
            if (!o.params.empty()) {
                CHECK_EQ(o.params[0].name, "SerialNumber");
                CHECK_EQ(o.params[0].value_type, 8);
            }
        }
    }

    /* GetInstancesResp with unique keys. */
    {
        InstanceInfo inst;
        inst.path = "Device.X.1.";
        inst.unique_keys = {{"ID", "7"}};

        InstancesResult result;
        result.requested_path = "Device.X.";
        result.instances = {inst};

        GetInstancesResp gr;
        gr.results = {result};

        Response resp;
        resp.resp_type = std::move(gr);

        Body body;
        body.msg_body = std::move(resp);

        Msg msg;
        msg.header = Header{"55", MsgType::GetInstancesResp};
        msg.body = std::move(body);

        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        auto* r = std::get_if<Response>(&decoded->body->msg_body);
        auto* g = r ? std::get_if<GetInstancesResp>(&r->resp_type) : nullptr;
        CHECK(g != nullptr);
        if (g && g->results.size() == 1 && g->results[0].instances.size() == 1) {
            const auto& in = g->results[0].instances[0];
            CHECK_EQ(in.path, "Device.X.1.");
            auto it = in.unique_keys.find("ID");
            CHECK(it != in.unique_keys.end());
            if (it != in.unique_keys.end()) CHECK_EQ(it->second, "7");
        }
    }

    /* GetSupportedProtocolResp versions string. */
    {
        GetSupportedProtocolResp gr;
        gr.agent_versions = "1.5";

        Response resp;
        resp.resp_type = std::move(gr);

        Body body;
        body.msg_body = std::move(resp);

        Msg msg;
        msg.header = Header{"56", MsgType::GetSupportedProtoResp};
        msg.body = std::move(body);

        auto decoded = Msg::decode(msg.encode());
        CHECK(decoded.has_value());
        auto* r = std::get_if<Response>(&decoded->body->msg_body);
        auto* g = r ? std::get_if<GetSupportedProtocolResp>(&r->resp_type) : nullptr;
        CHECK(g != nullptr);
        if (g) CHECK_EQ(g->agent_versions, "1.5");
    }
}

int main() {
    test_record_roundtrip();
    test_msg_get_roundtrip();
    test_msg_response_roundtrip();
    test_notify_usp_1_5_tags();
    test_new_request_roundtrips();
    test_new_response_roundtrips();

    std::printf("test_proto: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
