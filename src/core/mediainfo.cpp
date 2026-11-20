// SPDX-License-Identifier: BSD-3-Clause
#include "core/mediainfo.h"

#include "base/json.h"
#include "base/paths.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fm::media {
namespace {

using cutefish::base::Process;
constexpr double kMaxDuration = 7 * 24 * 60 * 60;

// Only the already-open local regular file is readable by the demuxer. No
// playlists, network URLs, device inputs or external file references.
bool startInput(Process& process, const char* program, const std::string& path,
                std::vector<std::string> options, std::vector<std::string> output, bool probe = false)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(fd);
        return false;
    }
    options.insert(options.end(), {"-protocol_whitelist", "fd", "-format_whitelist",
        "mov,mp3,flac,wav,ogg,matroska,webm,aac,aiff,avi,mpeg,mpegts,asf,flv,ape,wv,ac3,amr",
        "-fd", std::to_string(Process::kInheritedFd)});
    if (!probe)
        options.push_back("-i");
    options.push_back("fd:");
    options.insert(options.end(), output.begin(), output.end());
    process.setInheritedFd(fd);
    const bool started = process.start(program, options);
    process.setInheritedFd(-1);
    ::close(fd);
    return started;
}

template<class Number>
Number number(std::string_view text)
{
    Number value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size() ? value : 0;
}

// FFmpeg's time arguments must use a decimal point in every session locale.
std::string timeArg(double seconds)
{
    char value[32];
    const auto result = std::to_chars(value, value + sizeof(value), seconds, std::chars_format::fixed, 6);
    return {value, result.ptr};
}

bool validRange(double from, double to)
{
    return std::isfinite(from) && std::isfinite(to) && from >= 0 && to > from && to <= kMaxDuration;
}

} // namespace

bool canTrim()
{
    return !cutefish::base::paths::findExecutable("ffmpeg").empty();
}

bool canPlay()
{
    return !cutefish::base::paths::findExecutable("ffplay").empty();
}

bool probe(Process& process, const std::string& path)
{
    return startInput(process, "ffprobe", path, {"-v", "error", "-show_entries",
        "format=duration:stream=codec_type,width,height,sample_rate:stream_disposition=attached_pic", "-of", "json"}, {}, true);
}

Info parseInfo(std::string_view data)
{
    const auto json = cutefish::base::json::Value::parse(data);
    Info info;
    if (!json)
        return info;
    const double duration = number<double>((*json)["format"]["duration"].toString());
    if (std::isfinite(duration) && duration > 0 && duration <= kMaxDuration)
        info.duration = duration;
    for (const auto& stream : (*json)["streams"].toArray()) {
        const auto type = stream["codec_type"].toString();
        if (type == "video" && !info.width && !stream["disposition"]["attached_pic"].toInt()) {
            info.width = int(stream["width"].toInt());
            info.height = int(stream["height"].toInt());
        } else if (type == "audio" && !info.sampleRate) {
            info.sampleRate = number<int>(stream["sample_rate"].toString());
        }
    }
    return info;
}

std::string clock(double seconds, bool precise)
{
    if (!std::isfinite(seconds))
        return {};
    const auto ms = std::llround(std::clamp(seconds, 0.0, kMaxDuration) * 1000);
    const auto hours = ms / 3600000;
    const auto minutes = ms / 60000 % 60;
    const auto whole = ms / 1000 % 60;
    char text[40];
    if (hours)
        std::snprintf(text, sizeof(text), "%02lld:%02lld:%02lld", hours, minutes, whole);
    else
        std::snprintf(text, sizeof(text), "%02lld:%02lld", minutes, whole);
    if (precise)
        return std::string(text) + '.' + std::to_string(1000 + ms % 1000).substr(1);
    return text;
}

bool waveform(Process& process, const std::string& path, double duration)
{
    if (!validRange(0, duration))
        return false;
    const int rate = std::clamp(int(1800000 / duration), 6, 4000);
    return startInput(process, "ffmpeg", path, {"-nostdin", "-v", "error"},
        {"-map", "0:a:0", "-vn", "-ac", "1", "-ar", std::to_string(rate), "-t", timeArg(duration),
         "-fs", "8000000", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"});
}

std::vector<float> peaks(std::string_view pcm, size_t buckets)
{
    const size_t samples = pcm.size() / 2;
    if (!samples || !buckets)
        return {};
    std::vector<float> result(buckets);
    for (size_t i = 0; i < samples; ++i) {
        const auto word = uint16_t(uint8_t(pcm[i * 2])) | (uint16_t(uint8_t(pcm[i * 2 + 1])) << 8);
        const float amplitude = float(std::abs(int(int16_t(word)))) / 32768;
        auto& bucket = result[i * buckets / samples];
        bucket = std::max(bucket, amplitude);
    }
    return result;
}

bool play(Process& process, const std::string& path, double start, double end)
{
    return validRange(start, end) && startInput(process, "ffplay", path,
        {"-loglevel", "error", "-nodisp", "-autoexit", "-vn", "-ss", timeArg(start), "-t", timeArg(end - start)}, {});
}

TrimOutput::TrimOutput(std::string source) : m_source(std::move(source)) {}

TrimOutput::~TrimOutput()
{
    if (!m_temp.empty())
        ::unlink(m_temp.c_str());
    if (!m_dir.empty())
        ::rmdir(m_dir.c_str());
}

bool TrimOutput::start(Process& process, double from, double to)
{
    if (!validRange(from, to) || !m_dir.empty())
        return false;
    std::string temp = (std::filesystem::path(m_source).parent_path() / ".cutefish-trim-XXXXXX").string();
    if (!::mkdtemp(temp.data()))
        return false;
    m_dir = std::move(temp);
    m_temp = m_dir + "/audio.wav";
    // Fast input seek plus accurate decoding (not packet copying), and an
    // explicit mix-free PCM codec. Preserve the source channel layout/rate.
    return startInput(process, "ffmpeg", m_source, {"-nostdin", "-v", "error", "-ss", timeArg(from)},
        {"-map", "0:a:0", "-vn", "-t", timeArg(to - from), "-map_metadata", "-1", "-c:a", "pcm_s24le",
         "-rf64", "auto", "-f", "wav", "file:" + m_temp});
}

std::string TrimOutput::publish()
{
    struct stat st {};
    if (m_temp.empty() || ::lstat(m_temp.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 104)
        return {};
    const std::filesystem::path source(m_source);
    const auto stem = source.stem().string();
    for (int sequence = 1; sequence < 10000; ++sequence) {
        const auto name = stem + " (" + (sequence == 1 ? "trimmed" : "trimmed " + std::to_string(sequence)) + ").wav";
        const auto target = (source.parent_path() / name).string();
        if (::link(m_temp.c_str(), target.c_str()) == 0)
            return target;
        if (errno != EEXIST)
            break;
    }
    return {};
}

} // namespace fm::media
