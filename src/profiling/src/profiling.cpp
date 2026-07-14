#include "profiling/profiling.hpp"

#include "frame_profile_accumulator.hpp"

#if defined(FEI_ENABLE_PROFILE_SUMMARY)
#    include "frame_profile_history.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#if defined(FEI_ENABLE_PROFILE_SUMMARY)
#    include <algorithm>
#    include <cstdlib>
#    include <filesystem>
#    include <fstream>
#    include <limits>
#    include <utility>
#    include <vector>
#endif

namespace fei {
namespace {

#if defined(FEI_ENABLE_PROFILE_SUMMARY)

struct ProfileStats {
    std::uint64_t count = 0;
    std::int64_t total_ns = 0;
    std::int64_t self_ns = 0;
    std::int64_t min_ns = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_ns = 0;

    void add(std::int64_t total, std::int64_t self) {
        ++count;
        total_ns += total;
        self_ns += self;
        min_ns = std::min(min_ns, total);
        max_ns = std::max(max_ns, total);
    }
};

struct ProfileRecord {
    ProfileZoneKind kind = ProfileZoneKind::Generic;
    std::uint64_t schedule_id = 0;
    std::string schedule_name;
    std::string name;
    std::string file;
    std::string function;
    std::uint32_t line = 0;
    ProfileStats stats;
};

#endif

struct ProfileState {
    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::string> schedule_names;
    profiling_detail::FrameProfileAccumulator frame_stats;
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    std::unordered_map<std::string, ProfileRecord> records;
    profiling_detail::FrameProfileHistory frame_history;
#    if defined(FEI_PROFILE_OUTPUT_PATH)
    std::string output_directory = FEI_PROFILE_OUTPUT_PATH;
#    else
    std::string output_directory = "build/profile/latest";
#    endif
    bool atexit_registered = false;
#endif
};

ProfileState& profile_state() {
    static ProfileState state;
    return state;
}

#if defined(FEI_ENABLE_PROFILE_SUMMARY)

struct ActiveProfileScope {
    std::int64_t start_ns = 0;
    std::int64_t child_ns = 0;
};

thread_local std::vector<ActiveProfileScope> active_scopes;

std::string profile_record_key(
    ProfileZoneKind kind,
    std::uint64_t schedule_id,
    std::string_view name,
    std::string_view file,
    std::uint32_t line
) {
    std::string key;
    key.reserve(name.size() + file.size() + 64);
    key += std::to_string(static_cast<unsigned>(kind));
    key += '|';
    key += std::to_string(schedule_id);
    key += '|';
    key += name;
    key += '|';
    key += file;
    key += '|';
    key += std::to_string(line);
    return key;
}

void ensure_profile_summary_atexit() {
    auto& state = profile_state();
    if (state.atexit_registered) {
        return;
    }
    state.atexit_registered = true;
    std::atexit(flush_profile_summary);
}

std::string escape_csv(std::string_view value) {
    bool needs_quotes = false;
    for (auto ch : value) {
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (auto ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

double ns_to_ms(std::int64_t ns) {
    return static_cast<double>(ns) / 1'000'000.0;
}

struct RawProfileSummary {
    FrameProfileStats frame_stats;
    std::vector<ProfileRecord> records;
    std::vector<profiling_detail::FrameProfileHistorySample> frames;
};

RawProfileSummary copy_profile_summary() {
    RawProfileSummary result;
    auto& state = profile_state();
    {
        std::scoped_lock lock(state.mutex);
        result.frame_stats = state.frame_stats.stats();
        result.records.reserve(state.records.size());
        for (const auto& [_, record] : state.records) {
            if (record.stats.count > 0) {
                result.records.push_back(record);
            }
        }
        result.frames = state.frame_history.samples();
    }
    return result;
}

ProfileEntrySnapshot make_profile_entry(const ProfileRecord& record) {
    const auto count = static_cast<double>(record.stats.count);
    return ProfileEntrySnapshot {
        .kind = record.kind,
        .schedule_id = record.schedule_id,
        .schedule_name = record.schedule_name,
        .name = record.name,
        .file = record.file,
        .function = record.function,
        .line = record.line,
        .count = record.stats.count,
        .total_ms = ns_to_ms(record.stats.total_ns),
        .self_ms = ns_to_ms(record.stats.self_ns),
        .mean_ms = ns_to_ms(record.stats.total_ns) / count,
        .self_mean_ms = ns_to_ms(record.stats.self_ns) / count,
        .min_ms = ns_to_ms(record.stats.min_ns),
        .max_ms = ns_to_ms(record.stats.max_ns),
    };
}

void sort_profile_entries(std::vector<ProfileEntrySnapshot>& entries) {
    std::ranges::sort(entries, [](const auto& lhs, const auto& rhs) {
        if (lhs.total_ms == rhs.total_ms) {
            return lhs.name < rhs.name;
        }
        return lhs.total_ms > rhs.total_ms;
    });
}

void write_system_records(
    const std::filesystem::path& path,
    const std::vector<ProfileEntrySnapshot>& records
) {
    std::ofstream out(path);
    out << "schedule,system,total_ms,self_ms,count,mean_ms,self_mean_ms,min_ms,"
           "max_ms,file,line,function\n";

    for (const auto& record : records) {
        out << escape_csv(record.schedule_name) << ','
            << escape_csv(record.name) << ',' << record.total_ms << ','
            << record.self_ms << ',' << record.count << ',' << record.mean_ms
            << ',' << record.self_mean_ms << ',' << record.min_ms << ','
            << record.max_ms << ',' << escape_csv(record.file) << ','
            << record.line << ',' << escape_csv(record.function) << '\n';
    }
}

void write_zone_records(
    const std::filesystem::path& path,
    const std::vector<ProfileEntrySnapshot>& records
) {
    std::ofstream out(path);
    out << "zone,total_ms,self_ms,count,mean_ms,self_mean_ms,min_ms,max_ms,"
           "file,line,function\n";

    for (const auto& record : records) {
        out << escape_csv(record.name) << ',' << record.total_ms << ','
            << record.self_ms << ',' << record.count << ',' << record.mean_ms
            << ',' << record.self_mean_ms << ',' << record.min_ms << ','
            << record.max_ms << ',' << escape_csv(record.file) << ','
            << record.line << ',' << escape_csv(record.function) << '\n';
    }
}

void write_frame_records(
    const std::filesystem::path& path,
    const std::vector<ProfileFrameSample>& frames
) {
    std::ofstream out(path);
    out << "frame,duration_ms\n";
    for (const auto& frame : frames) {
        out << frame.frame << ',' << frame.duration_ms << '\n';
    }
}

void record_profile_scope(
    ProfileZoneKind kind,
    std::uint64_t schedule_id,
    std::string_view name,
    std::string_view file,
    std::string_view function,
    std::uint32_t line,
    std::int64_t total_ns,
    std::int64_t self_ns
) {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    auto key = profile_record_key(kind, schedule_id, name, file, line);
    auto [it, inserted] = state.records.try_emplace(key);
    if (inserted) {
        it->second.kind = kind;
        it->second.schedule_id = schedule_id;
        if (kind == ProfileZoneKind::System) {
            auto schedule_it = state.schedule_names.find(schedule_id);
            it->second.schedule_name =
                schedule_it != state.schedule_names.end() ?
                    schedule_it->second :
                    "schedule#" + std::to_string(schedule_id);
        }
        it->second.name = std::string(name);
        it->second.file = std::string(file);
        it->second.function = std::string(function);
        it->second.line = line;
    }
    it->second.stats.add(total_ns, self_ns);
}

#endif

std::int64_t profile_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()
    )
        .count();
}

} // namespace

void register_profile_schedule_name(
    std::uint64_t schedule_id,
    std::string_view name
) {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.schedule_names[schedule_id] = std::string(name);
}

void clear_profile_schedule_names() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.schedule_names.clear();
}

std::string profile_schedule_name(std::uint64_t schedule_id) {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    auto it = state.schedule_names.find(schedule_id);
    if (it != state.schedule_names.end()) {
        return it->second;
    }

    auto [fallback_it, _] = state.schedule_names.emplace(
        schedule_id,
        "schedule#" + std::to_string(schedule_id)
    );
    return fallback_it->second;
}

void profile_frame_mark() {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    ensure_profile_summary_atexit();
#endif

    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    auto duration = state.frame_stats.mark(profile_now_ns());
    if (!duration) {
        return;
    }
    state.frame_history.push(*duration);
#else
    (void)state.frame_stats.mark(profile_now_ns());
#endif
}

FrameProfileStats profile_frame_stats() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    return state.frame_stats.stats();
}

