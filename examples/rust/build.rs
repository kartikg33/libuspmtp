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
