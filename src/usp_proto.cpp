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
 * usp_proto.cpp
 *
 * Implementations of PbWriter, PbReader, and all USP TR-369 message
 * encode/decode methods declared in usp_proto.hpp.
 */

#include "usp_proto.hpp"

#include <cstring>
#include <stdexcept>

namespace usp::proto {

/* ═══════════════════════════════════════════════════════════════════════════
 * PbWriter
 * ═══════════════════════════════════════════════════════════════════════════ */

void PbWriter::write_tag(uint32_t field_num, uint32_t wire_type) {
    write_varint((static_cast<uint64_t>(field_num) << 3) | wire_type);
}

void PbWriter::write_varint(uint64_t v) {
    while (v > 0x7Fu) {
        buf_.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
        v >>= 7;
    }
    buf_.push_back(static_cast<uint8_t>(v));
}

void PbWriter::write_fixed32_raw(uint32_t v) {
    buf_.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
    buf_.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
}

void PbWriter::write_varint_field(uint32_t field_num, uint64_t v) {
    if (v == 0) return;
    write_tag(field_num, 0);
    write_varint(v);
}

void PbWriter::write_bool_field(uint32_t field_num, bool v) {
    if (!v) return;
    write_tag(field_num, 0);
    write_varint(1u);
}

void PbWriter::write_int32_field(uint32_t field_num, int32_t v) {
    if (v == 0) return;
    write_tag(field_num, 0);
    /* Sign-extend negative values to 64 bits before varint encoding. */
    write_varint(static_cast<uint64_t>(static_cast<int64_t>(v)));
}

void PbWriter::write_fixed32_field(uint32_t field_num, uint32_t v) {
    if (v == 0) return;
    write_tag(field_num, 5);
    write_fixed32_raw(v);
}

void PbWriter::write_string_field(uint32_t field_num, const std::string& s) {
    if (s.empty()) return;
    write_tag(field_num, 2);
    write_varint(s.size());
    buf_.insert(buf_.end(), s.begin(), s.end());
}

void PbWriter::write_bytes_field(uint32_t field_num, const std::vector<uint8_t>& b) {
    if (b.empty()) return;
    write_tag(field_num, 2);
    write_varint(b.size());
    buf_.insert(buf_.end(), b.begin(), b.end());
}

void PbWriter::write_message_field(uint32_t field_num, const std::vector<uint8_t>& msg) {
    write_tag(field_num, 2);
    write_varint(msg.size());
    buf_.insert(buf_.end(), msg.begin(), msg.end());
}

void PbWriter::write_map_entry(uint32_t field_num,
                               const std::string& key,
                               const std::string& value) {
    PbWriter entry;
    entry.write_string_field(1, key);
    entry.write_string_field(2, value);
    write_message_field(field_num, entry.bytes());
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PbReader
 * ═══════════════════════════════════════════════════════════════════════════ */

bool PbReader::read_varint(uint64_t& v) {
    v = 0;
    int shift = 0;
    while (pos_ < data_.size()) {
        uint8_t b = data_[pos_++];
        v |= static_cast<uint64_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

bool PbReader::read_tag(uint32_t& field_num, uint32_t& wire_type) {
    uint64_t tag;
    if (!read_varint(tag)) return false;
    field_num = static_cast<uint32_t>(tag >> 3);
    wire_type = static_cast<uint32_t>(tag & 0x7u);
    return field_num != 0;
}

bool PbReader::skip_field(uint32_t wire_type) {
    switch (wire_type) {
    case 0: { /* varint */
        uint64_t v;
        return read_varint(v);
    }
    case 1: { /* 64-bit */
        if (pos_ + 8 > data_.size()) return false;
        pos_ += 8;
        return true;
    }
    case 2: { /* length-delimited */
        uint64_t len;
        if (!read_varint(len)) return false;
        if (pos_ + len > data_.size()) return false;
        pos_ += static_cast<size_t>(len);
        return true;
    }
    case 5: { /* 32-bit */
        if (pos_ + 4 > data_.size()) return false;
        pos_ += 4;
        return true;
    }
    default:
        return false;
    }
}

bool PbReader::read_fixed32(uint32_t& v) {
    if (pos_ + 4 > data_.size()) return false;
    v = static_cast<uint32_t>(data_[pos_])
      | (static_cast<uint32_t>(data_[pos_ + 1]) << 8u)
      | (static_cast<uint32_t>(data_[pos_ + 2]) << 16u)
      | (static_cast<uint32_t>(data_[pos_ + 3]) << 24u);
    pos_ += 4;
    return true;
}

bool PbReader::read_bytes(std::vector<uint8_t>& out) {
    uint64_t len;
    if (!read_varint(len)) return false;
    if (pos_ + len > data_.size()) return false;
    out.assign(data_.data() + pos_, data_.data() + pos_ + len);
    pos_ += static_cast<size_t>(len);
    return true;
}

bool PbReader::read_string(std::string& out) {
    uint64_t len;
    if (!read_varint(len)) return false;
    if (pos_ + len > data_.size()) return false;
    out.assign(reinterpret_cast<const char*>(data_.data() + pos_), static_cast<size_t>(len));
    pos_ += static_cast<size_t>(len);
    return true;
}

bool PbReader::read_sub_reader(PbReader& sub) {
    uint64_t len;
    if (!read_varint(len)) return false;
    if (pos_ + len > data_.size()) return false;
    sub = PbReader(data_.subspan(pos_, static_cast<size_t>(len)));
    pos_ += static_cast<size_t>(len);
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NoSessionContextRecord
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> NoSessionContextRecord::encode() const {
    PbWriter w;
    w.write_bytes_field(2, payload);
    return w.take();
}

std::optional<NoSessionContextRecord> NoSessionContextRecord::decode(PbReader& r) {
    NoSessionContextRecord rec;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 2:
            if (!r.read_bytes(rec.payload)) return std::nullopt;
            break;
        default:
            if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return rec;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Record
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> Record::encode() const {
    PbWriter w;
    w.write_string_field(1, version);
    w.write_string_field(2, to_id);
    w.write_string_field(3, from_id);
    w.write_int32_field(4, payload_security);

    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NoSessionContextRecord>) {
            w.write_message_field(7, v.encode());
        } else if constexpr (std::is_same_v<T, UdsConnectRecord>) {
            w.write_message_field(12, v.encode());
        }
        /* monostate: no field written */
    }, record_type);

    return w.take();
}

std::optional<Record> Record::decode(std::span<const uint8_t> data) {
    PbReader r(data);
    Record rec;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1:  if (!r.read_string(rec.version))   return std::nullopt; break;
        case 2:  if (!r.read_string(rec.to_id))     return std::nullopt; break;
        case 3:  if (!r.read_string(rec.from_id))   return std::nullopt; break;
        case 4: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            rec.payload_security = static_cast<int32_t>(v);
            break;
        }
        case 7: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto v = NoSessionContextRecord::decode(sub);
            if (!v) return std::nullopt;
            rec.record_type = std::move(*v);
            break;
        }
        case 12: {
            if (!r.skip_field(wire_type)) return std::nullopt;
            rec.record_type = UdsConnectRecord{};
            break;
        }
        default:
            if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return rec;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Header
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> Header::encode() const {
    PbWriter w;
    w.write_string_field(1, msg_id);
    w.write_int32_field(2, static_cast<int32_t>(msg_type));
    return w.take();
}

std::optional<Header> Header::decode(PbReader& r) {
    Header h;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(h.msg_id)) return std::nullopt; break;
        case 2: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            h.msg_type = static_cast<MsgType>(static_cast<int32_t>(v));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return h;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ErrorBody / ParamError
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> ParamError::encode() const {
    PbWriter w;
    w.write_string_field(1, param_path);
    w.write_fixed32_field(2, err_code);
    w.write_string_field(3, err_msg);
    return w.take();
}

std::optional<ParamError> ParamError::decode(PbReader& r) {
    ParamError pe;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(pe.param_path)) return std::nullopt; break;
        case 2: if (!r.read_fixed32(pe.err_code))  return std::nullopt; break;
        case 3: if (!r.read_string(pe.err_msg))    return std::nullopt; break;
        default: if (!r.skip_field(wire_type))     return std::nullopt;
        }
    }
    return pe;
}

