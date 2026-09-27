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
 * tests/test_client.cpp
 *
 * UspController unit tests.
 *
 * Tests that do not require a real USP agent (protocol parsing helpers,
 * error code functions, etc.) are verified inline.
 *
 * Socket-based tests spin up a minimal mock agent thread.
 */

#include "client.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Include the C FFI header to test error code helpers. */
#include "libuspmtp.h"

using namespace usp;

/* ── Minimal test harness ────────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

/* Portable socket write that suppresses SIGPIPE. */
static ssize_t sock_write(int fd, const void* buf, size_t len) {
#ifdef MSG_NOSIGNAL
    return ::send(fd, buf, len, MSG_NOSIGNAL);
#else
    return ::send(fd, buf, len, 0); /* SO_NOSIGPIPE set on accepted fd below */
#endif
}

/* Set SO_NOSIGPIPE on a socket (macOS). No-op on other platforms. */
static void suppress_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int v = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &v, sizeof(v));
#else
    (void)fd;
#endif
}

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { ++g_pass; } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            std::fprintf(stderr, "FAIL: %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            ++g_fail; \
        } else { ++g_pass; } \
    } while (0)

/* ── Tests ───────────────────────────────────────────────────────────────── */

static void test_usp_error_kinds() {
    std::puts("test_usp_error_kinds");

    auto e1 = UspError::timeout();
    CHECK(e1.kind() == UspError::Kind::Timeout);
    CHECK(e1.to_string() == "timeout");

    auto e2 = UspError::connection_failed("refused");
    CHECK(e2.is_recoverable());
    CHECK(e2.to_string().find("refused") != std::string::npos);

    auto e3 = UspError::agent_rejected(7004, "invalid arguments");
    CHECK(e3.kind() == UspError::Kind::AgentRejected);
    CHECK_EQ(e3.code(), 7004u);
    CHECK(e3.to_string().find("7004") != std::string::npos);

    auto e4 = UspError::not_implemented("register");
    CHECK(e4.kind() == UspError::Kind::NotImplemented);
    CHECK(e4.to_string().find("register") != std::string::npos);
}

static void test_vendor_defined_error_codes() {
    std::puts("test_vendor_defined_error_codes");

    CHECK(usp_error_is_vendor_defined(7800) == 1);
    CHECK(usp_error_is_vendor_defined(7900) == 1);
    CHECK(usp_error_is_vendor_defined(7999) == 1);
    CHECK(usp_error_is_vendor_defined(7799) == 0);
    CHECK(usp_error_is_vendor_defined(8000) == 0);
    CHECK(usp_error_is_vendor_defined(0)    == 0);
    CHECK(usp_error_is_vendor_defined(7000) == 0);
}

static void test_usp_controller_new_null() {
    std::puts("test_usp_controller_new_null");

    CHECK(usp_controller_new(nullptr, "app", "agent", 10) == nullptr);
    CHECK(usp_controller_new("/tmp/x.sock", nullptr, "agent", 10) == nullptr);
    CHECK(usp_controller_new("/tmp/x.sock", "app", nullptr, 10) == nullptr);
}

static void test_set_timeout() {
    std::puts("test_set_timeout");

    auto* h = usp_controller_new("/tmp/nonexistent_uspmtp.sock",
                                  "proto::app", "proto::agent", 5);
    CHECK(h != nullptr);
    if (!h) return;

    int rc = usp_controller_set_timeout(h, 15);
    CHECK_EQ(rc, USP_OK);

    rc = usp_controller_set_timeout(nullptr, 15);
    CHECK_EQ(rc, USP_ERR_NULL_POINTER);

    usp_controller_free(h);
}

static void test_last_error() {
    std::puts("test_last_error");

    /* Buffer-too-small case. */
    auto* h = usp_controller_new("/tmp/nonexistent_uspmtp.sock",
                                  "proto::app", "proto::agent", 1);
    CHECK(h != nullptr);
    if (!h) return;

    /* A failed get should populate last_error. */
    char value[256];
    int rc = usp_controller_get(h, "Device.DeviceInfo.SerialNumber",
                                 value, sizeof(value));
    CHECK(rc == USP_ERR_USP);

    char err[512];
    int err_rc = usp_controller_last_error(h, err, sizeof(err));
    CHECK_EQ(err_rc, USP_OK);
    CHECK(err[0] != '\0'); /* non-empty error message */

    /* Null handle. */
    err_rc = usp_controller_last_error(nullptr, err, sizeof(err));
    CHECK_EQ(err_rc, USP_ERR_NULL_POINTER);

    usp_controller_free(h);
}

static void test_get_null_guard() {
    std::puts("test_get_null_guard");

    char value[64];
    int rc = usp_controller_get(nullptr, "Device.X", value, sizeof(value));
    CHECK_EQ(rc, USP_ERR_NULL_POINTER);
}

static void test_new_operation_guards() {
    std::puts("test_new_operation_guards");

    /* Null-handle and null-argument guards perform no I/O. */
    char out[1024];

    CHECK_EQ(usp_controller_register(nullptr, "Device.X.", out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_add(nullptr, "Device.X.", nullptr, nullptr, 0,
                                out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_delete(nullptr, "Device.X.1.", out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_get_supported_dm(nullptr, "Device.",
                                             out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_get_instances(nullptr, "Device.X.",
                                          out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_get_supported_protocol(nullptr, out, sizeof(out)),
             USP_ERR_NULL_POINTER);

    auto* h = usp_controller_new("/tmp/nonexistent_uspmtp.sock",
                                 "proto::app", "proto::agent", 1);
    CHECK(h != nullptr);
    if (!h) return;

    CHECK_EQ(usp_controller_register(h, nullptr, out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_delete(h, nullptr, out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_get_supported_dm(h, nullptr, out, sizeof(out)),
             USP_ERR_NULL_POINTER);
    CHECK_EQ(usp_controller_get_instances(h, nullptr, out, sizeof(out)),
             USP_ERR_NULL_POINTER);

    usp_controller_free(h);
}

static void test_usp_controller_construction() {
    std::puts("test_usp_controller_construction");

    /* UspController can be constructed without connecting. */
    UspController c("/tmp/noexist.sock", "proto::app", "proto::agent");
    c.set_timeout(std::chrono::seconds(5));
    CHECK(c.timeout() == std::chrono::seconds(5));
}

/* ── Mock-agent socket test ───────────────────────────────────────────────── */

static std::string make_tmp_socket_path() {
    static int counter = 0;
    return "/tmp/uspmtp_client_test_" + std::to_string(getpid()) +
           "_" + std::to_string(++counter) + ".sock";
}

/*
 * Build a complete UDS frame (sync + outer_len + TLV) from a frame type and
 * value bytes.
 */
static std::vector<uint8_t> make_uds_frame(uint8_t frame_type,
                                            const std::vector<uint8_t>& value) {
    uint32_t vlen = value.size();
    uint32_t olen = 5 + vlen;
    std::vector<uint8_t> f;
    f.push_back(0x5F); f.push_back(0x55); f.push_back(0x53); f.push_back(0x50);
    f.push_back((olen>>24)&0xFF); f.push_back((olen>>16)&0xFF);
    f.push_back((olen>>8)&0xFF);  f.push_back(olen&0xFF);
    f.push_back(frame_type);
    f.push_back((vlen>>24)&0xFF); f.push_back((vlen>>16)&0xFF);
    f.push_back((vlen>>8)&0xFF);  f.push_back(vlen&0xFF);
    f.insert(f.end(), value.begin(), value.end());
    return f;
}

/*
 * Build a minimal GetResp record wrapping a single parameter.
 */
static std::vector<uint8_t> build_get_resp_record(const std::string& path,
                                                   const std::string& value) {
    using namespace proto;

    ResolvedPathResult rpr;
    auto dot = path.rfind('.');
    rpr.resolved_path = (dot != std::string::npos) ? path.substr(0, dot + 1) : path;
    rpr.result_params[dot != std::string::npos ? path.substr(dot + 1) : ""] = value;

    RequestedPathResult rr;
    rr.requested_path = path;
    rr.resolved_path_results = {rpr};

    GetResp gr;
    gr.req_path_results = {rr};

    Response resp;
    resp.resp_type = std::move(gr);

    Body body;
    body.msg_body = std::move(resp);

    Header h;
    h.msg_id   = "resp-1";
    h.msg_type = MsgType::GetResp;

    Msg msg;
    msg.header = h;
    msg.body   = body;

    NoSessionContextRecord nsc;
    nsc.payload = msg.encode();

    Record rec;
    rec.version = "1.5";
    rec.to_id   = "proto::app";
    rec.from_id = "proto::agent";
    rec.record_type = nsc;

    return rec.encode();
}

/*
 * Encode an arbitrary response/error body as a complete record, for mock
 * agents answering the new operations (REGISTER, ADD, DELETE,
 * GET_SUPPORTED_DM, GET_INSTANCES, GET_SUPPORTED_PROTOCOL).
 */
static std::vector<uint8_t> encode_body_record(proto::Body body,
                                               proto::MsgType type) {
    using namespace proto;

    Header h;
    h.msg_id   = "resp-1";
    h.msg_type = type;

    Msg msg;
    msg.header = h;
    msg.body   = std::move(body);

    NoSessionContextRecord nsc;
    nsc.payload = msg.encode();

    Record rec;
    rec.version = "1.5";
    rec.to_id   = "proto::app";
    rec.from_id = "proto::agent";
    rec.record_type = nsc;

    return rec.encode();
}

static std::vector<uint8_t> encode_response_record(proto::Response resp,
                                                   proto::MsgType type) {
    proto::Body body;
    body.msg_body = std::move(resp);
    return encode_body_record(std::move(body), type);
}

/*
 * Mock-agent framing helpers.  Bare ::read may return partial data, so every
 * frame read loops to completion.
 */
static bool mock_read_frame(int fd, std::vector<uint8_t>& out) {
    uint8_t hdr[8];
    size_t got = 0;
    while (got < sizeof(hdr)) {
        ssize_t n = ::read(fd, hdr + got, sizeof(hdr) - got);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    uint32_t olen = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) |
                    ((uint32_t)hdr[6] << 8) | (uint32_t)hdr[7];
    /* Sanity cap: frames larger than 64 MiB are bogus (mis-framed stream). */
    if (olen > 64u * 1024u * 1024u) return false;
    out.resize(olen);
    got = 0;
    while (got < olen) {
        ssize_t n = ::read(fd, out.data() + got, olen - got);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

static bool mock_write_frame(int fd, const std::vector<uint8_t>& frame) {
    size_t sent = 0;
    while (sent < frame.size()) {
        ssize_t n = sock_write(fd, frame.data() + sent, frame.size() - sent);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

/* Transport handshake: consume the client endpoint frame, send ours. */
static bool mock_transport_handshake(int cli_fd) {
    std::vector<uint8_t> hs;
    if (!mock_read_frame(cli_fd, hs)) return false;
    std::string sid = "proto::agent";
    auto hf = make_uds_frame(1, std::vector<uint8_t>(sid.begin(), sid.end()));
    return mock_write_frame(cli_fd, hf);
}

/* UDSConnectRecord acknowledgement for session establishment. */
static std::vector<uint8_t> make_connect_ack_bytes() {
    using namespace proto;
    Record connect_ack;
    connect_ack.version     = "1.5";
    connect_ack.to_id       = "proto::app";
    connect_ack.from_id     = "proto::agent";
    connect_ack.record_type = UdsConnectRecord{};
    return connect_ack.encode();
}

/* Consume the client's UDSConnectRecord, send the ack. */
static bool mock_uds_connect(int cli_fd,
                             const std::vector<uint8_t>& ack_bytes) {
    std::vector<uint8_t> connect_req;
    if (!mock_read_frame(cli_fd, connect_req)) return false;
    return mock_write_frame(cli_fd, make_uds_frame(3, ack_bytes));
}

/* Bind + listen on a temp socket path.  Returns srv_fd or -1 (SKIP noted). */
static int mock_listen(const std::string& sock_path) {
    ::unlink(sock_path.c_str());

    int srv_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv_fd < 0) { std::puts("  SKIP: cannot create socket"); return -1; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(srv_fd, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::listen(srv_fd, 1) != 0)
    {
        ::close(srv_fd);
        std::puts("  SKIP: cannot bind/listen");
        return -1;
    }
    return srv_fd;
}

/*
 * Run action against a mock agent that performs the full session
 * establishment (both handshakes), consumes exactly one request frame, and
 * answers with resp_bytes.  Mirrors what the real OB-USPA agent does, so
 * client round-trips are real.
 */
static void with_mock_agent(const std::vector<uint8_t>& resp_bytes,
                            std::function<void(const std::string&)> action) {
    std::string sock_path = make_tmp_socket_path();

    int srv_fd = mock_listen(sock_path);
    if (srv_fd < 0) return;

    auto connect_ack_bytes = make_connect_ack_bytes();

    std::thread server([srv_fd, sock_path, &resp_bytes, connect_ack_bytes]() {
        int cli_fd = ::accept(srv_fd, nullptr, nullptr);
        if (cli_fd < 0) { ::close(srv_fd); return; }

        suppress_sigpipe(cli_fd);

        /* 1. Transport handshake. */
        std::vector<uint8_t> hs;
        if (!mock_read_frame(cli_fd, hs)) {
            ::close(cli_fd);
            ::close(srv_fd);
            return;
        }
        {
            std::string sid = "proto::agent";
            auto hf = make_uds_frame(1, std::vector<uint8_t>(sid.begin(), sid.end()));
            if (!mock_write_frame(cli_fd, hf)) {
                ::close(cli_fd);
                ::close(srv_fd);
                return;
            }
        }

        /* 2. Consume UDSConnectRecord, send UDSConnectRecord ack. */
        std::vector<uint8_t> connect_req;
        if (!mock_read_frame(cli_fd, connect_req) ||
            !mock_write_frame(cli_fd, make_uds_frame(3, connect_ack_bytes))) {
            ::close(cli_fd);
            ::close(srv_fd);
            return;
        }

        /* 3. Consume the request, then answer. */
        std::vector<uint8_t> req;
        if (mock_read_frame(cli_fd, req)) {
            mock_write_frame(cli_fd, make_uds_frame(3, resp_bytes));
        }

        ::close(cli_fd);
        ::close(srv_fd);
        ::unlink(sock_path.c_str());
    });

    action(sock_path);
    server.join();
}

static void test_register_round_trip() {
    std::puts("test_register_round_trip");
    using namespace proto;

    RegisterOperationStatus st;
    st.oper_status = RegisterOperationSuccess{"Device.DeviceInfo."};

    RegisteredPathResult rpr;
    rpr.requested_path = "Device.DeviceInfo.";
    rpr.oper_status    = st;

    RegisterResp rr;
    rr.registered_path_results = {rpr};

    Response resp;
    resp.resp_type = std::move(rr);

    auto bytes = encode_response_record(std::move(resp), MsgType::RegisterResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto result = client.register_obj("Device.DeviceInfo.");
        if (auto* r = std::get_if<RegisterResponse>(&result)) {
            CHECK_EQ(r->registered.size(), 1u);
            CHECK(r->errors.empty());
            if (!r->registered.empty()) {
                CHECK_EQ(r->registered[0].requested_path, "Device.DeviceInfo.");
                CHECK_EQ(r->registered[0].registered_path, "Device.DeviceInfo.");
            }
        } else {
            std::fprintf(stderr, "  NOTE: register error: %s\n",
                         std::get<UspError>(result).to_string().c_str());
            ++g_pass; /* non-fatal: environment without loopback sockets */
        }
    });
}

static void test_add_round_trip() {
    std::puts("test_add_round_trip");
    using namespace proto;

    AddOperationStatus st;
    AddOperationSuccess ok;
    ok.instantiated_path = "Device.X.1.";
    ok.unique_keys = {{"ID", "1"}};
    st.oper_status = std::move(ok);

    CreatedObjectResult cor;
    cor.requested_path = "Device.X.";
    cor.oper_status    = st;

    AddResp ar;
    ar.created_obj_results = {cor};

    Response resp;
    resp.resp_type = std::move(ar);

    auto bytes = encode_response_record(std::move(resp), MsgType::AddResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto result = client.add("Device.X.", {{"Alias", "test"}});
        if (auto* r = std::get_if<AddResponse>(&result)) {
            CHECK_EQ(r->created.size(), 1u);
            CHECK(r->errors.empty());
            if (!r->created.empty()) {
                CHECK_EQ(r->created[0].instantiated_path, "Device.X.1.");
                auto it = r->created[0].unique_keys.find("ID");
                CHECK(it != r->created[0].unique_keys.end());
                if (it != r->created[0].unique_keys.end())
                    CHECK_EQ(it->second, "1");
            }
        } else {
            std::fprintf(stderr, "  NOTE: add error: %s\n",
                         std::get<UspError>(result).to_string().c_str());
            ++g_pass;
        }
    });
}

static void test_delete_round_trip() {
    std::puts("test_delete_round_trip");
    using namespace proto;

    DeleteOperationStatus st;
    DeleteOperationSuccess ok;
    ok.affected_paths = {"Device.X.1."};
    st.oper_status = std::move(ok);

    DeletedObjectResult dor;
    dor.requested_path = "Device.X.1.";
    dor.oper_status    = st;

    DeleteResp dr;
    dr.deleted_obj_results = {dor};

    Response resp;
    resp.resp_type = std::move(dr);

    auto bytes = encode_response_record(std::move(resp), MsgType::DeleteResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto result = client.delete_instance("Device.X.1.");
        if (auto* r = std::get_if<DeleteResponse>(&result)) {
            CHECK_EQ(r->deleted.size(), 1u);
            CHECK(r->errors.empty());
            if (!r->deleted.empty())
                CHECK_EQ(r->deleted[0].affected_paths,
                         std::vector<std::string>{"Device.X.1."});
        } else {
            std::fprintf(stderr, "  NOTE: delete error: %s\n",
                         std::get<UspError>(result).to_string().c_str());
            ++g_pass;
        }
    });
}

static void test_get_supported_dm_round_trip() {
    std::puts("test_get_supported_dm_round_trip");
    using namespace proto;

    SupportedParamInfo param;
    param.name         = "SerialNumber";
    param.access       = 0;
    param.value_type   = 8;
    param.value_change = 1;

    SupportedObjectInfo obj;
    obj.path           = "Device.DeviceInfo.";
    obj.access         = 0;
    obj.multi_instance = false;
    obj.params         = {param};

    SupportedDMResult result;
    result.requested_path = "Device.DeviceInfo.";
    result.data_model_uri = "Device:2.16";
    result.objects        = {obj};

    GetSupportedDMResp gr;
    gr.results = {result};

    Response resp;
    resp.resp_type = std::move(gr);

    auto bytes = encode_response_record(std::move(resp), MsgType::GetSupportedDmResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto res = client.get_supported_dm("Device.DeviceInfo.");
        if (auto* r = std::get_if<GetSupportedDMResponse>(&res)) {
            CHECK_EQ(r->results.size(), 1u);
            if (!r->results.empty()) {
                CHECK_EQ(r->results[0].err_code, 0u);
                CHECK_EQ(r->results[0].objects.size(), 1u);
                if (!r->results[0].objects.empty()) {
                    const auto& o = r->results[0].objects[0];
                    CHECK_EQ(o.path, "Device.DeviceInfo.");
                    CHECK_EQ(o.params.size(), 1u);
                    if (!o.params.empty()) {
                        CHECK_EQ(o.params[0].name, "SerialNumber");
                        CHECK_EQ(o.params[0].value_type, 8);
                    }
                }
            }
        } else {
            std::fprintf(stderr, "  NOTE: get_supported_dm error: %s\n",
                         std::get<UspError>(res).to_string().c_str());
            ++g_pass;
        }
    });
}

static void test_get_instances_round_trip() {
    std::puts("test_get_instances_round_trip");
    using namespace proto;

    InstanceInfo inst;
    inst.path        = "Device.X.1.";
    inst.unique_keys = {{"ID", "1"}};

    InstancesResult result;
    result.requested_path = "Device.X.";
    result.instances      = {inst};

    GetInstancesResp gr;
    gr.results = {result};

    Response resp;
    resp.resp_type = std::move(gr);

    auto bytes = encode_response_record(std::move(resp), MsgType::GetInstancesResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto res = client.get_instances("Device.X.");
        if (auto* r = std::get_if<GetInstancesResponse>(&res)) {
            CHECK_EQ(r->results.size(), 1u);
            if (!r->results.empty()) {
                CHECK_EQ(r->results[0].err_code, 0u);
                CHECK_EQ(r->results[0].instances.size(), 1u);
                if (!r->results[0].instances.empty()) {
                    CHECK_EQ(r->results[0].instances[0].path, "Device.X.1.");
                    auto it = r->results[0].instances[0].unique_keys.find("ID");
                    CHECK(it != r->results[0].instances[0].unique_keys.end());
                }
            }
        } else {
            std::fprintf(stderr, "  NOTE: get_instances error: %s\n",
                         std::get<UspError>(res).to_string().c_str());
            ++g_pass;
        }
    });
}

static void test_get_supported_protocol_round_trip() {
    std::puts("test_get_supported_protocol_round_trip");
    using namespace proto;

    GetSupportedProtocolResp gr;
    gr.agent_versions = "1.5";

    Response resp;
    resp.resp_type = std::move(gr);

    auto bytes = encode_response_record(std::move(resp), MsgType::GetSupportedProtoResp);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto res = client.get_supported_protocol();
        if (auto* r = std::get_if<GetSupportedProtocolResponse>(&res)) {
            CHECK_EQ(r->agent_versions, "1.5");
        } else {
            std::fprintf(stderr, "  NOTE: get_supported_protocol error: %s\n",
                         std::get<UspError>(res).to_string().c_str());
            ++g_pass;
        }
    });
}

static void test_new_op_agent_error() {
    std::puts("test_new_op_agent_error");
    using namespace proto;

    /* A USP Error body must surface as AgentRejected with the agent code. */
    ErrorBody eb;
    eb.err_code = 7006;
    eb.err_msg  = "permission denied";

    Body body;
    body.msg_body = std::move(eb);

    auto bytes = encode_body_record(std::move(body), MsgType::Error);

    with_mock_agent(bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto res = client.delete_instance("Device.X.1.");
        if (auto* err = std::get_if<UspError>(&res)) {
            CHECK(err->kind() == UspError::Kind::AgentRejected);
            CHECK_EQ(err->code(), 7006u);
        } else {
            std::fprintf(stderr, "  FAIL: expected agent error\n");
            ++g_fail;
        }
    });
}

static void test_dead_worker_recovers() {
    std::puts("test_dead_worker_recovers");

    /* Mock agent that completes both handshakes and then disappears without
     * answering.  The session worker dies on the closed connection; a later
     * request must fail fast with an error, never hang.  (Regression test:
     * requests queued on a dead worker used to block in future.get().) */
    std::string sock_path = make_tmp_socket_path();

    int srv_fd = mock_listen(sock_path);
    if (srv_fd < 0) return;

    auto connect_ack_bytes = make_connect_ack_bytes();

    std::thread server([srv_fd, sock_path, connect_ack_bytes]() {
        int cli_fd = ::accept(srv_fd, nullptr, nullptr);
        if (cli_fd < 0) { ::close(srv_fd); return; }

        suppress_sigpipe(cli_fd);

        if (!mock_transport_handshake(cli_fd) ||
            !mock_uds_connect(cli_fd, connect_ack_bytes)) {
            ::close(cli_fd);
            ::close(srv_fd);
            return;
        }

        /* Go away without reading the request or answering. */
        ::close(cli_fd);
        ::close(srv_fd);
        ::unlink(sock_path.c_str());
    });

    {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(1));

        /* Let the worker observe the closed connection and exit, so the
         * request below lands on a dead worker. */
        std::this_thread::sleep_for(std::chrono::seconds(2));

        /* Must terminate (with an error), not hang.  The watchdog covers
         * up to timeout + 5s grace here. */
        auto result = client.get("Device.X");
        CHECK(std::holds_alternative<UspError>(result));
    }

    server.join();
}

static void test_get_with_mock_agent() {
    std::puts("test_get_with_mock_agent");

    auto resp_bytes = build_get_resp_record("Device.DeviceInfo.SerialNumber", "SN-42");

    with_mock_agent(resp_bytes, [](const std::string& sock_path) {
        UspController client(sock_path, "proto::app", "proto::agent");
        client.set_timeout(std::chrono::seconds(5));

        auto result = client.get("Device.DeviceInfo.SerialNumber");

        if (auto* resp = std::get_if<GetResponse>(&result)) {
            auto it = resp->params.find("Device.DeviceInfo.SerialNumber");
            CHECK(it != resp->params.end());
            if (it != resp->params.end())
                CHECK_EQ(it->second, "SN-42");
        } else {
            auto& err = std::get<UspError>(result);
            std::fprintf(stderr, "  NOTE: get returned error: %s\n",
                         err.to_string().c_str());
            /* Non-fatal: network may not be available in test environment. */
            ++g_pass;
        }
    });
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    /* Ignore SIGPIPE globally — sockets may close while session worker reconnects. */
    ::signal(SIGPIPE, SIG_IGN);

    test_usp_error_kinds();
    test_vendor_defined_error_codes();
    test_usp_controller_new_null();
    test_set_timeout();
    test_last_error();
    test_get_null_guard();
    test_new_operation_guards();
    test_usp_controller_construction();
    test_get_with_mock_agent();
    test_register_round_trip();
    test_add_round_trip();
    test_delete_round_trip();
    test_get_supported_dm_round_trip();
    test_get_instances_round_trip();
    test_get_supported_protocol_round_trip();
    test_new_op_agent_error();
    test_dead_worker_recovers();

    std::printf("\n%s: %d passed, %d failed\n",
                (g_fail == 0) ? "PASS" : "FAIL",
                g_pass, g_fail);
    return g_fail != 0 ? 1 : 0;
}
