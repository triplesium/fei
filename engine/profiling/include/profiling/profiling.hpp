#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#if defined(ETS_ENABLE_TRACY)
#    include <tracy/Tracy.hpp>
#endif

namespace ets {

enum class ProfileZoneKind : std::uint8_t {
    Generic,
    System,
};

struct FrameProfileStats {
    std::uint64_t frame_count {0};
    double fps {0.0};
    double latest_frame_ms {0.0};
    double average_frame_ms {0.0};
};

struct ProfileEntrySnapshot {
    ProfileZoneKind kind {ProfileZoneKind::Generic};
    std::uint64_t schedule_id {0};
    std::string schedule_name;
    std::string name;
    std::string file;
    std::string function;
    std::uint32_t line {0};
    std::uint64_t count {0};
    double total_ms {0.0};
    double self_ms {0.0};
    double mean_ms {0.0};
    double self_mean_ms {0.0};
    double min_ms {0.0};
    double max_ms {0.0};
};

struct ProfileFrameSample {
    std::uint64_t frame {0};
    double duration_ms {0.0};
};

struct ProfileSummarySnapshot {
    bool available {false};
    FrameProfileStats frame_stats;
    std::vector<ProfileEntrySnapshot> systems;
    std::vector<ProfileEntrySnapshot> zones;
    std::vector<ProfileFrameSample> frames;
};

struct ProfileFrameDetailSnapshot {
    std::uint64_t frame {0};
    double duration_ms {0.0};
    std::vector<ProfileEntrySnapshot> systems;
    std::vector<ProfileEntrySnapshot> zones;
};

struct ProfileFrameDetailsSnapshot {
    bool available {false};
    std::vector<ProfileFrameDetailSnapshot> details;
};

struct ProfileCaptureStatus {
    bool available {false};
    bool recording {false};
    bool bounded {false};
    std::uint64_t frame_limit {0};
    std::uint64_t frames_remaining {0};
};

struct GpuProfileEntrySnapshot {
    std::string name;
    std::uint64_t count {0};
    double latest_ms {0.0};
    double total_ms {0.0};
    double mean_ms {0.0};
    double min_ms {0.0};
    double max_ms {0.0};
};

struct GpuProfileSummarySnapshot {
    bool available {false};
    std::vector<GpuProfileEntrySnapshot> entries;
};

void register_profile_schedule_name(
    std::uint64_t schedule_id,
    std::string_view name
);
void clear_profile_schedule_names();
std::string profile_schedule_name(std::uint64_t schedule_id);

void profile_frame_mark();
FrameProfileStats profile_frame_stats();
ProfileSummarySnapshot profile_summary_snapshot();
ProfileFrameDetailsSnapshot
profile_frame_details_snapshot(const std::vector<std::uint64_t>& frames);
ProfileCaptureStatus profile_capture_status();
void start_profile_capture(std::uint64_t frame_limit = 0);
void stop_profile_capture();
void clear_profile_frame_stats();
void flush_profile_summary();
void clear_profile_summary();
void set_profile_summary_output_directory(std::string path);
void record_gpu_profile_duration(
    std::string_view name,
    std::uint64_t duration_ns
);
GpuProfileSummarySnapshot gpu_profile_summary_snapshot();
void clear_gpu_profile_summary();

#if defined(ETS_ENABLE_TRACY)

class DynamicProfileScope {
  private:
    tracy::ScopedZone m_zone;

  public:
    DynamicProfileScope(
        std::string_view name,
        std::string_view file,
        std::string_view function,
        std::uint32_t line
    ) :
        m_zone(
            line,
            file.data(),
            file.size(),
            function.data(),
            function.size(),
            name.data(),
            name.size(),
            0,
            true
        ) {}
};

#endif

#if defined(ETS_ENABLE_PROFILE_SUMMARY)

class SummaryProfileScope {
  private:
    ProfileZoneKind m_kind;
    std::uint64_t m_schedule_id;
    std::string_view m_name;
    std::string_view m_file;
    std::string_view m_function;
    std::uint32_t m_line;
    bool m_active {false};