ProfileSummarySnapshot profile_summary_snapshot() {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    auto raw = copy_profile_summary();
    ProfileSummarySnapshot snapshot {
        .available = true,
        .frame_stats = raw.frame_stats,
    };
    snapshot.systems.reserve(raw.records.size());
    snapshot.zones.reserve(raw.records.size());
    for (const auto& record : raw.records) {
        auto entry = make_profile_entry(record);
        if (record.kind == ProfileZoneKind::System) {
            snapshot.systems.push_back(std::move(entry));
        } else {
            snapshot.zones.push_back(std::move(entry));
        }
    }
    sort_profile_entries(snapshot.systems);
    sort_profile_entries(snapshot.zones);

    snapshot.frames.reserve(raw.frames.size());
    for (const auto& frame : raw.frames) {
        snapshot.frames.push_back(
            ProfileFrameSample {
                .frame = frame.frame,
                .duration_ms = ns_to_ms(frame.duration_ns),
            }
        );
    }
    return snapshot;
#else
    return ProfileSummarySnapshot {
        .frame_stats = profile_frame_stats(),
    };
#endif
}

void clear_profile_frame_stats() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.frame_stats.clear();
}

void flush_profile_summary() {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    auto snapshot = profile_summary_snapshot();
    auto& state = profile_state();
    std::filesystem::path output_directory;
    {
        std::scoped_lock lock(state.mutex);
        output_directory = state.output_directory;
    }

    std::filesystem::create_directories(output_directory);
    write_system_records(output_directory / "systems.csv", snapshot.systems);
    write_zone_records(output_directory / "zones.csv", snapshot.zones);
    write_frame_records(output_directory / "frames.csv", snapshot.frames);
#endif
}