std::vector<uint8_t> ErrorBody::encode() const {
    PbWriter w;
    w.write_fixed32_field(1, err_code);
    w.write_string_field(2, err_msg);
    for (auto& pe : param_errs)
        w.write_message_field(3, pe.encode());
    return w.take();
}

std::optional<ErrorBody> ErrorBody::decode(PbReader& r) {
    ErrorBody eb;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_fixed32(eb.err_code)) return std::nullopt; break;
        case 2: if (!r.read_string(eb.err_msg))   return std::nullopt; break;
        case 3: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto pe = ParamError::decode(sub);
            if (!pe) return std::nullopt;
            eb.param_errs.push_back(std::move(*pe));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return eb;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Get / GetResp
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> Get::encode() const {
    PbWriter w;
    for (auto& p : param_paths)
        w.write_string_field(1, p);
    w.write_fixed32_field(2, max_depth);
    return w.take();
}

std::optional<Get> Get::decode(PbReader& r) {
    Get g;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            std::string s;
            if (!r.read_string(s)) return std::nullopt;
            g.param_paths.push_back(std::move(s));
            break;
        }
        case 2: if (!r.read_fixed32(g.max_depth)) return std::nullopt; break;
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return g;
}

std::vector<uint8_t> ResolvedPathResult::encode() const {
    PbWriter w;
    w.write_string_field(1, resolved_path);
    for (auto& [k, v] : result_params)
        w.write_map_entry(2, k, v);
    return w.take();
}

