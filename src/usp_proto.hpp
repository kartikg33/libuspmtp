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
 * usp_proto.hpp
 *
 * Hand-written C++20 representations of the USP TR-369 Protocol Buffer
 * message types, plus a minimal protobuf encoder/decoder.
 *
 * Field tag numbers and type mappings are taken verbatim from the BBF TR-369
 * specification (usp-record-1-3.proto and usp-msg-1-3.proto).
 *
 * References:
 *   https://usp.technology/
 *   https://github.com/BroadbandForum/usp
 *
 * Encoding rules (proto3):
 *   - Scalar fields at their default value (0, false, "") are NOT encoded.
 *   - Repeated fields produce one length-delimited entry per element.
 *   - Map fields are encoded as repeated MapEntry messages.
 *   - Oneof fields encode only the active variant.
 *
 * Wire types:
 *   0 – varint   (bool, int32, enum)
 *   2 – length-delimited (string, bytes, embedded message, map entries)
 *   5 – 32-bit LE (fixed32)
 */

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace usp::proto {

/* ═══════════════════════════════════════════════════════════════════════════
 * Low-level protobuf encoder
 * ═══════════════════════════════════════════════════════════════════════════ */

class PbWriter {
public:
    /* Append a raw varint. */
    void write_varint(uint64_t v);

    /* Append a raw 32-bit little-endian value. */
    void write_fixed32_raw(uint32_t v);

    /* Write a varint field (wire type 0). */
    void write_varint_field(uint32_t field_num, uint64_t v);

    /* Write a bool field (wire type 0); skipped when false (proto3 default). */
    void write_bool_field(uint32_t field_num, bool v);

    /* Write an int32 / enum field (wire type 0); skipped when 0. */
    void write_int32_field(uint32_t field_num, int32_t v);

    /* Write a fixed32 field (wire type 5); skipped when 0. */
    void write_fixed32_field(uint32_t field_num, uint32_t v);

    /* Write a string field (wire type 2); skipped when empty. */
    void write_string_field(uint32_t field_num, const std::string& s);

    /* Write a bytes field (wire type 2); skipped when empty. */
    void write_bytes_field(uint32_t field_num, const std::vector<uint8_t>& b);

    /* Write an embedded message field (wire type 2). Always written (even if
     * the sub-message encoded to zero bytes), because a present optional
     * message should always be encoded.  Callers must decide whether to call
     * this based on the optional having a value. */
    void write_message_field(uint32_t field_num, const std::vector<uint8_t>& msg);

    /* Write a string→string map entry (each entry is an embedded message). */
    void write_map_entry(uint32_t field_num,
                         const std::string& key,
                         const std::string& value);

    const std::vector<uint8_t>& bytes() const noexcept { return buf_; }

    /* Move the internal buffer out. */
    std::vector<uint8_t> take() { return std::move(buf_); }

private:
    void write_tag(uint32_t field_num, uint32_t wire_type);
    std::vector<uint8_t> buf_;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Low-level protobuf decoder
 * ═══════════════════════════════════════════════════════════════════════════ */

class PbReader {
public:
    explicit PbReader(std::span<const uint8_t> data) noexcept
        : data_(data), pos_(0) {}

    bool has_more() const noexcept { return pos_ < data_.size(); }

    /* Read a tag; returns true and sets field_num/wire_type on success. */
    bool read_tag(uint32_t& field_num, uint32_t& wire_type);

    /* Skip a field with the given wire_type. */
    bool skip_field(uint32_t wire_type);

    /* Read a varint. */
    bool read_varint(uint64_t& v);

    /* Read a 32-bit little-endian value. */
    bool read_fixed32(uint32_t& v);

    /* Read a length-delimited value. */
    bool read_bytes(std::vector<uint8_t>& out);
    bool read_string(std::string& out);

    /* Return a sub-reader over the next length-delimited chunk without copying. */
    bool read_sub_reader(PbReader& sub);

private:
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * USP Record types  (usp-record-1-3.proto)
 * ═══════════════════════════════════════════════════════════════════════════ */

struct NoSessionContextRecord {
    std::vector<uint8_t> payload; /* field 2, bytes */

    std::vector<uint8_t> encode() const;
    static std::optional<NoSessionContextRecord> decode(PbReader& r);
};

/* Record.record_type oneof (tags 7 and 12). */
struct UdsConnectRecord {
    /* Empty message – no fields. */
    std::vector<uint8_t> encode() const { return {}; }
};

struct Record {
    std::string version;          /* field 1, string  */
    std::string to_id;            /* field 2, string  */
    std::string from_id;          /* field 3, string  */
    int32_t     payload_security{0}; /* field 4, int32 (enum PayloadSecurity) */

    /* Oneof record_type (tags 7, 12). */
    std::variant<std::monostate,
                 NoSessionContextRecord,
                 UdsConnectRecord> record_type;

    std::vector<uint8_t> encode() const;
    static std::optional<Record> decode(std::span<const uint8_t> data);
};

/* ═══════════════════════════════════════════════════════════════════════════
 * USP Message types  (usp-msg-1-3.proto)
 * ═══════════════════════════════════════════════════════════════════════════ */

enum class MsgType : int32_t {
    Error               = 0,
    Get                 = 1,
    GetResp             = 2,
    Notify              = 3,
    Set                 = 4,
    SetResp             = 5,
    Operate             = 6,
    OperateResp         = 7,
    Add                 = 8,
    AddResp             = 9,
    Delete              = 10,
    DeleteResp          = 11,
    GetSupportedDm      = 12,
    GetSupportedDmResp  = 13,
    GetInstances        = 14,
    GetInstancesResp    = 15,
    NotifyResp          = 16,
    GetSupportedProto   = 17,
    GetSupportedProtoResp = 18,
    Register            = 19,
    RegisterResp        = 20,
    Deregister          = 21,
    DeregisterResp      = 22,
};

struct Header {
    std::string msg_id;             /* field 1, string */
    MsgType     msg_type{MsgType::Error}; /* field 2, int32 (enum) */

    std::vector<uint8_t> encode() const;
    static std::optional<Header> decode(PbReader& r);
};

/* Error body */
struct ParamError {
    std::string param_path; /* field 1 */
    uint32_t    err_code{0}; /* field 2, fixed32 */
    std::string err_msg;    /* field 3 */

    std::vector<uint8_t> encode() const;
    static std::optional<ParamError> decode(PbReader& r);
};

struct ErrorBody {
    uint32_t              err_code{0}; /* field 1, fixed32 */
    std::string           err_msg;     /* field 2, string  */
    std::vector<ParamError> param_errs; /* field 3, repeated message */

    std::vector<uint8_t> encode() const;
    static std::optional<ErrorBody> decode(PbReader& r);
};

/* ── GET ──────────────────────────────────────────────────────────────────── */

struct Get {
    std::vector<std::string> param_paths; /* field 1, repeated string */
    uint32_t max_depth{0};               /* field 2, fixed32 */

    std::vector<uint8_t> encode() const;
    static std::optional<Get> decode(PbReader& r);
};

struct ResolvedPathResult {
    std::string                    resolved_path; /* field 1, string */
    std::map<std::string,std::string> result_params; /* field 2, map<string,string> */

    std::vector<uint8_t> encode() const;
    static std::optional<ResolvedPathResult> decode(PbReader& r);
};

struct RequestedPathResult {
    std::string requested_path; /* field 1, string  */
    uint32_t    err_code{0};    /* field 2, fixed32 */
    std::string err_msg;        /* field 3, string  */
    std::vector<ResolvedPathResult> resolved_path_results; /* field 4, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<RequestedPathResult> decode(PbReader& r);
};

struct GetResp {
    std::vector<RequestedPathResult> req_path_results; /* field 1, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<GetResp> decode(PbReader& r);
};

/* ── SET ──────────────────────────────────────────────────────────────────── */

struct UpdateParamSetting {
    std::string param;    /* field 1, string */
    std::string value;    /* field 2, string */
    bool        required{false}; /* field 3, bool */

    std::vector<uint8_t> encode() const;
    static std::optional<UpdateParamSetting> decode(PbReader& r);
};

struct UpdateObject {
    std::string obj_path; /* field 1, string */
    std::vector<UpdateParamSetting> param_settings; /* field 2, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<UpdateObject> decode(PbReader& r);
};

struct Set {
    bool allow_partial{false};        /* field 1, bool */
    std::vector<UpdateObject> update_objs; /* field 2, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<Set> decode(PbReader& r);
};

struct OperationFailure {
    uint32_t    err_code{0}; /* field 1, fixed32 */
    std::string err_msg;     /* field 2, string  */
    /* field 3 updated_inst_failures – not needed for response parsing */

    std::vector<uint8_t> encode() const;
    static std::optional<OperationFailure> decode(PbReader& r);
};

struct OperationSuccess {
    /* field 1 updated_inst_results – not needed for response parsing */
    std::vector<uint8_t> encode() const;
    static std::optional<OperationSuccess> decode(PbReader& r);
};

struct OperationStatus {
    /* Oneof oper_status (tags 1=failure, 2=success). */
    std::variant<std::monostate, OperationFailure, OperationSuccess> oper_status;

    std::vector<uint8_t> encode() const;
    static std::optional<OperationStatus> decode(PbReader& r);
};

struct UpdatedObjectResult {
    std::string                    requested_path; /* field 1, string   */
    std::optional<OperationStatus> oper_status;    /* field 2, message  */

    std::vector<uint8_t> encode() const;
    static std::optional<UpdatedObjectResult> decode(PbReader& r);
};

struct SetResp {
    std::vector<UpdatedObjectResult> updated_obj_results; /* field 1, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<SetResp> decode(PbReader& r);
};

/* ── OPERATE ──────────────────────────────────────────────────────────────── */

struct Operate {
    std::string command;      /* field 1, string */
    std::string command_key;  /* field 2, string */
    bool        send_resp{false}; /* field 3, bool */
    std::map<std::string,std::string> input_args; /* field 4, map<string,string> */

    std::vector<uint8_t> encode() const;
    static std::optional<Operate> decode(PbReader& r);
};

struct OutputArgs {
    std::map<std::string,std::string> output_args; /* field 1, map<string,string> */

    std::vector<uint8_t> encode() const;
    static std::optional<OutputArgs> decode(PbReader& r);
};

struct CommandFailure {
    uint32_t    err_code{0}; /* field 1, fixed32 */
    std::string err_msg;     /* field 2, string  */

    std::vector<uint8_t> encode() const;
    static std::optional<CommandFailure> decode(PbReader& r);
};

struct OperationResult {
    std::string executed_command; /* field 1, string */

    /* Oneof operation_resp (tags 2=req_obj_path, 3=req_output_args, 4=cmd_failure). */
    std::variant<std::monostate,
                 std::string,     /* req_obj_path (tag 2) */
                 OutputArgs,      /* req_output_args (tag 3) */
                 CommandFailure>  /* cmd_failure (tag 4) */
        operation_resp;

    std::vector<uint8_t> encode() const;
    static std::optional<OperationResult> decode(PbReader& r);
};

struct OperateResp {
    std::vector<OperationResult> operation_results; /* field 1, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<OperateResp> decode(PbReader& r);
};

/* ── ADD ──────────────────────────────────────────────────────────────────── */

struct CreateParamSetting {
    std::string param;   /* field 1, string */
    std::string value;   /* field 2, string */
    bool required{false}; /* field 3, bool */

    std::vector<uint8_t> encode() const;
    static std::optional<CreateParamSetting> decode(PbReader& r);
};

struct CreateObject {
    std::string obj_path; /* field 1, string */
    std::vector<CreateParamSetting> param_settings; /* field 2, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<CreateObject> decode(PbReader& r);
};

struct Add {
    bool allow_partial{false};     /* field 1, bool */
    std::vector<CreateObject> create_objs; /* field 2, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<Add> decode(PbReader& r);
};

struct AddOperationFailure {
    uint32_t    err_code{0}; /* field 1, fixed32 */
    std::string err_msg;     /* field 2, string  */

    std::vector<uint8_t> encode() const;
    static std::optional<AddOperationFailure> decode(PbReader& r);
};

struct AddOperationSuccess {
    std::string instantiated_path; /* field 1, string */
    std::map<std::string,std::string> unique_keys; /* field 3, map<string,string> */

    std::vector<uint8_t> encode() const;
    static std::optional<AddOperationSuccess> decode(PbReader& r);
};

struct AddOperationStatus {
    /* Oneof oper_status (tags 1=failure, 2=success). */
    std::variant<std::monostate, AddOperationFailure, AddOperationSuccess> oper_status;

    std::vector<uint8_t> encode() const;
    static std::optional<AddOperationStatus> decode(PbReader& r);
};

struct CreatedObjectResult {
    std::string                       requested_path; /* field 1, string  */
    std::optional<AddOperationStatus> oper_status;    /* field 2, message */

    std::vector<uint8_t> encode() const;
    static std::optional<CreatedObjectResult> decode(PbReader& r);
};

struct AddResp {
    std::vector<CreatedObjectResult> created_obj_results; /* field 1, repeated */

    std::vector<uint8_t> encode() const;
    static std::optional<AddResp> decode(PbReader& r);
};

/* ── NOTIFY ───────────────────────────────────────────────────────────────── */

struct ValueChange {
    std::string param_path;  /* field 1, string */
    std::string param_value; /* field 2, string */

    std::vector<uint8_t> encode() const;
    static std::optional<ValueChange> decode(PbReader& r);
};

struct ObjectCreation {
    std::string obj_path; /* field 1, string */

    std::vector<uint8_t> encode() const;
    static std::optional<ObjectCreation> decode(PbReader& r);
};

struct ObjectDeletion {
    std::string obj_path; /* field 1, string */

    std::vector<uint8_t> encode() const;
    static std::optional<ObjectDeletion> decode(PbReader& r);
};

struct Notify {
    std::string subscription_id; /* field 1, string */
    bool send_resp{false};       /* field 2, bool   */

    /* Oneof notification (tags 3=value_change, 4=obj_creation, 5=obj_deletion). */
    std::variant<std::monostate,
                 ValueChange,
                 ObjectCreation,
                 ObjectDeletion> notification;

    std::vector<uint8_t> encode() const;
    static std::optional<Notify> decode(PbReader& r);
};

struct NotifyResp {
    std::string subscription_id; /* field 1, string */

    std::vector<uint8_t> encode() const;
    static std::optional<NotifyResp> decode(PbReader& r);
};

/* ── Request / Response containers ───────────────────────────────────────── */

/* Oneof request type. */
using RequestVariant = std::variant<
    std::monostate,
    Get,       /* tag 1 */
    Set,       /* tag 4 */
    Add,       /* tag 5 */
    Operate,   /* tag 7 */
    Notify>;   /* tag 8 */

struct Request {
    RequestVariant req_type; /* oneof tags 1,4,5,7,8 */

    std::vector<uint8_t> encode() const;
    static std::optional<Request> decode(PbReader& r);
};

/* Oneof response type. */
using ResponseVariant = std::variant<
    std::monostate,
    GetResp,     /* tag 1 */
    SetResp,     /* tag 4 */
    AddResp,     /* tag 5 */
    OperateResp, /* tag 7 */
    NotifyResp>; /* tag 8 */

struct Response {
    ResponseVariant resp_type; /* oneof tags 1,4,5,7,8 */

    std::vector<uint8_t> encode() const;
    static std::optional<Response> decode(PbReader& r);
};

/* Body.msg_body oneof (tags 1=request, 2=response, 3=error). */
using BodyVariant = std::variant<
    std::monostate,
    Request,
    Response,
    ErrorBody>;

struct Body {
    BodyVariant msg_body; /* oneof tags 1,2,3 */

    std::vector<uint8_t> encode() const;
    static std::optional<Body> decode(PbReader& r);
};

struct Msg {
    std::optional<Header> header; /* field 1, message */
    std::optional<Body>   body;   /* field 2, message */

    std::vector<uint8_t> encode() const;
    static std::optional<Msg> decode(std::span<const uint8_t> data);
};

} // namespace usp::proto