void clear_profile_summary() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.frame_stats.clear();
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    state.records.clear();
    state.frame_history.clear();
#endif
}

void set_profile_summary_output_directory(std::string path) {
#if defined(FEI_ENABLE_PROFILE_SUMMARY)
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.output_directory = std::move(path);
#else
    (void)path;
#endif
}

#if defined(FEI_ENABLE_PROFILE_SUMMARY)

SummaryProfileScope::SummaryProfileScope(
    ProfileZoneKind kind,
    std::uint64_t schedule_id,
    std::string_view name,
    std::string_view file,
    std::string_view function,
    std::uint32_t line
) :
    m_kind(kind), m_schedule_id(schedule_id), m_name(name), m_file(file),
    m_function(function), m_line(line), m_active(true) {
    ensure_profile_summary_atexit();
    active_scopes.push_back(
        ActiveProfileScope {
            .start_ns = profile_now_ns(),
            .child_ns = 0,
        }
    );
}

SummaryProfileScope::~SummaryProfileScope() {
    if (!m_active || active_scopes.empty()) {
        return;
    }

    const auto now = profile_now_ns();
    auto active = active_scopes.back();
    active_scopes.pop_back();

    const auto total_ns = now - active.start_ns;
    const auto self_ns = std::max<std::int64_t>(0, total_ns - active.child_ns);
    if (!active_scopes.empty()) {
        active_scopes.back().child_ns += total_ns;
    }

    record_profile_scope(
        m_kind,
        m_schedule_id,
        m_name,
        m_file,
        m_function,
        m_line,
        total_ns,
        self_ns
    );
}

#endif

} // namespace fei
