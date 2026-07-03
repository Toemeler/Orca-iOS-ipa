// orca-core-cli: minimal iOS test harness for the OrcaSlicer slicing engine.
// Loads a mesh, applies default full print config, slices, writes G-code.
// Step-1 proof binary for the Orca-iOS-ipa port; not part of the final app.
// License: AGPL-3.0 (same as OrcaSlicer).
#include <cstdio>
#include <string>
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"

using namespace Slic3r;

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <input.stl> <output.gcode>\n", argv[0]);
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    std::printf("orca-core-cli: libslic3r %s on iOS\n", SLIC3R_VERSION);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();

    Model model;
    try {
        model = Model::read_from_file(in, &config);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "load failed: %s\n", e.what());
        return 1;
    }
    for (ModelObject *o : model.objects)
        o->ensure_on_bed();
    std::printf("loaded %zu object(s)\n", model.objects.size());

    Print print;
    print.apply(model, config);
    std::string warn = print.validate();
    if (!warn.empty())
        std::printf("validate: %s\n", warn.c_str());

    try {
        print.process();
        GCodeProcessorResult result;
        print.export_gcode(out, &result, nullptr);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "slicing failed: %s\n", e.what());
        return 1;
    }
    std::printf("OK: wrote %s\n", out.c_str());
    return 0;
}