std::optional<ResolvedPathResult> ResolvedPathResult::decode(PbReader& r) {
    ResolvedPathResult rpr;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(rpr.resolved_path)) return std::nullopt; break;
        case 2: {
            /* Map entry: embedded message with key=1 (str) value=2 (str). */
            PbReader entry(std::span<const uint8_t>{});
            if (!r.read_sub_reader(entry)) return std::nullopt;
            std::string key, val;
            uint32_t ef, ew;
            while (entry.read_tag(ef, ew)) {
                switch (ef) {
                case 1: if (!entry.read_string(key)) return std::nullopt; break;
                case 2: if (!entry.read_string(val)) return std::nullopt; break;
                default: if (!entry.skip_field(ew))  return std::nullopt;
                }
            }
            rpr.result_params.emplace(std::move(key), std::move(val));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return rpr;
}

std::vector<uint8_t> RequestedPathResult::encode() const {
    PbWriter w;
    w.write_string_field(1, requested_path);
    w.write_fixed32_field(2, err_code);
    w.write_string_field(3, err_msg);
    for (auto& rpr : resolved_path_results)
        w.write_message_field(4, rpr.encode());
    return w.take();
}

std::optional<RequestedPathResult> RequestedPathResult::decode(PbReader& r) {
    RequestedPathResult rr;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(rr.requested_path)) return std::nullopt; break;
        case 2: if (!r.read_fixed32(rr.err_code))      return std::nullopt; break;
        case 3: if (!r.read_string(rr.err_msg))        return std::nullopt; break;
        case 4: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto rpr = ResolvedPathResult::decode(sub);
            if (!rpr) return std::nullopt;
            rr.resolved_path_results.push_back(std::move(*rpr));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return rr;
}

std::vector<uint8_t> GetResp::encode() const {
    PbWriter w;
    for (auto& rr : req_path_results)
        w.write_message_field(1, rr.encode());
    return w.take();
}

std::optional<GetResp> GetResp::decode(PbReader& r) {
    GetResp gr;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto rr = RequestedPathResult::decode(sub);
            if (!rr) return std::nullopt;
            gr.req_path_results.push_back(std::move(*rr));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return gr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Set / SetResp
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> UpdateParamSetting::encode() const {
    PbWriter w;
    w.write_string_field(1, param);
    w.write_string_field(2, value);
    w.write_bool_field(3, required);
    return w.take();
}

std::optional<UpdateParamSetting> UpdateParamSetting::decode(PbReader& r) {
    UpdateParamSetting s;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(s.param)) return std::nullopt; break;
        case 2: if (!r.read_string(s.value)) return std::nullopt; break;
        case 3: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            s.required = (v != 0);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return s;
}

