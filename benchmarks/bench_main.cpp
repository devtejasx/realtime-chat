// Benchmark entry point.
//
// A hand-written main (rather than BENCHMARK_MAIN()) so logging can be silenced
// first. Several benchmarked paths log on failure, and log I/O inside a timed loop
// would dominate the measurement and make results depend on terminal speed.
#include <benchmark/benchmark.h>

#include "rtc/logging/logger.hpp"

int main(int argc, char** argv) {
    // "off" rather than "error": even an occasional line skews a tight loop.
    rtc::logging::init("off", "text");

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    rtc::logging::shutdown();
    return 0;
}
