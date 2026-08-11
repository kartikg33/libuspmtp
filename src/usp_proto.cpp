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

#include "usp_proto.hpp"

#include "usp-msg-1-5.pb.h"
#include "usp-record-1-5.pb.h"

#include <limits>
#include <string>
#include <type_traits>

namespace usp::proto {
namespace {

template<typename Generated>
std::optional<Generated> parse_message(std::span<const uint8_t> data) {
    if (data.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    Generated out;
    if (!out.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        return std::nullopt;
    }
    return out;
}

template<typename Generated>
std::vector<uint8_t> serialize_message(const Generated& msg) {
    std::string bytes;
    if (!msg.SerializeToString(&bytes)) {
        return {};
    }
    return {bytes.begin(), bytes.end()};
}

static ::usp::Header_MsgType to_native(MsgType type) {
    return static_cast<::usp::Header_MsgType>(static_cast<int>(type));
}

static MsgType from_native(::usp::Header_MsgType type) {
    return static_cast<MsgType>(static_cast<int>(type));
}

static ::usp::Get to_native(const Get& in) {
    ::usp::Get out;
    for (const auto& path : in.param_paths) {
        out.add_param_paths(path);
    }
    out.set_max_depth(in.max_depth);
    return out;
}

static Get from_native(const ::usp::Get& in) {
    Get out;
    out.param_paths.assign(in.param_paths().begin(), in.param_paths().end());
    out.max_depth = in.max_depth();
    return out;
}

static ResolvedPathResult from_native(const ::usp::GetResp_ResolvedPathResult& in) {
    ResolvedPathResult out;
    out.resolved_path = in.resolved_path();
    for (const auto& entry : in.result_params()) {
        out.result_params.emplace(entry.first, entry.second);
    }
    return out;
}

static RequestedPathResult from_native(const ::usp::GetResp_RequestedPathResult& in) {
    RequestedPathResult out;
    out.requested_path = in.requested_path();
    out.err_code = in.err_code();
    out.err_msg = in.err_msg();
    for (const auto& result : in.resolved_path_results()) {
        out.resolved_path_results.push_back(from_native(result));
    }
    return out;
}

static GetResp from_native(const ::usp::GetResp& in) {
    GetResp out;
    for (const auto& result : in.req_path_results()) {
        out.req_path_results.push_back(from_native(result));
    }
    return out;
}

static ::usp::GetResp_ResolvedPathResult to_native(const ResolvedPathResult& in) {
    ::usp::GetResp_ResolvedPathResult out;
    out.set_resolved_path(in.resolved_path);
    auto* params = out.mutable_result_params();
    for (const auto& [key, value] : in.result_params) {
        (*params)[key] = value;
    }
    return out;
}

static ::usp::GetResp_RequestedPathResult to_native(const RequestedPathResult& in) {
    ::usp::GetResp_RequestedPathResult out;
    out.set_requested_path(in.requested_path);
    out.set_err_code(in.err_code);
    out.set_err_msg(in.err_msg);
    for (const auto& result : in.resolved_path_results) {
        *out.add_resolved_path_results() = to_native(result);
    }
    return out;
}

static ::usp::GetResp to_native(const GetResp& in) {
    ::usp::GetResp out;
    for (const auto& result : in.req_path_results) {
        *out.add_req_path_results() = to_native(result);
    }
    return out;
}

static ::usp::Set_UpdateParamSetting to_native(const UpdateParamSetting& in) {
    ::usp::Set_UpdateParamSetting out;
    out.set_param(in.param);
    out.set_value(in.value);
    out.set_required(in.required);
    return out;
}

static ::usp::Set_UpdateObject to_native(const UpdateObject& in) {
    ::usp::Set_UpdateObject out;
    out.set_obj_path(in.obj_path);
    for (const auto& setting : in.param_settings) {
        *out.add_param_settings() = to_native(setting);
    }
    return out;
}

static ::usp::Set to_native(const Set& in) {
    ::usp::Set out;
    out.set_allow_partial(in.allow_partial);
    for (const auto& object : in.update_objs) {
        *out.add_update_objs() = to_native(object);
    }
    return out;
}

static OperationFailure from_native(const ::usp::SetResp_UpdatedObjectResult_OperationStatus_OperationFailure& in) {
    return {in.err_code(), in.err_msg()};
}

static OperationStatus from_native(const ::usp::SetResp_UpdatedObjectResult_OperationStatus& in) {
    OperationStatus out;
    switch (in.oper_status_case()) {
    case ::usp::SetResp_UpdatedObjectResult_OperationStatus::kOperFailure:
        out.oper_status = from_native(in.oper_failure());
        break;
    case ::usp::SetResp_UpdatedObjectResult_OperationStatus::kOperSuccess:
        out.oper_status = OperationSuccess{};
        break;
    default:
        break;
    }
    return out;
}

static UpdatedObjectResult from_native(const ::usp::SetResp_UpdatedObjectResult& in) {
    UpdatedObjectResult out;
    out.requested_path = in.requested_path();
    if (in.has_oper_status()) {
        out.oper_status = from_native(in.oper_status());
    }
    return out;
}

static SetResp from_native(const ::usp::SetResp& in) {
    SetResp out;
    for (const auto& result : in.updated_obj_results()) {
        out.updated_obj_results.push_back(from_native(result));
    }
    return out;
}

static ::usp::Operate to_native(const Operate& in) {
    ::usp::Operate out;
    out.set_command(in.command);
    out.set_command_key(in.command_key);
    out.set_send_resp(in.send_resp);
    auto* args = out.mutable_input_args();
    for (const auto& [key, value] : in.input_args) {
        (*args)[key] = value;
    }
    return out;
}

static OutputArgs from_native(const ::usp::OperateResp_OperationResult_OutputArgs& in) {
    OutputArgs out;
    for (const auto& entry : in.output_args()) {
        out.output_args.emplace(entry.first, entry.second);
    }
    return out;
}

static CommandFailure from_native(const ::usp::OperateResp_OperationResult_CommandFailure& in) {
    return {in.err_code(), in.err_msg()};
}

static OperationResult from_native(const ::usp::OperateResp_OperationResult& in) {
    OperationResult out;
    out.executed_command = in.executed_command();
    switch (in.operation_resp_case()) {
    case ::usp::OperateResp_OperationResult::kReqObjPath:
        out.operation_resp = in.req_obj_path();
        break;
    case ::usp::OperateResp_OperationResult::kReqOutputArgs:
        out.operation_resp = from_native(in.req_output_args());
        break;
    case ::usp::OperateResp_OperationResult::kCmdFailure:
        out.operation_resp = from_native(in.cmd_failure());
        break;
    default:
        break;
    }
    return out;
}

static OperateResp from_native(const ::usp::OperateResp& in) {
    OperateResp out;
    for (const auto& result : in.operation_results()) {
        out.operation_results.push_back(from_native(result));
    }
    return out;
}

static ::usp::Add_CreateParamSetting to_native(const CreateParamSetting& in) {
    ::usp::Add_CreateParamSetting out;
    out.set_param(in.param);
    out.set_value(in.value);
    out.set_required(in.required);
    return out;
}

static ::usp::Add_CreateObject to_native(const CreateObject& in) {
    ::usp::Add_CreateObject out;
    out.set_obj_path(in.obj_path);
    for (const auto& setting : in.param_settings) {
        *out.add_param_settings() = to_native(setting);
    }
    return out;
}

static ::usp::Add to_native(const Add& in) {
    ::usp::Add out;
    out.set_allow_partial(in.allow_partial);
    for (const auto& object : in.create_objs) {
        *out.add_create_objs() = to_native(object);
    }
    return out;
}

static AddOperationFailure from_native(const ::usp::AddResp_CreatedObjectResult_OperationStatus_OperationFailure& in) {
    return {in.err_code(), in.err_msg()};
}

static AddOperationSuccess from_native(const ::usp::AddResp_CreatedObjectResult_OperationStatus_OperationSuccess& in) {
    AddOperationSuccess out;
    out.instantiated_path = in.instantiated_path();
    for (const auto& entry : in.unique_keys()) {
        out.unique_keys.emplace(entry.first, entry.second);
    }
    return out;
}

static AddOperationStatus from_native(const ::usp::AddResp_CreatedObjectResult_OperationStatus& in) {
    AddOperationStatus out;
    switch (in.oper_status_case()) {
    case ::usp::AddResp_CreatedObjectResult_OperationStatus::kOperFailure:
        out.oper_status = from_native(in.oper_failure());
        break;
    case ::usp::AddResp_CreatedObjectResult_OperationStatus::kOperSuccess:
        out.oper_status = from_native(in.oper_success());
        break;
    default:
        break;
    }
    return out;
}

static CreatedObjectResult from_native(const ::usp::AddResp_CreatedObjectResult& in) {
    CreatedObjectResult out;
    out.requested_path = in.requested_path();
    if (in.has_oper_status()) {
        out.oper_status = from_native(in.oper_status());
    }
    return out;
}

static AddResp from_native(const ::usp::AddResp& in) {
    AddResp out;
    for (const auto& result : in.created_obj_results()) {
        out.created_obj_results.push_back(from_native(result));
    }
    return out;
}

static ::usp::NotifyResp to_native(const NotifyResp& in) {
    ::usp::NotifyResp out;
    out.set_subscription_id(in.subscription_id);
    return out;
}

static ValueChange from_native(const ::usp::Notify_ValueChange& in) {
    return {in.param_path(), in.param_value()};
}

static ObjectCreation from_native(const ::usp::Notify_ObjectCreation& in) {
    ObjectCreation out;
    out.obj_path = in.obj_path();
    for (const auto& entry : in.unique_keys()) {
        out.unique_keys.emplace(entry.first, entry.second);
    }
    return out;
}

static ObjectDeletion from_native(const ::usp::Notify_ObjectDeletion& in) {
    return {in.obj_path()};
}

static Notify from_native(const ::usp::Notify& in) {
    Notify out;
    out.subscription_id = in.subscription_id();
    out.send_resp = in.send_resp();
    switch (in.notification_case()) {
    case ::usp::Notify::kValueChange:
        out.notification = from_native(in.value_change());
        break;
    case ::usp::Notify::kObjCreation:
        out.notification = from_native(in.obj_creation());
        break;
    case ::usp::Notify::kObjDeletion:
        out.notification = from_native(in.obj_deletion());
        break;
    default:
        break;
    }
    return out;
}

static ::usp::Notify to_native(const Notify& in) {
    ::usp::Notify out;
    out.set_subscription_id(in.subscription_id);
    out.set_send_resp(in.send_resp);
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ValueChange>) {
            auto* value_change = out.mutable_value_change();
            value_change->set_param_path(value.param_path);
            value_change->set_param_value(value.param_value);
        } else if constexpr (std::is_same_v<T, ObjectCreation>) {
            auto* obj_creation = out.mutable_obj_creation();
            obj_creation->set_obj_path(value.obj_path);
            auto* keys = obj_creation->mutable_unique_keys();
            for (const auto& [key, unique_value] : value.unique_keys) {
                (*keys)[key] = unique_value;
            }
        } else if constexpr (std::is_same_v<T, ObjectDeletion>) {
            out.mutable_obj_deletion()->set_obj_path(value.obj_path);
        }
    }, in.notification);
    return out;
}

