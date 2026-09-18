// Shared oracle check for the harnesses: a failed oracle aborts (what libFuzzer detects) after
// saying which one, so a reproducer is diagnosable without a debugger.
#pragma once
#include <cstdio>
#include <cstdlib>

#define VCODEC_FUZZ_CHECK(cond, what) \
    do { if (!(cond)) { std::fprintf(stderr, "fuzz oracle failed: %s (%s:%d)\n", what, __FILE__, __LINE__); std::abort(); } } while (0)
