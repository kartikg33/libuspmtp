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
 * usp_proto.hpp
 *
 * Thin C++20 value wrappers for the USP TR-369 protobuf schemas vendored in
 * proto/. Encoding and decoding are delegated to the official protobuf C++
 * runtime and generated classes; this file intentionally contains no wire
 * format implementation.
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

struct NoSessionContextRecord {
    std::vector<uint8_t> payload;

    std::vector<uint8_t> encode() const;
    static std::optional<NoSessionContextRecord> decode(std::span<const uint8_t> data);
};

struct UdsConnectRecord {
    std::vector<uint8_t> encode() const;
};

struct Record {
    std::string version;
    std::string to_id;
    std::string from_id;
    std::string originator_id;
    std::string destination_id;
    int32_t payload_security{0};
    std::variant<std::monostate, NoSessionContextRecord, UdsConnectRecord> record_type;

    std::vector<uint8_t> encode() const;
    static std::optional<Record> decode(std::span<const uint8_t> data);
};

enum class MsgType : int32_t {
    Error = 0,
    Get = 1,
    GetResp = 2,
    Notify = 3,
    Set = 4,
    SetResp = 5,
    Operate = 6,
    OperateResp = 7,
    Add = 8,
    AddResp = 9,
    Delete = 10,
    DeleteResp = 11,
    GetSupportedDm = 12,
    GetSupportedDmResp = 13,
    GetInstances = 14,
    GetInstancesResp = 15,
    NotifyResp = 16,
    GetSupportedProto = 17,
    GetSupportedProtoResp = 18,
    Register = 19,
    RegisterResp = 20,
    Deregister = 21,
    DeregisterResp = 22,
};

struct Header {
    std::string msg_id;
    MsgType msg_type{MsgType::Error};
};

struct ParamError {
    std::string param_path;
    uint32_t err_code{0};
    std::string err_msg;
};

struct ErrorBody {
    uint32_t err_code{0};
    std::string err_msg;
    std::vector<ParamError> param_errs;
};

struct Get {
    std::vector<std::string> param_paths;
    uint32_t max_depth{0};
};

struct ResolvedPathResult {
    std::string resolved_path;
    std::map<std::string, std::string> result_params;
};

struct RequestedPathResult {
    std::string requested_path;
    uint32_t err_code{0};
    std::string err_msg;
    std::vector<ResolvedPathResult> resolved_path_results;
};

struct GetResp {
    std::vector<RequestedPathResult> req_path_results;
};

struct UpdateParamSetting {
    std::string param;
    std::string value;
    bool required{false};
};

struct UpdateObject {
    std::string obj_path;
    std::vector<UpdateParamSetting> param_settings;
};

struct Set {
    bool allow_partial{false};
    std::vector<UpdateObject> update_objs;
};

struct OperationFailure {
    uint32_t err_code{0};
    std::string err_msg;
};

struct OperationSuccess {};

struct OperationStatus {
    std::variant<std::monostate, OperationFailure, OperationSuccess> oper_status;
};

struct UpdatedObjectResult {
    std::string requested_path;
    std::optional<OperationStatus> oper_status;
};

struct SetResp {
    std::vector<UpdatedObjectResult> updated_obj_results;
};

struct Operate {
    std::string command;
    std::string command_key;
    bool send_resp{false};
    std::map<std::string, std::string> input_args;
};

struct OutputArgs {
    std::map<std::string, std::string> output_args;
};

struct CommandFailure {
    uint32_t err_code{0};
    std::string err_msg;
};

struct OperationResult {
    std::string executed_command;
    std::variant<std::monostate, std::string, OutputArgs, CommandFailure> operation_resp;
};

struct OperateResp {
    std::vector<OperationResult> operation_results;
};

struct CreateParamSetting {
    std::string param;
    std::string value;
    bool required{false};
};

struct CreateObject {
    std::string obj_path;
    std::vector<CreateParamSetting> param_settings;
};

struct Add {
    bool allow_partial{false};
    std::vector<CreateObject> create_objs;
};

struct AddOperationFailure {
    uint32_t err_code{0};
    std::string err_msg;
};

struct AddOperationSuccess {
    std::string instantiated_path;
    std::map<std::string, std::string> unique_keys;
};

struct AddOperationStatus {
    std::variant<std::monostate, AddOperationFailure, AddOperationSuccess> oper_status;
};

struct CreatedObjectResult {
    std::string requested_path;
    std::optional<AddOperationStatus> oper_status;
};

struct AddResp {
    std::vector<CreatedObjectResult> created_obj_results;
};

struct Delete {
    bool allow_partial{false};
    std::vector<std::string> obj_paths;
};

struct DeleteOperationFailure {
    uint32_t err_code{0};
    std::string err_msg;
};

struct UnaffectedPathError {
    std::string path;
    uint32_t err_code{0};
    std::string err_msg;
};

