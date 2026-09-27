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
 * client.cpp
 *
 * UspController implementation.
 *
 * Session worker design:
 *
 *   The worker thread loops indefinitely:
 *
 *     1. Non-blocking check for a pending command.
 *     2. If a command is present:
 *          – Execute it (send record, wait for non-Notify response with timeout).
 *          – Fulfill the caller's promise.
 *     3. Otherwise: call transport.recv() (1-second socket timeout).
 *          – Timeout → continue loop (check stop token).
 *          – Notify record → send NotifyResp (if requested), fire all callbacks.
 *          – Other record → log; continue.
 *          – Hard I/O error → stop worker.
 */

#include "client.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace usp {

/* ── Global message counter ─────────────────────────────────────────────── */

static std::atomic<uint64_t> g_msg_counter{1};

static std::string next_msg_id() {
    return std::to_string(g_msg_counter.fetch_add(1, std::memory_order_relaxed));
}

/* ── Protocol version ───────────────────────────────────────────────────── */

static constexpr std::chrono::seconds SESSION_IO_TIMEOUT{1};
static constexpr std::chrono::seconds DEFAULT_TIMEOUT{10};

/* ═══════════════════════════════════════════════════════════════════════════
 * Session worker function (runs inside SessionState::worker jthread)
 * ═══════════════════════════════════════════════════════════════════════════ */

namespace {

/* Forward declarations */
static std::optional<UspError>
handle_notification_record(Transport& transport,
                            const proto::Record& record,
                            const std::string& endpoint_id,
                            const std::string& agent_endpoint_id,
                            const std::vector<std::function<void()>>& callbacks);

static std::variant<proto::Record, UspError>
wait_for_non_notify(Transport& transport,
                    std::chrono::seconds timeout,
                    const std::string& endpoint_id,
                    const std::string& agent_endpoint_id,
                    std::vector<std::function<void()>>& callbacks);

/* Build a USP Record wrapping an inner Msg. */
static proto::Record build_record_fn(const std::string& from_id,
                                     const std::string& to_id,
                                     proto::MsgType msg_type,
                                     proto::Body body)
{
    proto::Msg msg;
    msg.header = proto::Header{next_msg_id(), msg_type};
    msg.body   = std::move(body);

    proto::NoSessionContextRecord nsc;
    nsc.payload = msg.encode();

    proto::Record rec;
    rec.version         = "1.5";
    rec.to_id           = to_id;
    rec.from_id         = from_id;
    rec.payload_security = 0; /* Plaintext */
    rec.record_type     = std::move(nsc);
    return rec;
}

/* Extract inner Msg from a NoSessionContextRecord. */
static std::variant<proto::Msg, UspError> extract_msg(const proto::Record& record) {
    const auto* nsc = std::get_if<proto::NoSessionContextRecord>(&record.record_type);
    if (!nsc)
        return UspError::protocol("expected NoSessionContextRecord in response");

    auto msg = proto::Msg::decode(nsc->payload);
    if (!msg)
        return UspError::protocol("failed to decode inner Msg");

    return std::move(*msg);
}

/* Check if this record contains a Notify, return it if so. */
static std::optional<proto::Notify> extract_notify(const proto::Record& record) {
    auto msg_var = extract_msg(record);
    if (std::holds_alternative<UspError>(msg_var)) return std::nullopt;

    auto& msg = std::get<proto::Msg>(msg_var);
    if (!msg.body) return std::nullopt;

    auto* req = std::get_if<proto::Request>(&msg.body->msg_body);
    if (!req) return std::nullopt;

    auto* notify = std::get_if<proto::Notify>(&req->req_type);
    if (!notify) return std::nullopt;

    return *notify;
}

/* True if an ADD result indicates the subscription was already registered,
 * which is not a failure: the subscription is present either because this
 * ADD created it or because it already existed on the agent.  Success for a
 * subscription requires that it exists (newly created or pre-existing), not
 * that it was created by this specific ADD. */
static bool is_already_subscribed(const proto::AddOperationFailure& f) {
    if (f.err_code == 7025) return true; /* subscription ID already in use */
    return f.err_msg.find("already in use") != std::string::npos ||
           f.err_msg.find("already exists") != std::string::npos;
}

/* Handle an incoming record that might be a Notify.
 * Returns nullopt if the record was a Notify (handled).
 * Returns the record itself if it was not a Notify (pass back to caller). */
static std::optional<UspError>
handle_notification_record(Transport& transport,
                            const proto::Record& record,
                            const std::string& endpoint_id,
                            const std::string& agent_endpoint_id,
                            const std::vector<std::function<void()>>& callbacks)
{
    auto notify_opt = extract_notify(record);
    if (!notify_opt) return std::nullopt; /* Not a notify – caller handles it */

    const auto& notify = *notify_opt;

    if (notify.send_resp) {
        proto::NotifyResp nr;
        nr.subscription_id = notify.subscription_id;

        proto::Response resp;
        resp.resp_type = std::move(nr);

        proto::Body body;
        body.msg_body = std::move(resp);

        auto resp_record = build_record_fn(endpoint_id, agent_endpoint_id,
                                           proto::MsgType::NotifyResp,
                                           std::move(body));
        transport.send(resp_record);
    }

    /* Fire all notification callbacks. */
    for (const auto& cb : callbacks) {
        if (cb) cb();
    }

    return std::nullopt;
}

/* Wait for the first non-Notify record; handle any Notify records inline. */
static std::variant<proto::Record, UspError>
wait_for_non_notify(Transport& transport,
                    std::chrono::seconds timeout,
                    const std::string& endpoint_id,
                    const std::string& agent_endpoint_id,
                    std::vector<std::function<void()>>& callbacks)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;) {
        auto result = transport.recv();

        if (auto* rec = std::get_if<proto::Record>(&result)) {
            /* Check if it's a Notify. */
            auto notify_opt = extract_notify(*rec);
            if (notify_opt) {
                /* Handle it and continue waiting. */
                handle_notification_record(transport, *rec,
                                           endpoint_id, agent_endpoint_id,
                                           callbacks);
                continue;
            }
            /* Non-notify response: return to caller. */
            return std::move(*rec);
        }

        auto& err = std::get<TransportError>(result);
        if (err.is_timeout()) {
            if (std::chrono::steady_clock::now() >= deadline)
                return UspError::timeout();
            continue;
        }
        return UspError::from_transport(err);
    }
}

