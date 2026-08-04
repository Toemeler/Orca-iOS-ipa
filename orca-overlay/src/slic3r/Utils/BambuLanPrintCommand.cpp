// Bambu LAN backend - print command construction. See BambuLanPrintCommand.hpp.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BambuLanPrintCommand.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r {
namespace BambuLan {

using json = nlohmann::json;

std::string sanitize_remote_file_name(const std::string& path)
{
    // Keep only the last path component.
    size_t      pos  = path.find_last_of("/\\");
    std::string name = pos == std::string::npos ? path : path.substr(pos + 1);

    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        // The firmware's file list is unhappy with quotes, control characters
        // and path separators; everything else (including UTF-8 continuation
        // bytes, so non-ASCII project names survive) is passed through.
        if (c < 0x20 || c == '"' || c == '\'' || c == ':' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
            out.push_back('_');
        else
            out.push_back(static_cast<char>(c));
    }
    if (out.empty())
        out = "print.3mf";
    return out;
}

std::string build_print_url(const std::string& ftp_folder, const std::string& file_name)
{
    // The upload lands in the FTP root, which the firmware exposes as /sdcard.
    // Orca's per-model config carries that prefix for the P1/A1 series
    // (resources/printers/{C11,C12,N1,N2S}.json -> "ftp_folder": "sdcard/") and
    // leaves it empty for the X1 series, which uses the same path in practice -
    // hence the default rather than a bare "file:///".
    std::string folder = ftp_folder.empty() ? std::string("sdcard/") : ftp_folder;
    while (!folder.empty() && folder.front() == '/')
        folder.erase(folder.begin());
    if (!folder.empty() && folder.back() != '/')
        folder.push_back('/');

    std::string name = file_name;
    while (!name.empty() && name.front() == '/')
        name.erase(name.begin());

    return "file:///" + folder + name;
}

std::string build_project_file_command(const LanPrintRequest& req)
{
    const std::string file_name = sanitize_remote_file_name(req.file_name);

    std::string subtask_name = req.subtask_name;
    if (subtask_name.empty()) {
        subtask_name         = file_name;
        const size_t dot_pos = subtask_name.find_last_of('.');
        if (dot_pos != std::string::npos && dot_pos > 0)
            subtask_name = subtask_name.substr(0, dot_pos);
    }

    const int plate = req.plate_index > 0 ? req.plate_index : 1;

    json print;
    print["sequence_id"]  = std::to_string(req.sequence_id);
    print["command"]      = "project_file";
    print["param"]        = "Metadata/plate_" + std::to_string(plate) + ".gcode";
    print["project_id"]   = "0";
    print["profile_id"]   = "0";
    print["task_id"]      = "0";
    print["subtask_id"]   = "0";
    print["subtask_name"] = subtask_name;
    print["file"]         = "";
    print["url"]          = build_print_url(req.ftp_folder, file_name);
    print["md5"]          = "";
    print["timelapse"]    = req.timelapse;
    print["bed_type"]     = req.bed_type.empty() ? std::string("auto") : req.bed_type;
    // Firmware generations disagree on the spelling of this one key; both are
    // sent, and whichever is not understood is ignored like any unknown field.
    print["bed_leveling"]   = req.bed_leveling;
    print["bed_levelling"]  = req.bed_leveling;
    print["flow_cali"]      = req.flow_cali;
    print["vibration_cali"] = req.vibration_cali;
    print["layer_inspect"]  = req.layer_inspect;
    print["use_ams"]        = req.use_ams;

    // Orca hands these over already serialised. A malformed string would break
    // the whole command, so anything that does not parse as an array is dropped.
    if (!req.ams_mapping.empty()) {
        json mapping = json::parse(req.ams_mapping, nullptr, false);
        if (mapping.is_array())
            print["ams_mapping"] = mapping;
    }
    if (!req.ams_mapping2.empty()) {
        json mapping2 = json::parse(req.ams_mapping2, nullptr, false);
        if (mapping2.is_array())
            print["ams_mapping2"] = mapping2;
    }

    json root;
    root["print"] = print;
    return root.dump();
}

} // namespace BambuLan
} // namespace Slic3r