static ::usp::Request to_native(const Request& in) {
    ::usp::Request out;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Get>) {
            *out.mutable_get() = to_native(value);
        } else if constexpr (std::is_same_v<T, Set>) {
            *out.mutable_set() = to_native(value);
        } else if constexpr (std::is_same_v<T, Add>) {
            *out.mutable_add() = to_native(value);
        } else if constexpr (std::is_same_v<T, Operate>) {
            *out.mutable_operate() = to_native(value);
        } else if constexpr (std::is_same_v<T, Notify>) {
            *out.mutable_notify() = to_native(value);
        }
    }, in.req_type);
    return out;
}

static Request from_native(const ::usp::Request& in) {
    Request out;
    switch (in.req_type_case()) {
    case ::usp::Request::kGet:
        out.req_type = from_native(in.get());
        break;
    case ::usp::Request::kSet:
        out.req_type = Set{};
        break;
    case ::usp::Request::kAdd:
        out.req_type = Add{};
        break;
    case ::usp::Request::kOperate:
        out.req_type = Operate{};
        break;
    case ::usp::Request::kNotify:
        out.req_type = from_native(in.notify());
        break;
    default:
        break;
    }
    return out;
}

static Response from_native(const ::usp::Response& in) {
    Response out;
    switch (in.resp_type_case()) {
    case ::usp::Response::kGetResp:
        out.resp_type = from_native(in.get_resp());
        break;
    case ::usp::Response::kSetResp:
        out.resp_type = from_native(in.set_resp());
        break;
    case ::usp::Response::kAddResp:
        out.resp_type = from_native(in.add_resp());
        break;
    case ::usp::Response::kOperateResp:
        out.resp_type = from_native(in.operate_resp());
        break;
    case ::usp::Response::kNotifyResp:
        out.resp_type = NotifyResp{in.notify_resp().subscription_id()};
        break;
    default:
        break;
    }
    return out;
}