/* ── Worker function ──────────────────────────────────────────────────────── */

void session_worker(std::stop_token stop_token,
                    Transport transport_in,
                    std::weak_ptr<UspController::SessionState> weak_state,
                    std::chrono::seconds timeout,
                    std::string endpoint_id,
                    std::string agent_endpoint_id)
{
    Transport transport = std::move(transport_in);
    std::vector<std::function<void()>> notify_callbacks;

    while (!stop_token.stop_requested()) {
        /* Lock the session for this iteration only.  Holding a permanent
         * shared_ptr here would keep the SessionState (and its jthread)
         * alive past the owning UspController; when the worker thread then
         * tore down the last reference it would destroy the jthread on
         * itself and self-join (EDEADLK → terminate).  With a weak_ptr the
         * owner always destroys the session, which requests stop and joins
         * deterministically from the owning thread. */
        std::shared_ptr<UspController::SessionState> state_ptr = weak_state.lock();
        if (!state_ptr) return; /* owner gone */

        /* 1. Non-blocking check for a pending command. */
        std::optional<UspController::WorkerCommand> cmd;
        {
            std::unique_lock lk(state_ptr->cmd_mutex);
            if (!state_ptr->commands.empty()) {
                cmd = std::move(state_ptr->commands.front());
                state_ptr->commands.pop();
            }
        }

        if (cmd) {
            /* 2. Process the command. */
            if (auto* req = std::get_if<UspController::RequestCmd>(&*cmd)) {
                /* Send the record, wait for a response. */
                if (auto send_err = transport.send(req->record)) {
                    req->reply.set_value(UspError::from_transport(*send_err));
                    if (send_err->is_recoverable()) return;
                } else {
                    auto resp = wait_for_non_notify(transport, timeout,
                                                   endpoint_id, agent_endpoint_id,
                                                   notify_callbacks);
                    if (auto* rec = std::get_if<proto::Record>(&resp)) {
                        req->reply.set_value(std::move(*rec));
                    } else {
                        auto& err = std::get<UspError>(resp);
                        bool recoverable = err.is_recoverable();
                        req->reply.set_value(err);
                        if (recoverable) return;
                    }
                }
            } else if (auto* sub = std::get_if<UspController::SubscribeCmd>(&*cmd)) {
                /* Build and send an ADD request for subscriptions. */
                std::vector<proto::CreateObject> create_objs;
                for (auto& [sub_id, notif_type, path] : sub->subscriptions) {
                    proto::CreateObject co;
                    co.obj_path = "Device.LocalAgent.Subscription.";
                    co.param_settings = {
                        proto::CreateParamSetting{"Enable",        "true",  true},
                        proto::CreateParamSetting{"ID",            sub_id,  true},
                        proto::CreateParamSetting{"NotifType",     notif_type, true},
                        proto::CreateParamSetting{"ReferenceList", path,    false},
                        proto::CreateParamSetting{"Persistent",    "false", false},
                    };
                    create_objs.push_back(std::move(co));
                }

                proto::Add add_msg;
                add_msg.allow_partial = true;
                add_msg.create_objs   = std::move(create_objs);

                proto::Request req_body;
                req_body.req_type = std::move(add_msg);

                proto::Body body;
                body.msg_body = std::move(req_body);

                auto add_record = build_record_fn(endpoint_id, agent_endpoint_id,
                                                  proto::MsgType::Add,
                                                  std::move(body));

                if (auto send_err = transport.send(add_record)) {
                    sub->reply.set_value(UspError::from_transport(*send_err));
                    if (send_err->is_recoverable()) return;
                } else {
                    auto resp = wait_for_non_notify(transport, timeout,
                                                   endpoint_id, agent_endpoint_id,
                                                   notify_callbacks);
                    if (auto* rec = std::get_if<proto::Record>(&resp)) {
                        /* Parse ADD response. */
                        auto msg_var = extract_msg(*rec);
                        if (auto* msg = std::get_if<proto::Msg>(&msg_var)) {
                            bool ok = false;
                            std::string err_detail;

                            if (msg->body) {
                                if (auto* resp_var = std::get_if<proto::Response>(&msg->body->msg_body)) {
                                    if (auto* ar = std::get_if<proto::AddResp>(&resp_var->resp_type)) {
                                        /* A subscription "already exists" failure is a success:
                                         * the subscription is registered whether it was created
                                         * by this ADD or previously.  Any other failure is real. */
                                        ok = true;
                                        for (auto& cor : ar->created_obj_results) {
                                            if (!cor.oper_status) continue;
                                            if (auto* f = std::get_if<proto::AddOperationFailure>(
                                                    &cor.oper_status->oper_status)) {
                                                if (is_already_subscribed(*f))
                                                    continue;
                                                ok = false;
                                                err_detail = "subscription ADD failed for '" +
                                                             cor.requested_path + "': " + f->err_msg;
                                                break;
                                            }
                                        }
                                    }
                                } else if (auto* eb = std::get_if<proto::ErrorBody>(&msg->body->msg_body)) {
                                    ok = false;
                                    err_detail = "agent error " + std::to_string(eb->err_code) +
                                                 ": " + eb->err_msg;
                                }
                            }

                            if (ok) {
                                /* Register the callback. */
                                notify_callbacks.push_back(std::move(sub->callback));
                                sub->reply.set_value(std::nullopt);
                                std::cout << "OB-USPA: subscription listener active ("
                                          << sub->subscriptions.size() << " subscription(s))\n";
                            } else {
                                sub->reply.set_value(UspError::protocol(
                                    err_detail.empty() ? "unexpected ADD response" : err_detail));
                            }
                        } else {
                            sub->reply.set_value(
                                UspError::protocol("failed to decode ADD response"));
                        }
                    } else {
                        auto& err = std::get<UspError>(resp);
                        bool recoverable = err.is_recoverable();
                        sub->reply.set_value(err);
                        if (recoverable) return;
                    }
                }
            }
            continue; /* Go back to check for more commands */
        }

        /* 3. No command – poll for incoming frames (1s timeout via socket). */
        auto result = transport.recv();

        if (auto* rec = std::get_if<proto::Record>(&result)) {
            auto notify_opt = extract_notify(*rec);
            if (notify_opt) {
                handle_notification_record(transport, *rec,
                                           endpoint_id, agent_endpoint_id,
                                           notify_callbacks);
            } else {
                std::cerr << "USP: unexpected record received in idle loop; ignoring.\n";
            }
        } else {
            auto* transport_err = std::get_if<TransportError>(&result);
            if (transport_err && !transport_err->is_timeout()) {
                std::cerr << "USP: session worker stopping: " << transport_err->to_string() << "\n";
                return;
            }
            /* Timeout — normal, loop again. */
        }
    }
}

