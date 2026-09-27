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
 * examples/rust/src/main.rs
 *
 * Rust example demonstrating the libuspmtp C API over a hand-written FFI
 * layer (no external crates), with the same behaviour as examples/c/basic.c:
 *
 *   1. GET the baseline paths (Device.LocalAgent., Device.UnixDomainSockets.,
 *      Device.DeviceInfo.) and print all returned parameters.  These prove we
 *      are fetching real values from the usp-agent-app data model.
 *   2. Subscribe to Device.DeviceInfo. for ValueChange notifications.
 *   3. On each notification, re-GET the baseline paths and print the
 *      updated parameter values.
 *
 * The notification callback is invoked from the library's internal worker
 * thread and MUST NOT call any usp_controller_* functions (deadlock risk).
 * Instead the callback writes one byte to a self-pipe; the main thread
 * performs a blocking read on the pipe and issues the follow-up GET safely.
 * Signal handling (SIGINT/SIGTERM) uses the same self-pipe so shutdown is
 * race-free: the handler sets a flag and writes a wake byte.
 *
 * Configuration (environment variables take precedence over argv):
 *   USP_SOCKET_PATH       – path to the OB-USPA UNIX domain socket
 *   USP_APP_ENDPOINT_ID   – this application's USP endpoint ID (from_id)
 *   USP_AGENT_ENDPOINT_ID – the USP agent's endpoint ID (to_id)
 *
 * Fallback order: env var → argv[N] → built-in default.
 *
 * Build (requires the shared libuspmtp, i.e. libuspmtp.so):
 *   cmake -S <repo-root> -B <build> -DLIBUSPMTP_BUILD_SHARED=ON
 *   cmake --build <build> && cmake --install <build> --prefix /usr/local
 *   LIBUSPMTP_LIB_DIR=/usr/local/lib cargo build --release
 */

mod ffi;

use std::env;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::process::ExitCode;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};

use ffi::{UspControllerHandle, USP_FFI_OK, USP_FFI_SUBSCRIPTION_VALUE_CHANGE};

// ── Baseline paths ───────────────────────────────────────────────────────────

/*
 * Baseline data used to prove we are fetching real values from the
 * usp-agent-app data model (and not mock/dummy data).  Cross-check the
 * printed values against the agent's factory-reset database:
 *   Device.LocalAgent.EndpointID                  == "proto::api-gateway"
 *   Device.UnixDomainSockets.UnixDomainSocket.1.* == Alias/Mode/Path below
 *   Device.DeviceInfo.*                           == agent DeviceInfo values
 */
const BASELINE_PATHS: &[&str] = &[
    "Device.LocalAgent.",
    "Device.UnixDomainSockets.",
    "Device.DeviceInfo.",
];

const SUBSCRIBE_PATH: &str = "Device.DeviceInfo.";

const RESULT_BUF_LEN: usize = 65536;
const ERROR_BUF_LEN: usize = 512;

// ── Globals ──────────────────────────────────────────────────────────────────

/* Set to true by the SIGINT/SIGTERM handler to request a clean exit. */
static G_STOP: AtomicBool = AtomicBool::new(false);

/* Write end of the self-pipe; the callback and the signal handler write here. */
static PIPE_WR: AtomicI32 = AtomicI32::new(-1);

// ── Signal handler / notification callback ───────────────────────────────────

/*
 * Async-signal-safe: only an atomic store and a pipe write.
 * Wakes the blocking read in main so shutdown is never lost.
 */
unsafe extern "C" fn handle_signal(_sig: c_int) {
    G_STOP.store(true, Ordering::SeqCst);
    let wr = PIPE_WR.load(Ordering::SeqCst);
    if wr >= 0 {
        let byte = 0u8;
        ffi::write(wr, &byte as *const u8 as *const c_void, 1);
    }
}

/*
 * Called by the library's worker thread when a notification arrives.
 * user_data is the write end of the self-pipe cast to a pointer.
 * We write a single byte to wake the main thread; the main thread then
 * performs the follow-up GET.
 *
 * IMPORTANT: do NOT call any usp_controller_* function from here.
 */