static ::usp::Response to_native(const Response& in) {
    ::usp::Response out;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, GetResp>) {
            *out.mutable_get_resp() = to_native(value);
        } else if constexpr (std::is_same_v<T, NotifyResp>) {
            *out.mutable_notify_resp() = to_native(value);
        }
    }, in.resp_type);
    return out;
}

static ErrorBody from_native(const ::usp::Error& in) {
    ErrorBody out;
    out.err_code = in.err_code();
    out.err_msg = in.err_msg();
    for (const auto& err : in.param_errs()) {
        out.param_errs.push_back({err.param_path(), err.err_code(), err.err_msg()});
    }
    return out;
}

static ::usp::Body to_native(const Body& in) {
    ::usp::Body out;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Request>) {
            *out.mutable_request() = to_native(value);
        } else if constexpr (std::is_same_v<T, Response>) {
            *out.mutable_response() = to_native(value);
        }
    }, in.msg_body);
    return out;
}

static Body from_native(const ::usp::Body& in) {
    Body out;
    switch (in.msg_body_case()) {
    case ::usp::Body::kRequest:
        out.msg_body = from_native(in.request());
        break;
    case ::usp::Body::kResponse:
        out.msg_body = from_native(in.response());
        break;
    case ::usp::Body::kError:
        out.msg_body = from_native(in.error());
        break;
    default:
        break;
    }
    return out;
}

