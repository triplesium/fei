#include "profiling/profiling.hpp"

#include "frame_profile_accumulator.hpp"

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
#    include "frame_profile_history.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
#    include <cstdlib>
#    include <filesystem>
#    include <fstream>
#    include <utility>
#    include <vector>
#endif

namespace ets {
namespace {

#if defined(ETS_ENABLE_PROFILE_SUMMARY)

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
    std::uint64_t system_id = 0;
    std::string schedule_name;
    ProfileSymbolRef symbol;
    std::string name;
    std::string file;
    std::string function;
    std::uint32_t line = 0;
    ProfileStats stats;
};

struct FrameProfileRecord {
    std::string key;
    ProfileStats stats;
};

struct FrameProfileDetail {
    std::uint64_t frame {0};
    std::int64_t duration_ns {0};
    std::vector<FrameProfileRecord> records;
};

#endif

struct ProfileState {
    struct GpuRecord {
        std::string name;
        std::uint64_t count {0};
        std::uint64_t latest_ns {0};
        std::uint64_t total_ns {0};
        std::uint64_t min_ns {std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t max_ns {0};
    };

    std::mutex mutex;
    std::unordered_map<std::uint64_t, std::string> schedule_names;
    profiling_detail::FrameProfileAccumulator frame_stats;
    std::unordered_map<std::string, GpuRecord> gpu_records;
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    std::unordered_map<std::string, ProfileRecord> records;
    std::unordered_map<std::string, ProfileStats> current_frame_records;
    profiling_detail::FrameProfileHistory frame_history;
    std::deque<FrameProfileDetail> frame_details;
    std::atomic<bool> capture_recording {true};
    std::uint64_t capture_frame_limit {0};
    std::uint64_t capture_frames_remaining {0};
#    if defined(ETS_PROFILE_OUTPUT_PATH)
    std::string output_directory = ETS_PROFILE_OUTPUT_PATH;
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

#if defined(ETS_ENABLE_PROFILE_SUMMARY)

struct ActiveProfileScope {
    std::int64_t start_ns = 0;
    std::int64_t child_ns = 0;
};

thread_local std::vector<ActiveProfileScope> active_scopes;

std::string profile_record_key(
    ProfileZoneKind kind,
    std::uint64_t schedule_id,
    std::uint64_t system_id,
    std::string_view name,
    std::string_view file,
    std::uint32_t line
) {
    if (kind == ProfileZoneKind::System) {
        return std::to_string(static_cast<unsigned>(kind)) + '|' +
               std::to_string(schedule_id) + '|' + std::to_string(system_id);
    }

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
        .system_id = record.system_id,
        .schedule_name = record.schedule_name,
        .symbol = record.symbol,
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
    out << "schedule_id,system_id,schedule,system,symbol_kind,symbol_module,"
           "symbol_id,total_ms,self_ms,count,mean_ms,self_mean_ms,min_ms,"
           "max_ms,file,line,function\n";

    for (const auto& record : records) {
        out << record.schedule_id << ',' << record.system_id << ','
            << escape_csv(record.schedule_name) << ','
            << escape_csv(record.name) << ','
            << profile_symbol_kind_name(record.symbol.kind) << ','
            << escape_csv(record.symbol.module_id) << ',' << record.symbol.value
            << ',' << record.total_ms << ',' << record.self_ms << ','
            << record.count << ',' << record.mean_ms << ','
            << record.self_mean_ms << ',' << record.min_ms << ','
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
    std::uint64_t system_id,
    const ProfileSymbolRef* symbol,
    std::string_view name,
    std::string_view file,
    std::string_view function,
    std::uint32_t line,
    std::int64_t total_ns,
    std::int64_t self_ns
) {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    auto key =
        profile_record_key(kind, schedule_id, system_id, name, file, line);
    auto [it, inserted] = state.records.try_emplace(key);
    if (inserted) {
        it->second.kind = kind;
        it->second.schedule_id = schedule_id;
        it->second.system_id = system_id;
        if (symbol) {
            it->second.symbol = *symbol;
        }
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
    state.current_frame_records[key].add(total_ns, self_ns);
}

FrameProfileDetail take_frame_detail(
    ProfileState& state,
    std::uint64_t frame,
    std::int64_t duration_ns
) {
    FrameProfileDetail detail {
        .frame = frame,
        .duration_ns = duration_ns,
    };
    detail.records.reserve(state.current_frame_records.size());
    for (auto& [key, stats] : state.current_frame_records) {
        detail.records.push_back(
            FrameProfileRecord {
                .key = key,
                .stats = stats,
            }
        );
    }
    state.current_frame_records.clear();
    return detail;
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
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    ensure_profile_summary_atexit();
#endif

    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    auto duration = state.frame_stats.mark(profile_now_ns());
    if (!duration) {
        state.current_frame_records.clear();
        return;
    }
    if (state.capture_recording.load(std::memory_order_relaxed)) {
        const auto frame = state.frame_history.push(*duration);
        if (state.frame_details.size() >=
            profiling_detail::FrameProfileHistory::Capacity) {
            state.frame_details.pop_front();
        }
        state.frame_details.push_back(
            take_frame_detail(state, frame, *duration)
        );
        if (state.capture_frames_remaining > 0) {
            --state.capture_frames_remaining;
            if (state.capture_frames_remaining == 0) {
                state.capture_recording.store(false, std::memory_order_relaxed);
            }
        }
    }
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
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
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

ProfileFrameDetailsSnapshot
profile_frame_details_snapshot(const std::vector<std::uint64_t>& frames) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    ProfileFrameDetailsSnapshot snapshot {.available = true};
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    snapshot.details.reserve(frames.size());
    for (const auto frame : frames) {
        auto detail_it = std::ranges::find(
            state.frame_details,
            frame,
            &FrameProfileDetail::frame
        );
        if (detail_it == state.frame_details.end()) {
            continue;
        }

        ProfileFrameDetailSnapshot detail {
            .frame = detail_it->frame,
            .duration_ms = ns_to_ms(detail_it->duration_ns),
        };
        detail.systems.reserve(detail_it->records.size());
        detail.zones.reserve(detail_it->records.size());
        for (const auto& frame_record : detail_it->records) {
            const auto record_it = state.records.find(frame_record.key);
            if (record_it == state.records.end()) {
                continue;
            }
            auto record = record_it->second;
            record.stats = frame_record.stats;
            auto entry = make_profile_entry(record);
            if (record.kind == ProfileZoneKind::System) {
                detail.systems.push_back(std::move(entry));
            } else {
                detail.zones.push_back(std::move(entry));
            }
        }
        sort_profile_entries(detail.systems);
        sort_profile_entries(detail.zones);
        snapshot.details.push_back(std::move(detail));
    }
    return snapshot;
#else
    (void)frames;
    return {};
#endif
}

ProfileCaptureStatus profile_capture_status() {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    return ProfileCaptureStatus {
        .available = true,
        .recording = state.capture_recording.load(std::memory_order_relaxed),
        .bounded = state.capture_frame_limit > 0,
        .frame_limit = state.capture_frame_limit,
        .frames_remaining = state.capture_frames_remaining,
    };
#else
    return {};
#endif
}

void start_profile_capture(std::uint64_t frame_limit) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    ensure_profile_summary_atexit();
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.frame_stats.clear();
    state.records.clear();
    state.current_frame_records.clear();
    state.frame_history.clear();
    state.frame_details.clear();
    state.capture_frame_limit = frame_limit;
    state.capture_frames_remaining = frame_limit;
    state.capture_recording.store(true, std::memory_order_relaxed);
#else
    (void)frame_limit;
#endif
}

void stop_profile_capture() {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    profile_state().capture_recording.store(false, std::memory_order_relaxed);
#endif
}

void clear_profile_frame_stats() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.frame_stats.clear();
}

void flush_profile_summary() {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
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
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    state.records.clear();
    state.current_frame_records.clear();
    state.frame_history.clear();
    state.frame_details.clear();
#endif
}

void set_profile_summary_output_directory(std::string path) {
#if defined(ETS_ENABLE_PROFILE_SUMMARY)
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.output_directory = std::move(path);
#else
    (void)path;
#endif
}

void record_gpu_profile_duration(
    std::string_view name,
    std::uint64_t duration_ns
) {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    auto [it, inserted] = state.gpu_records.try_emplace(std::string(name));
    auto& record = it->second;
    if (inserted) {
        record.name = std::string(name);
    }
    ++record.count;
    record.latest_ns = duration_ns;
    record.total_ns += duration_ns;
    record.min_ns = std::min(record.min_ns, duration_ns);
    record.max_ns = std::max(record.max_ns, duration_ns);
}

GpuProfileSummarySnapshot gpu_profile_summary_snapshot() {
    GpuProfileSummarySnapshot snapshot;
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    snapshot.available = !state.gpu_records.empty();
    snapshot.entries.reserve(state.gpu_records.size());
    for (const auto& [_, record] : state.gpu_records) {
        if (record.count == 0) {
            continue;
        }
        constexpr double nanoseconds_per_millisecond = 1'000'000.0;
        snapshot.entries.push_back(
            GpuProfileEntrySnapshot {
                .name = record.name,
                .count = record.count,
                .latest_ms = static_cast<double>(record.latest_ns) /
                             nanoseconds_per_millisecond,
                .total_ms = static_cast<double>(record.total_ns) /
                            nanoseconds_per_millisecond,
                .mean_ms = static_cast<double>(record.total_ns) /
                           static_cast<double>(record.count) /
                           nanoseconds_per_millisecond,
                .min_ms = static_cast<double>(record.min_ns) /
                          nanoseconds_per_millisecond,
                .max_ms = static_cast<double>(record.max_ns) /
                          nanoseconds_per_millisecond,
            }
        );
    }
    std::ranges::sort(snapshot.entries, [](const auto& lhs, const auto& rhs) {
        return lhs.name < rhs.name;
    });
    return snapshot;
}

void clear_gpu_profile_summary() {
    auto& state = profile_state();
    std::scoped_lock lock(state.mutex);
    state.gpu_records.clear();
}

#if defined(ETS_ENABLE_PROFILE_SUMMARY)

SummaryProfileScope::SummaryProfileScope(
    ProfileZoneKind kind,
    std::uint64_t schedule_id,
    std::uint64_t system_id,
    const ProfileSymbolRef* symbol,
    std::string_view name,
    std::string_view file,
    std::string_view function,
    std::uint32_t line
) :
    m_kind(kind), m_schedule_id(schedule_id), m_system_id(system_id),
    m_symbol(symbol), m_name(name), m_file(file), m_function(function),
    m_line(line) {
    if (!profile_state().capture_recording.load(std::memory_order_relaxed)) {
        return;
    }
    ensure_profile_summary_atexit();
    m_active = true;
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
        m_system_id,
        m_symbol,
        m_name,
        m_file,
        m_function,
        m_line,
        total_ns,
        self_ns
    );
}

#endif

} // namespace ets