std::vector<uint8_t> UpdateObject::encode() const {
    PbWriter w;
    w.write_string_field(1, obj_path);
    for (auto& s : param_settings)
        w.write_message_field(2, s.encode());
    return w.take();
}

std::optional<UpdateObject> UpdateObject::decode(PbReader& r) {
    UpdateObject o;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(o.obj_path)) return std::nullopt; break;
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto s = UpdateParamSetting::decode(sub);
            if (!s) return std::nullopt;
            o.param_settings.push_back(std::move(*s));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return o;
}

std::vector<uint8_t> Set::encode() const {
    PbWriter w;
    w.write_bool_field(1, allow_partial);
    for (auto& o : update_objs)
        w.write_message_field(2, o.encode());
    return w.take();
}

std::optional<Set> Set::decode(PbReader& r) {
    Set s;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            s.allow_partial = (v != 0);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto o = UpdateObject::decode(sub);
            if (!o) return std::nullopt;
            s.update_objs.push_back(std::move(*o));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return s;
}

std::vector<uint8_t> OperationFailure::encode() const {
    PbWriter w;
    w.write_fixed32_field(1, err_code);
    w.write_string_field(2, err_msg);
    return w.take();
}

std::optional<OperationFailure> OperationFailure::decode(PbReader& r) {
    OperationFailure f;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_fixed32(f.err_code)) return std::nullopt; break;
        case 2: if (!r.read_string(f.err_msg))   return std::nullopt; break;
        default: if (!r.skip_field(wire_type))   return std::nullopt;
        }
    }
    return f;
}

std::vector<uint8_t> OperationSuccess::encode() const {
    return {};
}

std::optional<OperationSuccess> OperationSuccess::decode(PbReader& r) {
    /* Skip any unknown fields. */
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        if (!r.skip_field(wire_type)) return std::nullopt;
    }
    return OperationSuccess{};
}

std::vector<uint8_t> OperationStatus::encode() const {
    PbWriter w;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OperationFailure>) {
            w.write_message_field(1, v.encode());
        } else if constexpr (std::is_same_v<T, OperationSuccess>) {
            w.write_message_field(2, v.encode());
        }
    }, oper_status);
    return w.take();
}

std::optional<OperationStatus> OperationStatus::decode(PbReader& r) {
    OperationStatus os;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto f = OperationFailure::decode(sub);
            if (!f) return std::nullopt;
            os.oper_status = std::move(*f);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto s = OperationSuccess::decode(sub);
            if (!s) return std::nullopt;
            os.oper_status = std::move(*s);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return os;
}

std::vector<uint8_t> UpdatedObjectResult::encode() const {
    PbWriter w;
    w.write_string_field(1, requested_path);
    if (oper_status) w.write_message_field(2, oper_status->encode());
    return w.take();
}

std::optional<UpdatedObjectResult> UpdatedObjectResult::decode(PbReader& r) {
    UpdatedObjectResult uor;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(uor.requested_path)) return std::nullopt; break;
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto os = OperationStatus::decode(sub);
            if (!os) return std::nullopt;
            uor.oper_status = std::move(*os);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return uor;
}

std::vector<uint8_t> SetResp::encode() const {
    PbWriter w;
    for (auto& r : updated_obj_results)
        w.write_message_field(1, r.encode());
    return w.take();
}