/* Complete the UDS session-establishment handshake.
 *
 * Returns nullopt on success.  On failure returns a TransportError that the
 * caller (ensure_session) converts and throws.
 *
 * The agent expects a UDSConnectRecord to begin a controller session; it
 * acknowledges with its own UDSConnectRecord before it will process
 * NoSessionContext requests.  Without this handshake the agent only ever
 * responds with a UDSConnectRecord and libuspmtp would report
 * "expected NoSessionContextRecord in response". */
static std::optional<TransportError>
establish_uds_session(Transport& transport,
                      const std::string& from_id,
                      const std::string& to_id,
                      std::chrono::seconds timeout)
{
    proto::Record connect_record;
    connect_record.version           = "1.5";
    connect_record.to_id             = to_id;
    connect_record.from_id           = from_id;
    connect_record.payload_security  = 0;
    connect_record.record_type       = proto::UdsConnectRecord{};

    if (auto err = transport.send(connect_record))
        return err;

    /* Await the agent's UDSConnectRecord acknowledgement. */
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = transport.recv();

        if (auto* rec = std::get_if<proto::Record>(&result)) {
            if (std::holds_alternative<proto::UdsConnectRecord>(rec->record_type))
                return std::nullopt; /* Session established. */

            /* Unexpected record at handshake. */
            return TransportError::protocol(
                "expected UDSConnectRecord during session establishment");
        }

        auto& err = std::get<TransportError>(result);
        if (err.is_timeout())
            continue; /* Keep waiting until the deadline. */
        return err;
    }

    return TransportError::connection_failed(
        "no UDS Connect acknowledgement from agent (session not established)");
}

} // anonymous namespace

