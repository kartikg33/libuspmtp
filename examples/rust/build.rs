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

//! Build script for the Rust example.
//!
//! Points rustc at the directory containing the shared libuspmtp
//! (`libuspmtp.so`, CMake `OUTPUT_NAME uspmtp`).  The actual link is declared
//! in `src/ffi.rs` with `#[link(kind = "dylib")]`, which forces dynamic
//! linking and fails the build if only a static archive is present.
//!
//! Configuration:
//!   LIBUSPMTP_LIB_DIR – directory holding libuspmtp.so.
//!                       Defaults to /usr/local/lib (i.e. the result of
//!                       `cmake --install <build> --prefix /usr/local`).

fn main() {
    let lib_dir =
        std::env::var("LIBUSPMTP_LIB_DIR").unwrap_or_else(|_| "/usr/local/lib".to_string());
    println!("cargo:rustc-link-search=native={lib_dir}");
    println!("cargo:rerun-if-env-changed=LIBUSPMTP_LIB_DIR");
    println!("cargo:rerun-if-changed=build.rs");
    // Rebuild if the shared library itself is replaced (best effort).
    println!("cargo:rerun-if-changed={lib_dir}/libuspmtp.so");
}