std::optional<SetResp> SetResp::decode(PbReader& r) {
    SetResp sr;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto uor = UpdatedObjectResult::decode(sub);
            if (!uor) return std::nullopt;
            sr.updated_obj_results.push_back(std::move(*uor));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return sr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Operate / OperateResp
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> Operate::encode() const {
    PbWriter w;
    w.write_string_field(1, command);
    w.write_string_field(2, command_key);
    w.write_bool_field(3, send_resp);
    for (auto& [k, v] : input_args)
        w.write_map_entry(4, k, v);
    return w.take();
}

std::optional<Operate> Operate::decode(PbReader& r) {
    Operate op;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(op.command))     return std::nullopt; break;
        case 2: if (!r.read_string(op.command_key)) return std::nullopt; break;
        case 3: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            op.send_resp = (v != 0);
            break;
        }
        case 4: {
            PbReader entry(std::span<const uint8_t>{});
            if (!r.read_sub_reader(entry)) return std::nullopt;
            std::string k, v;
            uint32_t ef, ew;
            while (entry.read_tag(ef, ew)) {
                switch (ef) {
                case 1: if (!entry.read_string(k)) return std::nullopt; break;
                case 2: if (!entry.read_string(v)) return std::nullopt; break;
                default: if (!entry.skip_field(ew)) return std::nullopt;
                }
            }
            op.input_args.emplace(std::move(k), std::move(v));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return op;
}

std::vector<uint8_t> OutputArgs::encode() const {
    PbWriter w;
    for (auto& [k, v] : output_args)
        w.write_map_entry(1, k, v);
    return w.take();
}

std::optional<OutputArgs> OutputArgs::decode(PbReader& r) {
    OutputArgs oa;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader entry(std::span<const uint8_t>{});
            if (!r.read_sub_reader(entry)) return std::nullopt;
            std::string k, v;
            uint32_t ef, ew;
            while (entry.read_tag(ef, ew)) {
                switch (ef) {
                case 1: if (!entry.read_string(k)) return std::nullopt; break;
                case 2: if (!entry.read_string(v)) return std::nullopt; break;
                default: if (!entry.skip_field(ew)) return std::nullopt;
                }
            }
            oa.output_args.emplace(std::move(k), std::move(v));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return oa;
}

std::vector<uint8_t> CommandFailure::encode() const {
    PbWriter w;
    w.write_fixed32_field(1, err_code);
    w.write_string_field(2, err_msg);
    return w.take();
}

std::optional<CommandFailure> CommandFailure::decode(PbReader& r) {
    CommandFailure cf;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_fixed32(cf.err_code)) return std::nullopt; break;
        case 2: if (!r.read_string(cf.err_msg))   return std::nullopt; break;
        default: if (!r.skip_field(wire_type))    return std::nullopt;
        }
    }
    return cf;
}

std::vector<uint8_t> OperationResult::encode() const {
    PbWriter w;
    w.write_string_field(1, executed_command);
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            w.write_string_field(2, v);
        } else if constexpr (std::is_same_v<T, OutputArgs>) {
            w.write_message_field(3, v.encode());
        } else if constexpr (std::is_same_v<T, CommandFailure>) {
            w.write_message_field(4, v.encode());
        }
    }, operation_resp);
    return w.take();
}

std::optional<OperationResult> OperationResult::decode(PbReader& r) {
    OperationResult or_;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(or_.executed_command)) return std::nullopt; break;
        case 2: {
            std::string s;
            if (!r.read_string(s)) return std::nullopt;
            or_.operation_resp = std::move(s);
            break;
        }
        case 3: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto oa = OutputArgs::decode(sub);
            if (!oa) return std::nullopt;
            or_.operation_resp = std::move(*oa);
            break;
        }
        case 4: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto cf = CommandFailure::decode(sub);
            if (!cf) return std::nullopt;
            or_.operation_resp = std::move(*cf);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return or_;
}

std::vector<uint8_t> OperateResp::encode() const {
    PbWriter w;
    for (auto& r : operation_results)
        w.write_message_field(1, r.encode());
    return w.take();
}

std::optional<OperateResp> OperateResp::decode(PbReader& r) {
    OperateResp or_;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto op = OperationResult::decode(sub);
            if (!op) return std::nullopt;
            or_.operation_results.push_back(std::move(*op));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return or_;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Add / AddResp
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> CreateParamSetting::encode() const {
    PbWriter w;
    w.write_string_field(1, param);
    w.write_string_field(2, value);
    w.write_bool_field(3, required);
    return w.take();
}

std::optional<CreateParamSetting> CreateParamSetting::decode(PbReader& r) {
    CreateParamSetting s;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(s.param)) return std::nullopt; break;
        case 2: if (!r.read_string(s.value)) return std::nullopt; break;
        case 3: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            s.required = (v != 0);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return s;
}