/* ═══════════════════════════════════════════════════════════════════════════
 * SessionState destructor
 * ═══════════════════════════════════════════════════════════════════════════ */

UspController::SessionState::~SessionState() {
    /* The worker must already be stopped and joined by the owning thread
     * (see shutdown in ~UspController / clear_session).  The jthread
     * destructor is a no-op safeguard here; it must never observe a
     * joinable thread owned by the thread running the destructor. */
}

/* Stop a session worker and join it from the calling (owning) thread.
 * Must be called with the session owned by the caller, never from the
 * worker thread itself.  Bounded: the worker wakes from socket I/O within
 * ~1s and from request waits within the request timeout. */
static void shutdown_state(const std::shared_ptr<UspController::SessionState>& state) {
    if (!state) return;
    state->worker.request_stop();
    if (state->worker.joinable())
        state->worker.join();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UspController
 * ═══════════════════════════════════════════════════════════════════════════ */

UspController::UspController(std::string socket_path,
                             std::string app_endpoint_id,
                             std::string agent_endpoint_id)
    : socket_path_(std::move(socket_path))
    , endpoint_id_(std::move(app_endpoint_id))
    , agent_endpoint_id_(std::move(agent_endpoint_id))
    , timeout_(DEFAULT_TIMEOUT)
    , session_mutex_(std::make_shared<std::mutex>())
    , session_(std::make_shared<std::optional<std::shared_ptr<SessionState>>>())
{}

void UspController::set_timeout(std::chrono::seconds t) {
    timeout_ = t;
}

UspController::~UspController() {
    std::lock_guard lk(*session_mutex_);
    if (session_ && *session_) {
        shutdown_state(**session_);
        *session_ = std::nullopt;
    }
}

std::chrono::seconds UspController::timeout() const {
    return timeout_;
}

/* ── ensure_session ─────────────────────────────────────────────────────── */

std::shared_ptr<UspController::SessionState> UspController::ensure_session() {
    std::lock_guard lk(*session_mutex_);

    if (*session_) return **session_;

    /* Connect transport with a 1-second I/O timeout (for the worker poll). */
    auto transport_result = Transport::connect(
        socket_path_, SESSION_IO_TIMEOUT, endpoint_id_);

    if (auto* err = std::get_if<TransportError>(&transport_result))
        throw *err; /* caller converts to UspError */

    Transport transport = std::move(std::get<Transport>(transport_result));

    /* Perform the UDS session-establishment handshake required by the
     * OB-USPA UDS MTP.  After the socket handshake completes, the agent
     * responds to requests with a UDSConnectRecord until the controller
     * establishes the USP session.  Send a UDSConnectRecord and wait for
     * the agent's UDSConnectRecord acknowledgement before any commands are
     * processed. */
    if (auto connect_err = establish_uds_session(transport, endpoint_id_,
                                                  agent_endpoint_id_, timeout_)) {
        throw *connect_err;
    }

    auto state = std::make_shared<SessionState>();

    /* The worker thread holds only a weak_ptr to the session: it must never
     * own the SessionState, otherwise the last reference could be released
     * on the worker thread itself, destroying (and self-joining) the jthread
     * it is running in. */
    std::weak_ptr<SessionState> weak_state = state;
    std::string ep = endpoint_id_;
    std::string aep = agent_endpoint_id_;
    std::chrono::seconds to = timeout_;

    state->worker = std::jthread(
        [weak_state, ep, aep, to](std::stop_token token, Transport t) {
            session_worker(std::move(token), std::move(t),
                           weak_state, to, ep, aep);
        },
        std::move(transport));

    *session_ = state;
    return state;
}

void UspController::clear_session() {
    std::lock_guard lk(*session_mutex_);
    if (session_ && *session_) {
        shutdown_state(**session_);
        *session_ = std::nullopt;
    }
}

/* ── build_record ───────────────────────────────────────────────────────── */

proto::Record UspController::build_record(proto::MsgType msg_type, proto::Body body) const {
    return build_record_fn(endpoint_id_, agent_endpoint_id_, msg_type, std::move(body));
}

/* ── round_trip ─────────────────────────────────────────────────────────── */

UspResult<proto::Record> UspController::round_trip(proto::Record record) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::shared_ptr<SessionState> state;
        try {
            state = ensure_session();
        } catch (const TransportError& te) {
            return UspError::from_transport(te);
        }

        /* Enqueue command with a promise. */
        std::promise<UspResult<proto::Record>> promise;
        auto future = promise.get_future();

        {
            std::unique_lock lk(state->cmd_mutex);
            RequestCmd cmd;
            cmd.record = record;
            cmd.reply  = std::move(promise);
            state->commands.emplace(std::move(cmd));
        }
        state->cmd_cv.notify_one();

        /* Wait for reply, bounded: if the worker died without fulfilling
         * the promise (e.g. its connection dropped after the session was
         * ensured but before it picked up this command), future.get()
         * would hang forever.  The worker always answers within the
         * request timeout, so anything beyond that plus a grace margin
         * means the session is dead: drop it and retry on a fresh one. */
        if (future.wait_for(timeout_ + std::chrono::seconds(5)) !=
            std::future_status::ready) {
            clear_session();
            if (attempt == 0) continue;
            return UspError::connection_failed("no response from session worker");
        }
        auto result = future.get();

        if (auto* rec = std::get_if<proto::Record>(&result))
            return std::move(*rec);

        auto& err = std::get<UspError>(result);
        if (err.is_recoverable() && attempt == 0) {
            std::cerr << "USP: connection lost (" << err.to_string()
                      << "), reconnecting...\n";
            clear_session();
            continue;
        }
        return err;
    }
    return UspError::connection_failed("failed to establish shared USP session");
}

