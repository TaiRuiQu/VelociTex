// Standalone native C++ public-ABI benchmark: no CLR, DOM, GPU or window.
#include "../native/core/api.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

struct Formula {
    std::u16string source;
    bool display;
};
struct Unit {
    std::string workload, id;
    std::vector<Formula> formulas;
};
#include "native-workloads.inc"

using Clock = std::chrono::steady_clock;
static double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

int main(int argc, char** argv) {
    try {
        if (argc != 5)
            throw std::runtime_error("native-core <native.dll> <report.json> <samples> <batch-ms>");
        const int samples = std::stoi(argv[3]);
        const double batchMs = std::stod(argv[4]);
        if (samples < 3 || batchMs < 1)
            throw std::runtime_error("Invalid configuration");
        HMODULE library = LoadLibraryA(argv[1]);
        if (!library)
            throw std::runtime_error("Cannot load native Core DLL");
#define API(name)                                                                                                      \
    auto name = reinterpret_cast<decltype(&vt_core_##name)>(GetProcAddress(library, "vt_core_" #name));                \
    if (!name)                                                                                                         \
    throw std::runtime_error("Missing ABI: " #name)
        API(default_options);
        API(workspace_create);
        API(workspace_destroy);
        API(compile);
        API(result_destroy);
        API(result_mathml);
#undef API
        void* workspace = workspace_create();
        if (!workspace)
            throw std::runtime_error("No workspace");
        std::ofstream output(argv[2]);
        output << std::setprecision(17);
        output << "{\"schemaVersion\":2,\"engine\":\"cpp\",\"workloadSha256\":\"" << workloadSha256
               << "\",\"timedRegion\":\"Native C++ public ABI: fresh parse, MathML serialization, owned UTF-8 string "
                  "copy, result destruction; no CLR, GPU or frame "
                  "wait\",\"result\":{\"config\":{\"samples\":"
               << samples << ",\"batchMs\":" << batchMs << ",\"warmupMs\":200},\"workloads\":{";
        std::string previous;
        bool first = true;
        size_t checksum = 0;
        for (const Unit& unit : units) {
            if (unit.workload != previous) {
                if (!previous.empty())
                    output << "],";
                output << '"' << unit.workload << "\":[";
                previous = unit.workload;
                first = true;
            }
            if (!first)
                output << ',';
            first = false;
            auto operation = [&] {
                for (const Formula& formula : unit.formulas) {
                    vt_options options;
                    default_options(&options);
                    options.DisplayMode = formula.display ? 1 : 0;
                    void* result = nullptr;
                    vt_diagnostic diagnostic{};
                    vt_string16 input{reinterpret_cast<const uint16_t*>(formula.source.data()),
                                      static_cast<int32_t>(formula.source.size())};
                    if (compile(workspace, input, &options, 0, &result, &diagnostic) != 0)
                        throw std::runtime_error(unit.id + ": compile failed");
                    vt_string8 markup{};
                    if (result_mathml(result, 1, &markup) != 0) {
                        result_destroy(result);
                        throw std::runtime_error(unit.id + ": MathML failed");
                    }
                    // A standalone owned string, not merely a borrowed pointer.
                    std::string owned(markup.Data, markup.Length);
                    result_destroy(result);
                    if (owned.size() < 20 || owned.find("<math") == std::string::npos)
                        throw std::runtime_error("Empty MathML");
                    checksum += owned.size();
                }
            };
            auto warm = Clock::now();
            do {
                operation();
            } while (milliseconds(warm) < 200);
            auto batch = [&](int count) {
                auto start = Clock::now();
                for (int i = 0; i < count; ++i)
                    operation();
                return milliseconds(start);
            };
            int count = 1;
            while (batch(count) < batchMs)
                count *= 2;
            std::vector<double> values;
            output << "{\"id\":\"" << unit.id << "\",\"formulaCount\":" << unit.formulas.size()
                   << ",\"mathml\":{\"operationsPerBatch\":" << count << ",\"batches\":[";
            for (int i = 0; i < samples; ++i) {
                double total = batch(count);
                values.push_back(total / count);
                if (i)
                    output << ',';
                output << "{\"operations\":" << count << ",\"totalMs\":" << total
                       << ",\"msPerOperation\":" << total / count << '}';
            }
            std::sort(values.begin(), values.end());
            output << "],\"summary\":{\"medianMs\":" << values[values.size() / 2] << "}}}";
            std::cout << unit.id << ": " << values[values.size() / 2] * 1000 << " us/unit\n";
        }
        output << "]},\"checksum\":" << checksum << "}}\n";
        workspace_destroy(workspace);
        FreeLibrary(library);
        return output.good() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