std::vector<uint8_t> CreateObject::encode() const {
    PbWriter w;
    w.write_string_field(1, obj_path);
    for (auto& s : param_settings)
        w.write_message_field(2, s.encode());
    return w.take();
}

std::optional<CreateObject> CreateObject::decode(PbReader& r) {
    CreateObject o;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(o.obj_path)) return std::nullopt; break;
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto s = CreateParamSetting::decode(sub);
            if (!s) return std::nullopt;
            o.param_settings.push_back(std::move(*s));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return o;
}

std::vector<uint8_t> Add::encode() const {
    PbWriter w;
    w.write_bool_field(1, allow_partial);
    for (auto& o : create_objs)
        w.write_message_field(2, o.encode());
    return w.take();
}

std::optional<Add> Add::decode(PbReader& r) {
    Add a;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            a.allow_partial = (v != 0);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto o = CreateObject::decode(sub);
            if (!o) return std::nullopt;
            a.create_objs.push_back(std::move(*o));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return a;
}

std::vector<uint8_t> AddOperationFailure::encode() const {
    PbWriter w;
    w.write_fixed32_field(1, err_code);
    w.write_string_field(2, err_msg);
    return w.take();
}

std::optional<AddOperationFailure> AddOperationFailure::decode(PbReader& r) {
    AddOperationFailure f;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_fixed32(f.err_code)) return std::nullopt; break;
        case 2: if (!r.read_string(f.err_msg))   return std::nullopt; break;
        default: if (!r.skip_field(wire_type))   return std::nullopt;
        }
    }
    return f;
}

std::vector<uint8_t> AddOperationSuccess::encode() const {
    PbWriter w;
    w.write_string_field(1, instantiated_path);
    for (auto& [k, v] : unique_keys)
        w.write_map_entry(3, k, v);
    return w.take();
}

std::optional<AddOperationSuccess> AddOperationSuccess::decode(PbReader& r) {
    AddOperationSuccess s;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(s.instantiated_path)) return std::nullopt; break;
        case 3: {
            PbReader entry(std::span<const uint8_t>{});
            if (!r.read_sub_reader(entry)) return std::nullopt;
            std::string k, v;
            uint32_t ef, ew;
            while (entry.read_tag(ef, ew)) {
                switch (ef) {
                case 1: if (!entry.read_string(k)) return std::nullopt; break;
                case 2: if (!entry.read_string(v)) return std::nullopt; break;
                default: if (!entry.skip_field(ew)) return std::nullopt;
                }
            }
            s.unique_keys.emplace(std::move(k), std::move(v));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return s;
}

std::vector<uint8_t> AddOperationStatus::encode() const {
    PbWriter w;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, AddOperationFailure>) {
            w.write_message_field(1, v.encode());
        } else if constexpr (std::is_same_v<T, AddOperationSuccess>) {
            w.write_message_field(2, v.encode());
        }
    }, oper_status);
    return w.take();
}

std::optional<AddOperationStatus> AddOperationStatus::decode(PbReader& r) {
    AddOperationStatus aos;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto f = AddOperationFailure::decode(sub);
            if (!f) return std::nullopt;
            aos.oper_status = std::move(*f);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto s = AddOperationSuccess::decode(sub);
            if (!s) return std::nullopt;
            aos.oper_status = std::move(*s);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return aos;
}

std::vector<uint8_t> CreatedObjectResult::encode() const {
    PbWriter w;
    w.write_string_field(1, requested_path);
    if (oper_status) w.write_message_field(2, oper_status->encode());
    return w.take();
}

std::optional<CreatedObjectResult> CreatedObjectResult::decode(PbReader& r) {
    CreatedObjectResult cor;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(cor.requested_path)) return std::nullopt; break;
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto aos = AddOperationStatus::decode(sub);
            if (!aos) return std::nullopt;
            cor.oper_status = std::move(*aos);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return cor;
}

