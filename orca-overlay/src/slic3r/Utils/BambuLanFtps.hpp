// Bambu LAN backend - FTPS upload
// -------------------------------
// The printer runs an FTP server behind implicit TLS on port 990, user `bblp`
// and the LAN access code as password. That is how a sliced 3mf gets onto the
// machine before the MQTT print command references it.
//
// libcurl (already linked by Orca, and already built for iOS by the step-1
// dependency chain) does the work; this is a thin, dependency-free wrapper so
// the host self test can link it without any of Orca.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef slic3r_BambuLanFtps_hpp_
#define slic3r_BambuLanFtps_hpp_

#include <cstdint>
#include <functional>
#include <string>

namespace Slic3r {
namespace BambuLan {

struct FtpsConfig
{
    std::string host;
    uint16_t    port = 990;
    std::string username = "bblp";
    std::string password;
    bool        use_tls = true;      // false is only used by the host self test
    int         connect_timeout_s = 15;
    // Abort a transfer that moves less than 1 byte/s for this long. The printer's
    // Wi-Fi stack is slow but never silent for a full minute mid-transfer.
    int         stall_timeout_s = 60;
};

enum FtpsResult : int {
    FtpsOk            = 0,
    FtpsFileNotFound  = 1,
    FtpsConnectFailed = 2,
    FtpsAuthFailed    = 3,
    FtpsTransferFailed= 4,
    FtpsCancelled     = 5,
};

// percent complete (0-100)
using FtpsProgressFn = std::function<void(int)>;
// return true to abort the transfer
using FtpsCancelFn = std::function<bool()>;

// Uploads `local_path` to `remote_path` (relative to the FTP root; a leading
// slash is fine, intermediate directories are created). Both callbacks may be
// empty. `error` receives a human-readable reason on failure.
int ftps_upload_file(const FtpsConfig&  cfg,
                     const std::string& local_path,
                     const std::string& remote_path,
                     FtpsProgressFn     progress_fn,
                     FtpsCancelFn       cancel_fn,
                     std::string&       error);

// Builds the URL used above. Exposed for the self test, which asserts the
// escaping rules (the access code is not part of the URL: credentials go
// through CURLOPT_USERNAME/PASSWORD so they never need escaping at all).
std::string build_ftps_url(const std::string& host, uint16_t port, const std::string& remote_path, bool use_tls);

} // namespace BambuLan
} // namespace Slic3r

#endif // slic3r_BambuLanFtps_hpp_