static Header from_native(const ::usp::Header& in) {
    return {in.msg_id(), from_native(in.msg_type())};
}

static ::usp::Msg to_native(const Msg& in) {
    ::usp::Msg out;
    if (in.header) {
        auto* header = out.mutable_header();
        header->set_msg_id(in.header->msg_id);
        header->set_msg_type(to_native(in.header->msg_type));
    }
    if (in.body) {
        *out.mutable_body() = to_native(*in.body);
    }
    return out;
}

static Msg from_native(const ::usp::Msg& in) {
    Msg out;
    if (in.has_header()) {
        out.header = from_native(in.header());
    }
    if (in.has_body()) {
        out.body = from_native(in.body());
    }
    return out;
}

static ::usp_record::Record to_native(const Record& in) {
    ::usp_record::Record out;
    out.set_version(in.version);
    out.set_to_id(in.to_id);
    out.set_from_id(in.from_id);
    out.set_originator_id(in.originator_id);
    out.set_destination_id(in.destination_id);
    out.set_payload_security(static_cast<::usp_record::Record_PayloadSecurity>(in.payload_security));

    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, NoSessionContextRecord>) {
            auto* nsc = out.mutable_no_session_context();
            nsc->set_payload(value.payload.data(), value.payload.size());
        } else if constexpr (std::is_same_v<T, UdsConnectRecord>) {
            out.mutable_uds_connect();
        }
    }, in.record_type);

    return out;
}

static Record from_native(const ::usp_record::Record& in) {
    Record out;
    out.version = in.version();
    out.to_id = in.to_id();
    out.from_id = in.from_id();
    out.originator_id = in.originator_id();
    out.destination_id = in.destination_id();
    out.payload_security = static_cast<int32_t>(in.payload_security());

    switch (in.record_type_case()) {
    case ::usp_record::Record::kNoSessionContext: {
        NoSessionContextRecord nsc;
        const auto& payload = in.no_session_context().payload();
        nsc.payload.assign(payload.begin(), payload.end());
        out.record_type = std::move(nsc);
        break;
    }
    case ::usp_record::Record::kUdsConnect:
        out.record_type = UdsConnectRecord{};
        break;
    default:
        break;
    }
    return out;
}

} // namespace

std::vector<uint8_t> NoSessionContextRecord::encode() const {
    ::usp_record::NoSessionContextRecord out;
    out.set_payload(payload.data(), payload.size());
    return serialize_message(out);
}

std::optional<NoSessionContextRecord> NoSessionContextRecord::decode(std::span<const uint8_t> data) {
    auto parsed = parse_message<::usp_record::NoSessionContextRecord>(data);
    if (!parsed) {
        return std::nullopt;
    }
    NoSessionContextRecord out;
    const auto& payload_bytes = parsed->payload();
    out.payload.assign(payload_bytes.begin(), payload_bytes.end());
    return out;
}

std::vector<uint8_t> UdsConnectRecord::encode() const {
    return serialize_message(::usp_record::UDSConnectRecord{});
}

std::vector<uint8_t> Record::encode() const {
    return serialize_message(to_native(*this));
}

std::optional<Record> Record::decode(std::span<const uint8_t> data) {
    auto parsed = parse_message<::usp_record::Record>(data);
    if (!parsed) {
        return std::nullopt;
    }
    return from_native(*parsed);
}

std::vector<uint8_t> Msg::encode() const {
    return serialize_message(to_native(*this));
}

std::optional<Msg> Msg::decode(std::span<const uint8_t> data) {
    auto parsed = parse_message<::usp::Msg>(data);
    if (!parsed) {
        return std::nullopt;
    }
    return from_native(*parsed);
}

} // namespace usp::proto