  public:
    SummaryProfileScope(
        ProfileZoneKind kind,
        std::uint64_t schedule_id,
        std::string_view name,
        std::string_view file,
        std::string_view function,
        std::uint32_t line
    );
    ~SummaryProfileScope();

    SummaryProfileScope(const SummaryProfileScope&) = delete;
    SummaryProfileScope& operator=(const SummaryProfileScope&) = delete;
    SummaryProfileScope(SummaryProfileScope&&) = delete;
    SummaryProfileScope& operator=(SummaryProfileScope&&) = delete;
};

#endif

} // namespace ets

#define ETS_PROFILE_CONCAT_IMPL(a, b) a##b
#define ETS_PROFILE_CONCAT(a, b) ETS_PROFILE_CONCAT_IMPL(a, b)
#define ETS_PROFILE_UNIQUE_NAME(name) ETS_PROFILE_CONCAT(name, __COUNTER__)

#if defined(ETS_ENABLE_TRACY)
#    define ETS_PROFILE_TRACY_FRAME() FrameMark
#    define ETS_PROFILE_TRACY_SCOPE(name) ZoneScopedN(name);
#    define ETS_PROFILE_TRACY_FUNCTION() ZoneScoped;
#    define ETS_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line) \
        ::ets::DynamicProfileScope ETS_PROFILE_UNIQUE_NAME(             \
            ets_tracy_profile_scope_                                    \
        ) {name, file, function, line};
#else
#    define ETS_PROFILE_TRACY_FRAME()
#    define ETS_PROFILE_TRACY_SCOPE(name)
#    define ETS_PROFILE_TRACY_FUNCTION()
#    define ETS_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line)
#endif

#if defined(ETS_ENABLE_PROFILE_SUMMARY)
#    define ETS_PROFILE_SUMMARY_SCOPE(                      \
        kind,                                               \
        schedule_id,                                        \
        name,                                               \
        file,                                               \
        function,                                           \
        line                                                \
    )                                                       \
        ::ets::SummaryProfileScope ETS_PROFILE_UNIQUE_NAME( \
            ets_summary_profile_scope_                      \
        ) {kind, schedule_id, name, file, function, line};
#else
#    define ETS_PROFILE_SUMMARY_SCOPE( \
        kind,                          \
        schedule_id,                   \
        name,                          \
        file,                          \
        function,                      \
        line                           \
    )
#endif

#define ETS_PROFILE_FRAME()    \
    ETS_PROFILE_TRACY_FRAME(); \
    ::ets::profile_frame_mark()

#define ETS_PROFILE_SCOPE(name)          \
    ETS_PROFILE_TRACY_SCOPE(name)        \
    ETS_PROFILE_SUMMARY_SCOPE(           \
        ::ets::ProfileZoneKind::Generic, \
        0,                               \
        name,                            \
        __FILE__,                        \
        __func__,                        \
        __LINE__                         \
    )

#define ETS_PROFILE_FUNCTION()           \
    ETS_PROFILE_TRACY_FUNCTION()         \
    ETS_PROFILE_SUMMARY_SCOPE(           \
        ::ets::ProfileZoneKind::Generic, \
        0,                               \
        __func__,                        \
        __FILE__,                        \
        __func__,                        \
        __LINE__                         \
    )

#define ETS_PROFILE_DYNAMIC_SCOPE(name, file, function, line)   \
    ETS_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line) \
    ETS_PROFILE_SUMMARY_SCOPE(                                  \
        ::ets::ProfileZoneKind::Generic,                        \
        0,                                                      \
        name,                                                   \
        file,                                                   \
        function,                                               \
        line                                                    \
    )

#define ETS_PROFILE_SYSTEM_SCOPE(schedule_id, profile_info) \
    ETS_PROFILE_TRACY_DYNAMIC_SCOPE(                        \
        (profile_info).name,                                \
        (profile_info).file,                                \
        (profile_info).function,                            \
        (profile_info).line                                 \
    )                                                       \
    ETS_PROFILE_SUMMARY_SCOPE(                              \
        ::ets::ProfileZoneKind::System,                     \
        schedule_id,                                        \
        (profile_info).name,                                \
        (profile_info).file,                                \
        (profile_info).function,                            \
        (profile_info).line                                 \
    )