unsafe extern "C" fn on_notification(user_data: *mut c_void) {
    let wr = user_data as c_int;
    let byte = 1u8;
    // Ignore errors: if the pipe is full the main thread will still wake.
    ffi::write(wr, &byte as *const u8 as *const c_void, 1);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

/* Return env var if set and non-empty, otherwise argv value, else default. */
fn env_or(var: &str, arg: Option<&str>, default: &str) -> String {
    match env::var(var) {
        Ok(v) if !v.is_empty() => v,
        _ => arg.unwrap_or(default).to_string(),
    }
}

fn to_cstr(label: &str, value: &str) -> Result<CString, String> {
    CString::new(value).map_err(|_| format!("{label} contains an interior NUL byte"))
}

/*
 * Print the tab-separated response text produced by usp_controller_get_many.
 *
 * Format per line:
 *   "PARAM\t<path>\t<value>"        – successfully resolved parameter
 *   "ERROR\t<path>\t<code>\t<msg>" – per-path error
 */
fn print_get_result(result: &str) {
    if result.is_empty() {
        println!("  (empty response)");
        return;
    }
    for line in result.lines() {
        let mut fields = line.split('\t');
        match fields.next() {
            Some("PARAM") => {
                let path = fields.next().unwrap_or("(null)");
                let value = fields.next().unwrap_or("(null)");
                println!("  {path} = {value}");
            }
            Some("ERROR") => {
                let path = fields.next().unwrap_or("?");
                let code = fields.next().unwrap_or("?");
                let msg = fields.next().unwrap_or("?");
                eprintln!("  ERROR path={path} code={code} msg={msg}");
            }
            _ => {}
        }
    }
}

// ── RAII wrappers ────────────────────────────────────────────────────────────

/* Self-pipe: callback/signal handler writes, main thread reads. */
struct SelfPipe {
    rd: c_int,
    wr: c_int,
}

impl SelfPipe {
    fn new() -> std::io::Result<Self> {
        let mut fds = [0 as c_int; 2];
        // SAFETY: fds points to two writable c_int slots.
        let rc = unsafe { ffi::pipe(fds.as_mut_ptr()) };
        if rc == 0 {
            Ok(Self {
                rd: fds[0],
                wr: fds[1],
            })
        } else {
            Err(std::io::Error::last_os_error())
        }
    }
}

impl Drop for SelfPipe {
    fn drop(&mut self) {
        // SAFETY: fds were returned by pipe() and are still open.
        unsafe {
            ffi::close(self.rd);
            ffi::close(self.wr);
        }
    }
}

/*
 * Owned USP controller handle.  Exactly one owner; released via
 * usp_controller_free() in Drop.  Deliberately !Send/!Sync (raw pointer
 * member) because the C API requires single-threaded use of a handle.
 */
struct Controller {
    raw: *mut UspControllerHandle,
}

impl Controller {
    fn new(socket_path: &CStr, app_endpoint: &CStr, agent_endpoint: &CStr) -> Option<Self> {
        // SAFETY: all pointers come from live CStrings; timeout 10s is valid.
        let raw = unsafe {
            ffi::usp_controller_new(
                socket_path.as_ptr(),
                app_endpoint.as_ptr(),
                agent_endpoint.as_ptr(),
                10,
            )
        };
        if raw.is_null() {
            None
        } else {
            Some(Self { raw })
        }
    }

    fn last_error(&self) -> String {
        let mut buf = vec![0 as c_char; ERROR_BUF_LEN];
        // SAFETY: handle is valid; buf is ERROR_BUF_LEN writable bytes.
        unsafe {
            ffi::usp_controller_last_error(self.raw, buf.as_mut_ptr(), buf.len());
            CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
        }
    }

    fn get_many(&self, paths: &[&CStr]) -> Result<String, String> {
        let ptrs: Vec<*const c_char> = paths.iter().map(|p| p.as_ptr()).collect();
        let mut buf = vec![0 as c_char; RESULT_BUF_LEN];
        // SAFETY: handle is valid; ptrs borrows live CStrings; buf is writable.
        let rc = unsafe {
            ffi::usp_controller_get_many(
                self.raw,
                ptrs.as_ptr(),
                ptrs.len(),
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        if rc == USP_FFI_OK {
            // SAFETY: on success the library NUL-terminates the buffer.
            Ok(unsafe {
                CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
            })
        } else {
            Err(format!("rc={rc}: {}", self.last_error()))
        }
    }

    fn subscribe_and_get(&self, path: &CStr, write_fd: c_int) -> Result<String, String> {
        let mut buf = vec![0 as c_char; RESULT_BUF_LEN];
        // SAFETY: handle and path are valid; the pipe write fd stays open for
        // the lifetime of the subscription (owned by main); buf is writable.
        // The callback never touches the handle or the buffer.
        let rc = unsafe {
            ffi::usp_controller_subscribe_and_get(
                self.raw,
                path.as_ptr(),
                USP_FFI_SUBSCRIPTION_VALUE_CHANGE,
                Some(on_notification),
                write_fd as *mut c_void,
                buf.as_mut_ptr(),
                buf.len(),
            )
        };
        if rc == USP_FFI_OK {
            // SAFETY: on success the library NUL-terminates the buffer.
            Ok(unsafe {
                CStr::from_ptr(buf.as_ptr()).to_string_lossy().into_owned()
            })
        } else {
            Err(format!("rc={rc}: {}", self.last_error()))
        }
    }
}

impl Drop for Controller {
    fn drop(&mut self) {
        // SAFETY: raw came from usp_controller_new() and is freed exactly once.
        unsafe {
            ffi::usp_controller_free(self.raw);
        }
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

fn run() -> Result<(), String> {
    let args: Vec<String> = env::args().collect();
    let socket_path = env_or(
        "USP_SOCKET_PATH",
        args.get(1).map(String::as_str),
        "/var/run/usp/broker_agent_path",
    );
    let app_endpoint_id = env_or(
        "USP_APP_ENDPOINT_ID",
        args.get(2).map(String::as_str),
        "proto::myapp",
    );
    let agent_endpoint_id = env_or(
        "USP_AGENT_ENDPOINT_ID",
        args.get(3).map(String::as_str),
        "proto::api-gateway",
    );

    println!("libuspmtp Rust example");
    println!("  socket : {socket_path}");
    println!("  app    : {app_endpoint_id}");
    println!("  agent  : {agent_endpoint_id}\n");

    // ── Self-pipe for safe cross-thread signalling ──
    let pipe = SelfPipe::new().map_err(|e| format!("pipe failed: {e}"))?;
    PIPE_WR.store(pipe.wr, Ordering::SeqCst);

    // ── Signal handling ──
    // SAFETY: handlers only perform async-signal-safe operations.
    unsafe {
        ffi::signal(ffi::SIGINT, handle_signal);
        ffi::signal(ffi::SIGTERM, handle_signal);
        let sig_ign: ffi::SignalHandler = std::mem::transmute(1usize);
        ffi::signal(ffi::SIGPIPE, sig_ign);
    }

    // ── Create USP controller handle ──
    let c_socket = to_cstr("socket path", &socket_path)?;
    let c_app = to_cstr("app endpoint ID", &app_endpoint_id)?;
    let c_agent = to_cstr("agent endpoint ID", &agent_endpoint_id)?;
    let controller = Controller::new(&c_socket, &c_app, &c_agent)
        .ok_or_else(|| "Failed to create USP controller handle.".to_string())?;

    let baseline: Vec<CString> = BASELINE_PATHS
        .iter()
        .map(|p| to_cstr("baseline path", p))
        .collect::<Result<_, _>>()?;
    let baseline_refs: Vec<&CStr> =
        baseline.iter().map(|p| p.as_c_str()).collect();

    // ── 1. GET baseline paths ──
    println!("=== GET baseline (LocalAgent, UnixDomainSockets, DeviceInfo) ===");
    match controller.get_many(&baseline_refs) {
        Ok(result) => print_get_result(&result),
        Err(e) => eprintln!("GET failed: {e}"),
    }

    // ── 2. Subscribe to Device.DeviceInfo. for ValueChange ──
    println!("\n=== Subscribing to Device.DeviceInfo. (ValueChange) ===");
    let c_subscribe = to_cstr("subscribe path", SUBSCRIBE_PATH)?;
    match controller.subscribe_and_get(&c_subscribe, pipe.wr) {
        Ok(result) => {
            println!("Subscription active.  Initial GET result:");
            print_get_result(&result);
        }
        Err(e) => {
            eprintln!("subscribe_and_get failed: {e}");
            eprintln!("Continuing without subscription.");
        }
    }

    // ── 3. Wait for notifications and re-GET on each one ──
    println!(
        "\nWaiting for Device.DeviceInfo. change notifications \
         (Ctrl-C to exit)...\n"
    );

    let mut drain = [0u8; 64];
    loop {
        if G_STOP.load(Ordering::SeqCst) {
            break;
        }
        // Blocking read: both the notification callback and the signal
        // handler wake us with a byte, so no timeout is needed and shutdown
        // can never be lost.
        // SAFETY: drain is a live writable buffer of drain.len() bytes.
        let n = unsafe {
            ffi::read(
                pipe.rd,
                drain.as_mut_ptr() as *mut c_void,
                drain.len(),
            )
        };
        if n <= 0 {
            if G_STOP.load(Ordering::SeqCst) {
                break;
            }
            if n == 0 {
                return Err("self-pipe closed unexpectedly".to_string());
            }
            continue; // Interrupted or transient error; retry.
        }
        if G_STOP.load(Ordering::SeqCst) {
            break;
        }

        // ── 3a. Re-GET baseline paths and print updated values ──
        println!("=== Notification received — re-GET baseline ===");
        match controller.get_many(&baseline_refs) {
            Ok(result) => print_get_result(&result),
            Err(e) => eprintln!("re-GET failed: {e}"),
        }
        println!();
    }

    println!("Exiting.");
    Ok(())
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::from(1)
        }
    }
}
