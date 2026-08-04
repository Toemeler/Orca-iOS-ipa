// Bambu LAN backend - print command construction
// ----------------------------------------------
// Isolated from the agent so the host self test can assert the exact JSON that
// goes to the printer without pulling in Orca. Only nlohmann/json is needed.
//
// Copyright (C) 2026 Orca-iOS-ipa contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef slic3r_BambuLanPrintCommand_hpp_
#define slic3r_BambuLanPrintCommand_hpp_

#include <string>

namespace Slic3r {
namespace BambuLan {

struct LanPrintRequest
{
    std::string file_name;        // basename as uploaded, e.g. "my_project.gcode.3mf"
    std::string ftp_folder;       // printer's SD mount prefix, e.g. "sdcard/"; empty -> default
    int         plate_index = 1;  // 1-based, matches Metadata/plate_N.gcode inside the 3mf
    std::string subtask_name;     // shown on the printer's screen; empty -> file stem
    std::string bed_type;         // Orca's gcode bed-type string; empty -> "auto"
    bool        timelapse = false;
    bool        bed_leveling = true;
    bool        flow_cali = false;
    bool        vibration_cali = true;
    bool        layer_inspect = false;
    bool        use_ams = false;
    std::string ams_mapping;      // JSON array string from Orca ("[0,1,-1]"), may be empty
    std::string ams_mapping2;     // newer [{ams_id,slot_id}] form, may be empty
    int         sequence_id = 0;
};

// "sdcard/" + "a.3mf" -> "file:///sdcard/a.3mf"
std::string build_print_url(const std::string& ftp_folder, const std::string& file_name);

// The full {"print": {...}} payload for MQTT topic device/<serial>/request.
std::string build_project_file_command(const LanPrintRequest& req);

// Strips any directory part and characters the firmware's file browser chokes
// on, so the name that goes over FTP is the name the print command references.
std::string sanitize_remote_file_name(const std::string& path);

} // namespace BambuLan
} // namespace Slic3r

#endif // slic3r_BambuLanPrintCommand_hpp_
