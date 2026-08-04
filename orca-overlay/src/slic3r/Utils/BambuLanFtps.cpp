// Bambu LAN backend - FTPS upload. See BambuLanFtps.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanFtps.hpp"

#include <cstdio>
#include <sys/stat.h>

#include <curl/curl.h>

namespace Slic3r {
namespace BambuLan {

namespace {

struct UploadState
{
    FILE*          fp = nullptr;
    curl_off_t     total = 0;
    FtpsProgressFn progress_fn;
    FtpsCancelFn   cancel_fn;
    int            last_percent = -1;
};

size_t read_callback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    UploadState* st = static_cast<UploadState*>(userdata);
    if (st->fp == nullptr)
        return 0;
    return std::fread(buffer, size, nitems, st->fp);
}

int xfer_callback(void* userdata, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/, curl_off_t ultotal, curl_off_t ulnow)
{
    UploadState* st = static_cast<UploadState*>(userdata);
    if (st->cancel_fn && st->cancel_fn())
        return 1; // aborts the transfer with CURLE_ABORTED_BY_CALLBACK

    const curl_off_t total = ultotal > 0 ? ultotal : st->total;
    if (st->progress_fn && total > 0) {
        int percent = static_cast<int>((ulnow * 100) / total);
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        if (percent != st->last_percent) {
            st->last_percent = percent;
            st->progress_fn(percent);
        }
    }
    return 0;
}

// The remote path is a file name chosen by Orca (project name, possibly with
// spaces or non-ASCII). It goes into a URL, so it has to be percent-encoded -
// but '/' must survive as a path separator.
std::string url_escape_path(const std::string& path)
{
    static const char* hex = "0123456789ABCDEF";
    std::string        out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                             || c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

} // namespace

std::string build_ftps_url(const std::string& host, uint16_t port, const std::string& remote_path, bool use_tls)
{
    std::string path = remote_path;
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());

    // ftps:// is implicit TLS in libcurl (control channel encrypted from the
    // first byte), which is what the printer's port 990 expects. Plain ftp:// is
    // only ever used by the host self test.
    std::string url = use_tls ? "ftps://" : "ftp://";
    url += host;
    url += ":";
    url += std::to_string(port);
    url += "/";
    url += url_escape_path(path);
    return url;
}

int ftps_upload_file(const FtpsConfig&  cfg,
                     const std::string& local_path,
                     const std::string& remote_path,
                     FtpsProgressFn     progress_fn,
                     FtpsCancelFn       cancel_fn,
                     std::string&       error)
{
    error.clear();

    struct stat st;
    if (::stat(local_path.c_str(), &st) != 0) {
        error = "file not found: " + local_path;
        return FtpsFileNotFound;
    }

    UploadState state;
    state.fp = std::fopen(local_path.c_str(), "rb");
    if (state.fp == nullptr) {
        error = "cannot open " + local_path;
        return FtpsFileNotFound;
    }
    state.total       = static_cast<curl_off_t>(st.st_size);
    state.progress_fn = std::move(progress_fn);
    state.cancel_fn   = std::move(cancel_fn);

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        std::fclose(state.fp);
        error = "curl_easy_init failed";
        return FtpsTransferFailed;
    }

    const std::string url = build_ftps_url(cfg.host, cfg.port, remote_path, cfg.use_tls);

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &state);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, state.total);
    curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.password.c_str());
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(cfg.connect_timeout_s));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(cfg.stall_timeout_s));
    // Orca may name a plate file inside a folder the printer does not have yet.
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, (long) CURLFTP_CREATE_DIR_RETRY);
    // The firmware's FTP server does not answer EPSV; PASV is what it speaks.
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPRT, 0L);
    // Embedded FTP servers routinely announce an address in their PASV reply
    // that is not reachable from the client. The data connection belongs to the
    // same host as the control connection, so use that and keep only the port.
    curl_easy_setopt(curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);

    // The firmware refuses STOR over an existing file, so sending the same
    // project twice would fail on the second try. The leading '*' makes libcurl
    // ignore the reply, which is what a first upload gets (550, no such file).
    const std::string delete_first = "*DELE " + remote_path;
    struct curl_slist* prequote = curl_slist_append(nullptr, delete_first.c_str());
    curl_easy_setopt(curl, CURLOPT_PREQUOTE, prequote);

    if (cfg.use_tls) {
        // Encrypt the data channel too. The printer's certificate is self-signed
        // and its CN never matches the IP, so neither peer nor host can be
        // verified - the access code is the authenticator here, exactly as in
        // the MQTT path.
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long) CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        // The firmware insists the data connection resume the control
        // connection's TLS session; libcurl does that by default with OpenSSL,
        // and this keeps it from being disabled by a global default elsewhere.
        curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    }

    const CURLcode res = curl_easy_perform(curl);

    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(prequote);
    std::fclose(state.fp);

    if (res == CURLE_OK) {
        if (state.progress_fn && state.last_percent != 100)
            state.progress_fn(100);
        return FtpsOk;
    }

    error = errbuf[0] != '\0' ? std::string(errbuf) : std::string(curl_easy_strerror(res));

    switch (res) {
    case CURLE_ABORTED_BY_CALLBACK:
        return FtpsCancelled;
    case CURLE_LOGIN_DENIED:
    case CURLE_REMOTE_ACCESS_DENIED:
        return FtpsAuthFailed;
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_SSL_CONNECT_ERROR:
        return FtpsConnectFailed;
    default:
        // 530 is "not logged in" even when curl reports a generic failure.
        if (response_code == 530)
            return FtpsAuthFailed;
        return FtpsTransferFailed;
    }
}

} // namespace BambuLan
} // namespace Slic3r
