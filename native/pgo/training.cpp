// The training program of the profile that the WebAssembly build uses.
//
// The compiler lays the code of a program out with the branches that the
// profile says are the hot ones next to each other.  A program that is
// rendered from formulas has a few very hot paths (the token loop of the
// parser, the dispatch of the builders) and a lot of cold ones (the
// diagnostics, the rare commands), and a compiler that does not know which
// is which spreads them over the whole text segment.
//
// The profile is collected from a build of the core that is instrumented for
// the profile and that renders a corpus of real world formulas.  The corpus is
// the acceptance corpus of the repository (1605 formulas), which is not the
// corpus of the benchmark, so the layout is not tuned for the numbers of the
// benchmark.
//
// The program reads the corpus from the file that
// `tools/reference/generate-pgo-profile.mjs` writes: a sequence of records
// that hold the length of the formula in bytes and the formula itself as
// UTF-16LE.  The formulas are read from a file and not from the standard
// input because some of them hold a line break.
//
// The profile is written when the program ends, and the file it is written to
// is the one that the `LLVM_PROFILE_FILE` environment variable names.
#include "core.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace velocitex;

namespace {

// Reads the corpus: a sequence of records that hold the length of a formula
// in bytes and the formula as UTF-16LE.
std::vector<std::u16string> ReadCorpus(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "The corpus %s cannot be read.\n", path);
        std::exit(1);
    }
    std::vector<std::u16string> formulas;
    while (true) {
        uint32_t length = 0;
        input.read(reinterpret_cast<char*>(&length), sizeof(length));
        if (!input) {
            break;
        }
        std::u16string formula(length / 2, u'\0');
        input.read(reinterpret_cast<char*>(formula.data()), length);
        if (!input) {
            std::fprintf(stderr, "The corpus %s ends in the middle of a formula.\n", path);
            std::exit(1);
        }
        formulas.push_back(std::move(formula));
    }
    return formulas;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <corpus> [rounds]\n", argv[0]);
        return 1;
    }
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 2;
    const std::vector<std::u16string> formulas = ReadCorpus(argv[1]);

    MathOptions options;
    options.StrictMode = MathStrictMode::Ignore;
    MathWorkspace workspace;
    html::MarkupBuffer out;
    // The first round builds the tables of the symbols, and it is not part of
    // the render of a formula, so the profile does not need it.
    for (int round = 0; round < rounds; ++round) {
        for (const std::u16string& formula : formulas) {
            std::unique_ptr<MathDocument> document;
            MathDiagnostic diagnostic;
            if (!try_compile(formula, options, workspace, document, diagnostic, false)) {
                continue;
            }
            out.Clear();
            document_mathml_into(*document, out, true);
            document_html_into(*document, out);
        }
    }
    std::printf("trained on %zu formulas x %d rounds\n", formulas.size(), rounds);
    return 0;
}
