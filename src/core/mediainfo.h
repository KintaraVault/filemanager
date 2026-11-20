// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "base/process.h"

#include <string>
#include <string_view>
#include <vector>

namespace fm::media {

struct Info {
    double duration = 0;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
};

bool canTrim();
bool canPlay();
bool probe(cutefish::base::Process& process, const std::string& path);
Info parseInfo(std::string_view data);
std::string clock(double seconds, bool precise = false);

// A bounded, mono signed 16-bit waveform, read in time order.
bool waveform(cutefish::base::Process& process, const std::string& path, double duration);
std::vector<float> peaks(std::string_view pcm, size_t buckets);
bool play(cutefish::base::Process& process, const std::string& path, double start, double end);

// A private temporary file alongside the source; publishing never overwrites
// a file, including the original. Destroying the job removes any partial WAV.
class TrimOutput {
public:
    explicit TrimOutput(std::string source);
    ~TrimOutput();
    TrimOutput(const TrimOutput&) = delete;
    TrimOutput& operator=(const TrimOutput&) = delete;
    bool start(cutefish::base::Process& process, double from, double to);
    std::string publish();

private:
    std::string m_source;
    std::string m_dir;
    std::string m_temp;
};

} // namespace fm::media