/* ── do_subscribe ───────────────────────────────────────────────────────── */

std::optional<UspError> UspController::do_subscribe(
    const std::vector<std::tuple<std::string,std::string,std::string>>& subs,
    std::function<void()> callback)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::shared_ptr<SessionState> state;
        try {
            state = ensure_session();
        } catch (const TransportError& te) {
            return UspError::from_transport(te);
        }

        std::promise<std::optional<UspError>> promise;
        auto future = promise.get_future();

        {
            std::unique_lock lk(state->cmd_mutex);
            SubscribeCmd cmd;
            cmd.subscriptions = subs;
            cmd.callback = callback;
            cmd.reply = std::move(promise);
            state->commands.emplace(std::move(cmd));
        }
        state->cmd_cv.notify_one();

        /* Bounded wait, as in round_trip: never hang on a dead worker. */
        if (future.wait_for(timeout_ + std::chrono::seconds(5)) !=
            std::future_status::ready) {
            clear_session();
            if (attempt == 0) continue;
            return UspError::connection_failed("no response from session worker");
        }
        auto result = future.get();

        if (!result) return std::nullopt; /* success */

        if (result->is_recoverable() && attempt == 0) {
            clear_session();
            continue;
        }
        return result;
    }
    return UspError::connection_failed("failed to establish shared USP session");
}

/* ── notif_type_str ─────────────────────────────────────────────────────── */

std::string UspController::notif_type_str(SubscriptionNotificationType t) {
    switch (t) {
    case SubscriptionNotificationType::ValueChange:    return "ValueChange";
    case SubscriptionNotificationType::ObjectCreation: return "ObjectCreation";
    case SubscriptionNotificationType::ObjectDeletion: return "ObjectDeletion";
    }
    return "ValueChange";
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Response parsing helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static UspResult<GetResponse> parse_get_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in GET response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* gr = std::get_if<proto::GetResp>(&resp->resp_type);
        if (!gr) return UspError::protocol("unexpected response type for GET");

        GetResponse out;
        for (auto& path_result : gr->req_path_results) {
            if (path_result.err_code != 0) {
                out.errors.push_back({path_result.requested_path,
                                      path_result.err_code,
                                      path_result.err_msg});
            } else {
                for (auto& resolved : path_result.resolved_path_results) {
                    for (auto& [param, value] : resolved.result_params) {
                        /* resolved_path ends with '.' per TR-369. */
                        out.params[resolved.resolved_path + param] = value;
                    }
                }
            }
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for GET response");
}

static UspResult<SetResponse> parse_set_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in SET response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* sr = std::get_if<proto::SetResp>(&resp->resp_type);
        if (!sr) return UspError::protocol("unexpected response type for SET");

        SetResponse out;
        for (auto& obj_result : sr->updated_obj_results) {
            if (obj_result.oper_status) {
                std::visit([&](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, proto::OperationSuccess>) {
                        out.updated.push_back(obj_result.requested_path);
                    } else if constexpr (std::is_same_v<T, proto::OperationFailure>) {
                        out.errors.push_back({obj_result.requested_path,
                                              v.err_code, v.err_msg});
                    }
                }, obj_result.oper_status->oper_status);
            }
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for SET response");
}

