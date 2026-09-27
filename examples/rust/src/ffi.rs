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

//! Hand-written FFI bindings for the bits of the libuspmtp C API
//! (`include/libuspmtp.h`) used by the Rust example, plus the raw libc
//! functions needed for the self-pipe and signal handling.
//!
//! No external crates are used so the example builds fully offline.
//!
//! The `#[link(kind = "dylib")]` attribute forces dynamic linking against
//! `libuspmtp.so`; linking fails if only a static archive is available.

use std::os::raw::{c_char, c_int, c_void};

// ── libuspmtp C API ──────────────────────────────────────────────────────────

/// Opaque handle; see `UspControllerHandle` in libuspmtp.h.
/// The caller owns it and must release it with `usp_controller_free`.
#[repr(C)]
pub struct UspControllerHandle {
    _private: [u8; 0],
}

/// `USP_OK` – success.
pub const USP_OK: c_int = 0;

/// `USP_SUBSCRIPTION_VALUE_CHANGE` – notify when a parameter changes.
pub const USP_SUBSCRIPTION_VALUE_CHANGE: c_int = 0;

/// `UspNotificationCallback` – invoked from the library's internal background
/// thread.  Must be thread-safe and must NOT call any `usp_controller_*`
/// functions (deadlock risk).
pub type UspNotificationCallback = Option<unsafe extern "C" fn(*mut c_void)>;

#[link(name = "uspmtp", kind = "dylib")]
extern "C" {
    pub fn usp_controller_new(
        socket_path: *const c_char,
        app_endpoint_id: *const c_char,
        agent_endpoint_id: *const c_char,
        timeout_secs: u64,
    ) -> *mut UspControllerHandle;

    pub fn usp_controller_free(handle: *mut UspControllerHandle);

    pub fn usp_controller_get_many(
        handle: *mut UspControllerHandle,
        paths: *const *const c_char,
        path_count: usize,
        out_result: *mut c_char,
        out_result_len: usize,
    ) -> c_int;

    pub fn usp_controller_subscribe_and_get(
        handle: *mut UspControllerHandle,
        path: *const c_char,
        notification_type: c_int,
        callback: UspNotificationCallback,
        user_data: *mut c_void,
        out_result: *mut c_char,
        out_result_len: usize,
    ) -> c_int;

    pub fn usp_controller_last_error(
        handle: *const UspControllerHandle,
        out_error: *mut c_char,
        out_error_len: usize,
    ) -> c_int;
}

// ── Raw libc (self-pipe + signals, no libc crate) ────────────────────────────

/// Signal handler function type for `signal()`.
pub type SignalHandler = unsafe extern "C" fn(c_int);

pub const SIGINT: c_int = 2;
pub const SIGTERM: c_int = 15;
pub const SIGPIPE: c_int = 13;

extern "C" {
    pub fn pipe(fds: *mut c_int) -> c_int;
    pub fn read(fd: c_int, buf: *mut c_void, count: usize) -> isize;
    pub fn write(fd: c_int, buf: *const c_void, count: usize) -> isize;
    pub fn close(fd: c_int) -> c_int;
    pub fn signal(signum: c_int, handler: SignalHandler) -> SignalHandler;
}