std::vector<uint8_t> AddResp::encode() const {
    PbWriter w;
    for (auto& r : created_obj_results)
        w.write_message_field(1, r.encode());
    return w.take();
}

std::optional<AddResp> AddResp::decode(PbReader& r) {
    AddResp ar;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto cor = CreatedObjectResult::decode(sub);
            if (!cor) return std::nullopt;
            ar.created_obj_results.push_back(std::move(*cor));
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return ar;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Notify / NotifyResp
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> ValueChange::encode() const {
    PbWriter w;
    w.write_string_field(1, param_path);
    w.write_string_field(2, param_value);
    return w.take();
}

std::optional<ValueChange> ValueChange::decode(PbReader& r) {
    ValueChange vc;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(vc.param_path))  return std::nullopt; break;
        case 2: if (!r.read_string(vc.param_value)) return std::nullopt; break;
        default: if (!r.skip_field(wire_type))      return std::nullopt;
        }
    }
    return vc;
}

std::vector<uint8_t> ObjectCreation::encode() const {
    PbWriter w;
    w.write_string_field(1, obj_path);
    return w.take();
}

std::optional<ObjectCreation> ObjectCreation::decode(PbReader& r) {
    ObjectCreation oc;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(oc.obj_path)) return std::nullopt; break;
        default: if (!r.skip_field(wire_type))   return std::nullopt;
        }
    }
    return oc;
}

std::vector<uint8_t> ObjectDeletion::encode() const {
    PbWriter w;
    w.write_string_field(1, obj_path);
    return w.take();
}

std::optional<ObjectDeletion> ObjectDeletion::decode(PbReader& r) {
    ObjectDeletion od;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(od.obj_path)) return std::nullopt; break;
        default: if (!r.skip_field(wire_type))   return std::nullopt;
        }
    }
    return od;
}

std::vector<uint8_t> Notify::encode() const {
    PbWriter w;
    w.write_string_field(1, subscription_id);
    w.write_bool_field(2, send_resp);
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ValueChange>) {
            w.write_message_field(3, v.encode());
        } else if constexpr (std::is_same_v<T, ObjectCreation>) {
            w.write_message_field(4, v.encode());
        } else if constexpr (std::is_same_v<T, ObjectDeletion>) {
            w.write_message_field(5, v.encode());
        }
    }, notification);
    return w.take();
}

std::optional<Notify> Notify::decode(PbReader& r) {
    Notify n;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(n.subscription_id)) return std::nullopt; break;
        case 2: {
            uint64_t v;
            if (!r.read_varint(v)) return std::nullopt;
            n.send_resp = (v != 0);
            break;
        }
        case 3: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto vc = ValueChange::decode(sub);
            if (!vc) return std::nullopt;
            n.notification = std::move(*vc);
            break;
        }
        case 4: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto oc = ObjectCreation::decode(sub);
            if (!oc) return std::nullopt;
            n.notification = std::move(*oc);
            break;
        }
        case 5: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto od = ObjectDeletion::decode(sub);
            if (!od) return std::nullopt;
            n.notification = std::move(*od);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return n;
}

std::vector<uint8_t> NotifyResp::encode() const {
    PbWriter w;
    w.write_string_field(1, subscription_id);
    return w.take();
}

std::optional<NotifyResp> NotifyResp::decode(PbReader& r) {
    NotifyResp nr;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: if (!r.read_string(nr.subscription_id)) return std::nullopt; break;
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return nr;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Request / Response / Body / Msg
 * ═══════════════════════════════════════════════════════════════════════════ */

std::vector<uint8_t> Request::encode() const {
    PbWriter w;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Get>) {
            w.write_message_field(1, v.encode());
        } else if constexpr (std::is_same_v<T, Set>) {
            w.write_message_field(4, v.encode());
        } else if constexpr (std::is_same_v<T, Add>) {
            w.write_message_field(5, v.encode());
        } else if constexpr (std::is_same_v<T, Operate>) {
            w.write_message_field(7, v.encode());
        } else if constexpr (std::is_same_v<T, Notify>) {
            w.write_message_field(8, v.encode());
        }
    }, req_type);
    return w.take();
}

