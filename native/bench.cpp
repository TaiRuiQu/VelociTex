#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

extern "C" int vt_mathml2(const char*, int, char*, int, int);

int main(int argc, char** argv)
{
    const char* file = argc > 1 ? argv[1] : "artifacts/native/corpus-lines.txt";
    int iterations = argc > 2 ? std::atoi(argv[2]) : 10000;
    std::ifstream input(file);
    std::vector<std::string> formulas;
    std::string line;
    while (std::getline(input, line)) if (!line.empty()) formulas.push_back(line);
    if (formulas.empty()) { std::fprintf(stderr, "no formulas in %s\n", file); return 2; }
    std::vector<char> output(65536);
    for (int w = 0; w < 1000; ++w)
        for (const auto& formula : formulas)
            vt_mathml2(formula.data(), static_cast<int>(formula.size()), output.data(), static_cast<int>(output.size()), 1);
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations) * formulas.size());
    auto begin = std::chrono::steady_clock::now();
    long long checksum = 0;
    for (int i = 0; i < iterations; ++i)
        for (const auto& formula : formulas)
        {
            auto start = std::chrono::steady_clock::now();
            int size = vt_mathml2(formula.data(), static_cast<int>(formula.size()), output.data(), static_cast<int>(output.size()), 1);
            auto stop = std::chrono::steady_clock::now();
            if (size < 0) { std::fprintf(stderr, "unsupported: %s\n", formula.c_str()); return 3; }
            checksum += size;
            samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        }
    auto end = std::chrono::steady_clock::now();
    std::sort(samples.begin(), samples.end());
    double total = std::chrono::duration<double, std::micro>(end - begin).count();
    std::printf("formulas=%zu iterations=%d calls=%zu total=%.1fms mean=%.4fus p50=%.4fus p95=%.4fus p99=%.4fus checksum=%lld\n",
        formulas.size(), iterations, samples.size(), total / 1000.0, total / samples.size(),
        samples[samples.size() / 2], samples[samples.size() * 95 / 100], samples[samples.size() * 99 / 100], checksum);
    return 0;
}
