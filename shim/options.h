#pragma once
namespace ctr2 {
// Switches read once from Game.exe's command line, each with an environment
// variable equivalent for scripts and tests. All are off by default.
//   --log          write failures and notes to ctr2shim.log    CTR2SHIM_LOG=1
//   --trace        also log every call traced (implies --log)  CTR2SHIM_TRACE=1
//   --capture      save the back buffer to ctr2-frame.bmp      CTR2SHIM_CAPTURE=1
//   --diagnostics  log access violations with stack details    CTR2SHIM_DIAGNOSTICS=1
//   --passthrough  activate the OS classes and only log        CTR2SHIM_MODE=trace
struct Options {
    bool log = false, trace = false, capture = false, diagnostics = false, passthrough = false;
};
const Options& GetOptions() noexcept;
}