std::optional<Request> Request::decode(PbReader& r) {
    Request req;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto g = Get::decode(sub);
            if (!g) return std::nullopt;
            req.req_type = std::move(*g);
            break;
        }
        case 4: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto s = Set::decode(sub);
            if (!s) return std::nullopt;
            req.req_type = std::move(*s);
            break;
        }
        case 5: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto a = Add::decode(sub);
            if (!a) return std::nullopt;
            req.req_type = std::move(*a);
            break;
        }
        case 7: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto op = Operate::decode(sub);
            if (!op) return std::nullopt;
            req.req_type = std::move(*op);
            break;
        }
        case 8: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto n = Notify::decode(sub);
            if (!n) return std::nullopt;
            req.req_type = std::move(*n);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return req;
}

std::vector<uint8_t> Response::encode() const {
    PbWriter w;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, GetResp>) {
            w.write_message_field(1, v.encode());
        } else if constexpr (std::is_same_v<T, SetResp>) {
            w.write_message_field(4, v.encode());
        } else if constexpr (std::is_same_v<T, AddResp>) {
            w.write_message_field(5, v.encode());
        } else if constexpr (std::is_same_v<T, OperateResp>) {
            w.write_message_field(7, v.encode());
        } else if constexpr (std::is_same_v<T, NotifyResp>) {
            w.write_message_field(8, v.encode());
        }
    }, resp_type);
    return w.take();
}

std::optional<Response> Response::decode(PbReader& r) {
    Response resp;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto gr = GetResp::decode(sub);
            if (!gr) return std::nullopt;
            resp.resp_type = std::move(*gr);
            break;
        }
        case 4: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto sr = SetResp::decode(sub);
            if (!sr) return std::nullopt;
            resp.resp_type = std::move(*sr);
            break;
        }
        case 5: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto ar = AddResp::decode(sub);
            if (!ar) return std::nullopt;
            resp.resp_type = std::move(*ar);
            break;
        }
        case 7: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto or_ = OperateResp::decode(sub);
            if (!or_) return std::nullopt;
            resp.resp_type = std::move(*or_);
            break;
        }
        case 8: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto nr = NotifyResp::decode(sub);
            if (!nr) return std::nullopt;
            resp.resp_type = std::move(*nr);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return resp;
}

std::vector<uint8_t> Body::encode() const {
    PbWriter w;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Request>) {
            w.write_message_field(1, v.encode());
        } else if constexpr (std::is_same_v<T, Response>) {
            w.write_message_field(2, v.encode());
        } else if constexpr (std::is_same_v<T, ErrorBody>) {
            w.write_message_field(3, v.encode());
        }
    }, msg_body);
    return w.take();
}

std::optional<Body> Body::decode(PbReader& r) {
    Body body;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto req = Request::decode(sub);
            if (!req) return std::nullopt;
            body.msg_body = std::move(*req);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto resp = Response::decode(sub);
            if (!resp) return std::nullopt;
            body.msg_body = std::move(*resp);
            break;
        }
        case 3: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto eb = ErrorBody::decode(sub);
            if (!eb) return std::nullopt;
            body.msg_body = std::move(*eb);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return body;
}

std::vector<uint8_t> Msg::encode() const {
    PbWriter w;
    if (header) w.write_message_field(1, header->encode());
    if (body)   w.write_message_field(2, body->encode());
    return w.take();
}

std::optional<Msg> Msg::decode(std::span<const uint8_t> data) {
    PbReader r(data);
    Msg msg;
    uint32_t field_num, wire_type;
    while (r.read_tag(field_num, wire_type)) {
        switch (field_num) {
        case 1: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto h = Header::decode(sub);
            if (!h) return std::nullopt;
            msg.header = std::move(*h);
            break;
        }
        case 2: {
            PbReader sub(std::span<const uint8_t>{});
            if (!r.read_sub_reader(sub)) return std::nullopt;
            auto b = Body::decode(sub);
            if (!b) return std::nullopt;
            msg.body = std::move(*b);
            break;
        }
        default: if (!r.skip_field(wire_type)) return std::nullopt;
        }
    }
    return msg;
}

} // namespace usp::proto