static UspResult<OperateResponse> parse_operate_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in OPERATE response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* or_ = std::get_if<proto::OperateResp>(&resp->resp_type);
        if (!or_) return UspError::protocol("unexpected response type for OPERATE");

        OperateResponse out;
        for (auto& op_result : or_->operation_results) {
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, proto::OutputArgs>) {
                    for (auto& [k, val] : v.output_args)
                        out.output_args[k] = val;
                } else if constexpr (std::is_same_v<T, proto::CommandFailure>) {
                    /* Return error via early return isn't possible inside visit;
                     * mark error and break. A single failure is fatal. */
                    out.output_args.clear();
                    out.output_args["__error__"] =
                        std::to_string(v.err_code) + ": " + v.err_msg;
                }
            }, op_result.operation_resp);
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for OPERATE response");
}

static UspResult<RegisterResponse> parse_register_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in REGISTER response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* rr = std::get_if<proto::RegisterResp>(&resp->resp_type);
        if (!rr) return UspError::protocol("unexpected response type for REGISTER");

        RegisterResponse out;
        for (auto& path_result : rr->registered_path_results) {
            if (!path_result.oper_status) {
                out.errors.push_back({path_result.requested_path, 0,
                                      "missing operation status"});
                continue;
            }
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, proto::RegisterOperationSuccess>) {
                    out.registered.push_back({path_result.requested_path,
                                              v.registered_path});
                } else if constexpr (std::is_same_v<T, proto::RegisterOperationFailure>) {
                    out.errors.push_back({path_result.requested_path,
                                          v.err_code, v.err_msg});
                }
            }, path_result.oper_status->oper_status);
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for REGISTER response");
}

static UspResult<AddResponse> parse_add_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in ADD response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* ar = std::get_if<proto::AddResp>(&resp->resp_type);
        if (!ar) return UspError::protocol("unexpected response type for ADD");

        AddResponse out;
        for (auto& obj_result : ar->created_obj_results) {
            if (!obj_result.oper_status) {
                out.errors.push_back({obj_result.requested_path, 0,
                                      "missing operation status"});
                continue;
            }
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, proto::AddOperationSuccess>) {
                    out.created.push_back({obj_result.requested_path,
                                           v.instantiated_path, v.unique_keys});
                } else if constexpr (std::is_same_v<T, proto::AddOperationFailure>) {
                    out.errors.push_back({obj_result.requested_path,
                                          v.err_code, v.err_msg});
                }
            }, obj_result.oper_status->oper_status);
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for ADD response");
}

static UspResult<DeleteResponse> parse_delete_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in DELETE response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* dr = std::get_if<proto::DeleteResp>(&resp->resp_type);
        if (!dr) return UspError::protocol("unexpected response type for DELETE");

        DeleteResponse out;
        for (auto& obj_result : dr->deleted_obj_results) {
            if (!obj_result.oper_status) {
                out.errors.push_back({obj_result.requested_path, 0,
                                      "missing operation status"});
                continue;
            }
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, proto::DeleteOperationSuccess>) {
                    out.deleted.push_back({obj_result.requested_path,
                                           v.affected_paths});
                    for (auto& unaffected : v.unaffected_errors) {
                        out.errors.push_back({unaffected.path,
                                              unaffected.err_code,
                                              unaffected.err_msg});
                    }
                } else if constexpr (std::is_same_v<T, proto::DeleteOperationFailure>) {
                    out.errors.push_back({obj_result.requested_path,
                                          v.err_code, v.err_msg});
                }
            }, obj_result.oper_status->oper_status);
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for DELETE response");
}

static UspResult<GetSupportedDMResponse> parse_gsdm_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in GET_SUPPORTED_DM response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* gr = std::get_if<proto::GetSupportedDMResp>(&resp->resp_type);
        if (!gr) return UspError::protocol("unexpected response type for GET_SUPPORTED_DM");

        GetSupportedDMResponse out;
        for (auto& result : gr->results) {
            GetSupportedDMResponse::Result r;
            r.requested_path = result.requested_path;
            r.err_code       = result.err_code;
            r.err_msg        = result.err_msg;
            r.data_model_uri = result.data_model_uri;
            r.objects        = result.objects;
            out.results.push_back(std::move(r));
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for GET_SUPPORTED_DM response");
}

static UspResult<GetInstancesResponse> parse_get_instances_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in GET_INSTANCES response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* gr = std::get_if<proto::GetInstancesResp>(&resp->resp_type);
        if (!gr) return UspError::protocol("unexpected response type for GET_INSTANCES");

        GetInstancesResponse out;
        for (auto& result : gr->results) {
            GetInstancesResponse::Result r;
            r.requested_path = result.requested_path;
            r.err_code       = result.err_code;
            r.err_msg        = result.err_msg;
            r.instances      = result.instances;
            out.results.push_back(std::move(r));
        }
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for GET_INSTANCES response");
}