struct DeleteOperationSuccess {
    std::vector<std::string> affected_paths;
    std::vector<UnaffectedPathError> unaffected_errors;
};

struct DeleteOperationStatus {
    std::variant<std::monostate, DeleteOperationFailure, DeleteOperationSuccess> oper_status;
};

struct DeletedObjectResult {
    std::string requested_path;
    std::optional<DeleteOperationStatus> oper_status;
};

struct DeleteResp {
    std::vector<DeletedObjectResult> deleted_obj_results;
};

struct Register {
    bool allow_partial{false};
    std::vector<std::string> reg_paths;
};

struct RegisterOperationFailure {
    uint32_t err_code{0};
    std::string err_msg;
};

struct RegisterOperationSuccess {
    std::string registered_path;
};

struct RegisterOperationStatus {
    std::variant<std::monostate, RegisterOperationFailure, RegisterOperationSuccess> oper_status;
};

struct RegisteredPathResult {
    std::string requested_path;
    std::optional<RegisterOperationStatus> oper_status;
};

struct RegisterResp {
    std::vector<RegisteredPathResult> registered_path_results;
};

struct GetSupportedDM {
    std::vector<std::string> obj_paths;
    bool first_level_only{false};
    bool return_commands{true};
    bool return_events{true};
    bool return_params{true};
    bool return_unique_key_sets{true};
};

struct SupportedParamInfo {
    std::string name;
    int access{0};       ///< ParamAccessType numeric value (0=read-only, 1=read-write, 2=write-only)
    int value_type{0};   ///< ParamValueType numeric value (0=unknown, 8=string, ...)
    int value_change{0}; ///< ValueChangeType numeric value (0=unknown, 1=allowed, 2=will-ignore)
};

struct SupportedCommandInfo {
    std::string name;
    std::vector<std::string> input_args;
    std::vector<std::string> output_args;
    int command_type{0}; ///< CmdType numeric value (0=unknown, 1=sync, 2=async)
};

struct SupportedEventInfo {
    std::string name;
    std::vector<std::string> arg_names;
};

struct SupportedObjectInfo {
    std::string path;
    int access{0}; ///< ObjAccessType numeric value (0=read-only, 1=add-delete, 2=add-only, 3=delete-only)
    bool multi_instance{false};
    std::vector<SupportedCommandInfo> commands;
    std::vector<SupportedEventInfo> events;
    std::vector<SupportedParamInfo> params;
    std::vector<std::string> divergent_paths;
    std::vector<std::vector<std::string>> unique_key_sets;
};

struct SupportedDMResult {
    std::string requested_path;
    uint32_t err_code{0};
    std::string err_msg;
    std::string data_model_uri;
    std::vector<SupportedObjectInfo> objects;
};

struct GetSupportedDMResp {
    std::vector<SupportedDMResult> results;
};

struct GetInstances {
    std::vector<std::string> obj_paths;
    bool first_level_only{false};
};

struct InstanceInfo {
    std::string path;
    std::map<std::string, std::string> unique_keys;
};

struct InstancesResult {
    std::string requested_path;
    uint32_t err_code{0};
    std::string err_msg;
    std::vector<InstanceInfo> instances;
};

struct GetInstancesResp {
    std::vector<InstancesResult> results;
};

struct GetSupportedProtocol {
    std::string controller_versions;
};

struct GetSupportedProtocolResp {
    std::string agent_versions;
};

struct ValueChange {
    std::string param_path;
    std::string param_value;
};

struct ObjectCreation {
    std::string obj_path;
    std::map<std::string, std::string> unique_keys;
};

struct ObjectDeletion {
    std::string obj_path;
};

struct Notify {
    std::string subscription_id;
    bool send_resp{false};
    std::variant<std::monostate, ValueChange, ObjectCreation, ObjectDeletion> notification;
};

struct NotifyResp {
    std::string subscription_id;
};

using RequestVariant = std::variant<std::monostate, Get, Set, Add, Operate, Notify,
    Delete, Register, GetSupportedDM, GetInstances, GetSupportedProtocol>;

struct Request {
    RequestVariant req_type;
};

using ResponseVariant = std::variant<std::monostate, GetResp, SetResp, AddResp, OperateResp, NotifyResp,
    DeleteResp, RegisterResp, GetSupportedDMResp, GetInstancesResp, GetSupportedProtocolResp>;

struct Response {
    ResponseVariant resp_type;
};

using BodyVariant = std::variant<std::monostate, Request, Response, ErrorBody>;

struct Body {
    BodyVariant msg_body;
};

struct Msg {
    std::optional<Header> header;
    std::optional<Body> body;

    std::vector<uint8_t> encode() const;
    static std::optional<Msg> decode(std::span<const uint8_t> data);
};

} // namespace usp::proto
