// orca_smoke_cli: minimal slicing proof for the iOS port (step 1).
// Loads a mesh, applies full default FFF config, runs the complete
// slicing pipeline, writes G-code. AGPL-3.0, part of Orca-iOS-ipa.
#include <cstdio>
#include <string>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <input.stl> <output.gcode>\n", argv[0]);
        return 2;
    }
    const std::string in  = argv[1];
    const std::string out = argv[2];

    std::printf("orca_smoke_cli: libslic3r %s on iOS\n", SLIC3R_VERSION);

    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.normalize_fdm();

    Model model;
    try {
        model = Model::read_from_file(in, &config, nullptr, LoadStrategy::AddDefaultInstances);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "load failed: %s\n", e.what());
        return 1;
    }
    std::printf("loaded %zu object(s)\n", model.objects.size());

    Print print;
    print.apply(model, config);
    std::string err = print.validate();
    if (!err.empty())
        std::printf("validate: %s (continuing)\n", err.c_str());
    print.process();
    print.export_gcode(out, nullptr, nullptr);
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