static UspResult<GetSupportedProtocolResponse> parse_gsp_response(const proto::Msg& msg) {
    if (!msg.body)
        return UspError::protocol("empty body in GET_SUPPORTED_PROTOCOL response");

    if (auto* resp = std::get_if<proto::Response>(&msg.body->msg_body)) {
        auto* gr = std::get_if<proto::GetSupportedProtocolResp>(&resp->resp_type);
        if (!gr) return UspError::protocol("unexpected response type for GET_SUPPORTED_PROTOCOL");

        GetSupportedProtocolResponse out;
        out.agent_versions = gr->agent_versions;
        return out;
    }

    if (auto* eb = std::get_if<proto::ErrorBody>(&msg.body->msg_body))
        return UspError::agent_rejected(eb->err_code, eb->err_msg);

    return UspError::protocol("unexpected body for GET_SUPPORTED_PROTOCOL response");
}

/* Helper: group (full_param_path, value) pairs into UpdateObject entries. */
static std::vector<proto::UpdateObject>
group_params(const std::vector<std::pair<std::string,std::string>>& params) {
    std::vector<std::string> order;
    std::map<std::string, std::vector<proto::UpdateParamSetting>> by_obj;

    for (auto& [path, value] : params) {
        auto dot = path.rfind('.');
        std::string obj_path  = (dot != std::string::npos) ? path.substr(0, dot + 1) : path;
        std::string param_name= (dot != std::string::npos) ? path.substr(dot + 1)    : std::string{};

        if (by_obj.find(obj_path) == by_obj.end()) {
            order.push_back(obj_path);
        }
        proto::UpdateParamSetting s;
        s.param    = param_name;
        s.value    = value;
        s.required = true;
        by_obj[obj_path].push_back(std::move(s));
    }

    std::vector<proto::UpdateObject> result;
    for (auto& op : order) {
        proto::UpdateObject o;
        o.obj_path      = op;
        o.param_settings= std::move(by_obj[op]);
        result.push_back(std::move(o));
    }
    return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API implementations
 * ═══════════════════════════════════════════════════════════════════════════ */

UspResult<GetResponse> UspController::get(const std::string& path) {
    return get_many({path});
}

UspResult<GetResponse> UspController::get_many(const std::vector<std::string>& paths) {
    proto::Get get_msg;
    get_msg.param_paths = paths;
    get_msg.max_depth   = 0;

    proto::Request req;
    req.req_type = std::move(get_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Get, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_get_response(std::get<proto::Msg>(msg_var));
}

UspResult<SetResponse> UspController::set(const std::string& path, const std::string& value) {
    return set_many({{path, value}});
}

UspResult<SetResponse> UspController::set_many(
    const std::vector<std::pair<std::string,std::string>>& params)
{
    proto::Set set_msg;
    set_msg.allow_partial = false;
    set_msg.update_objs   = group_params(params);

    proto::Request req;
    req.req_type = std::move(set_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Set, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_set_response(std::get<proto::Msg>(msg_var));
}

UspResult<OperateResponse> UspController::operate(
    const std::string& command,
    const std::vector<std::pair<std::string,std::string>>& args)
{
    proto::Operate op_msg;
    op_msg.command     = command;
    op_msg.command_key = "";
    op_msg.send_resp   = true;
    for (auto& [k, v] : args)
        op_msg.input_args[k] = v;

    proto::Request req;
    req.req_type = std::move(op_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Operate, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    auto op_result = parse_operate_response(std::get<proto::Msg>(msg_var));
    if (auto* err = std::get_if<UspError>(&op_result)) return *err;

    /* Check for internal error marker. */
    auto& or_ = std::get<OperateResponse>(op_result);
    if (auto it = or_.output_args.find("__error__"); it != or_.output_args.end()) {
        std::string msg = it->second;
        or_.output_args.erase(it);
        /* Parse "code: msg" */
        auto colon = msg.find(':');
        uint32_t code = 0;
        std::string detail = msg;
        if (colon != std::string::npos) {
            try { code = static_cast<uint32_t>(std::stoul(msg)); } catch (...) {}
            detail = msg.substr(colon + 2);
        }
        return UspError::agent_rejected(code, detail);
    }
    return or_;
}

UspResult<GetResponse> UspController::subscribe_and_get(
    const std::string& path,
    SubscriptionNotificationType notif_type,
    std::function<void()> callback)
{
    return subscribe_many_and_get({{path, notif_type}}, std::move(callback));
}

UspResult<GetResponse> UspController::subscribe_many_and_get(
    const std::vector<std::pair<std::string, SubscriptionNotificationType>>& subs,
    std::function<void()> callback)
{
    if (subs.empty())
        return UspError::protocol("subscribe_many_and_get requires at least one subscription");

    /* Build subscription tuples: (sub_id, notif_type_str, path). */
    std::vector<std::tuple<std::string,std::string,std::string>> sub_tuples;
    for (auto& [path, notif_type] : subs) {
        sub_tuples.emplace_back("sub-" + next_msg_id(),
                                notif_type_str(notif_type),
                                path);
    }

    if (auto err = do_subscribe(sub_tuples, std::move(callback)))
        return *err;

    /* Deduplicate GET paths. */
    std::vector<std::string> get_paths;
    std::set<std::string> seen;
    for (auto& [path, _] : subs)
        if (seen.insert(path).second)
            get_paths.push_back(path);

    return get_many(get_paths);
}

/* ── CreateObject grouping helper ────────────────────────────────────────── */

/* Group (param_name_or_path, value) pairs into a single CreateObject.
 * Names may be relative ("Enable") or full paths under obj; a full path
 * is reduced to its trailing parameter name. */
static proto::CreateObject group_create_params(
    const std::string& obj,
    const std::vector<std::pair<std::string,std::string>>& params)
{
    proto::CreateObject co;
    co.obj_path = obj;
    for (auto& [path, value] : params) {
        std::string name = path;
        if (name.rfind(obj, 0) == 0)
            name = name.substr(obj.size());
        auto dot = name.rfind('.');
        if (dot != std::string::npos)
            name = name.substr(dot + 1);
        proto::CreateParamSetting s;
        s.param    = name;
        s.value    = value;
        s.required = true;
        co.param_settings.push_back(std::move(s));
    }
    return co;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * REGISTER / ADD / DELETE / GET_SUPPORTED_DM / GET_INSTANCES /
 * GET_SUPPORTED_PROTOCOL implementations
 * ═══════════════════════════════════════════════════════════════════════════ */

UspResult<RegisterResponse> UspController::register_obj(const std::string& obj) {
    proto::Register reg_msg;
    reg_msg.allow_partial = false;
    reg_msg.reg_paths     = {obj};

    proto::Request req;
    req.req_type = std::move(reg_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Register, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_register_response(std::get<proto::Msg>(msg_var));
}

UspResult<AddResponse> UspController::add(
    const std::string& obj,
    const std::vector<std::pair<std::string,std::string>>& params)
{
    proto::Add add_msg;
    add_msg.allow_partial = false;
    add_msg.create_objs.push_back(group_create_params(obj, params));

    proto::Request req;
    req.req_type = std::move(add_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Add, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_add_response(std::get<proto::Msg>(msg_var));
}

UspResult<DeleteResponse> UspController::delete_instance(const std::string& instance) {
    proto::Delete del_msg;
    del_msg.allow_partial = false;
    del_msg.obj_paths     = {instance};

    proto::Request req;
    req.req_type = std::move(del_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::Delete, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_delete_response(std::get<proto::Msg>(msg_var));
}

UspResult<GetSupportedDMResponse> UspController::get_supported_dm(const std::string& obj) {
    proto::GetSupportedDM gsdm_msg;
    gsdm_msg.obj_paths              = {obj};
    gsdm_msg.first_level_only       = false;
    gsdm_msg.return_commands        = true;
    gsdm_msg.return_events          = true;
    gsdm_msg.return_params          = true;
    gsdm_msg.return_unique_key_sets = true;

    proto::Request req;
    req.req_type = std::move(gsdm_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::GetSupportedDm, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_gsdm_response(std::get<proto::Msg>(msg_var));
}

UspResult<GetInstancesResponse> UspController::get_instances(const std::string& obj) {
    proto::GetInstances gi_msg;
    gi_msg.obj_paths        = {obj};
    gi_msg.first_level_only = false;

    proto::Request req;
    req.req_type = std::move(gi_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::GetInstances, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_get_instances_response(std::get<proto::Msg>(msg_var));
}

UspResult<GetSupportedProtocolResponse> UspController::get_supported_protocol() {
    proto::GetSupportedProtocol gsp_msg;
    gsp_msg.controller_versions = "1.5";

    proto::Request req;
    req.req_type = std::move(gsp_msg);

    proto::Body body;
    body.msg_body = std::move(req);

    auto result = round_trip(build_record(proto::MsgType::GetSupportedProto, std::move(body)));
    if (auto* err = std::get_if<UspError>(&result)) return *err;

    auto msg_var = extract_msg(std::get<proto::Record>(result));
    if (auto* err = std::get_if<UspError>(&msg_var)) return *err;

    return parse_gsp_response(std::get<proto::Msg>(msg_var));
}

} // namespace usp
