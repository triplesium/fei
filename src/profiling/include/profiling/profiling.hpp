#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#if defined(FEI_ENABLE_TRACY)
#    include <tracy/Tracy.hpp>
#endif

namespace fei {

enum class ProfileZoneKind : std::uint8_t {
    Generic,
    System,
};

void register_profile_schedule_name(
    std::uint64_t schedule_id,
    std::string_view name
);
void clear_profile_schedule_names();
std::string profile_schedule_name(std::uint64_t schedule_id);

void profile_frame_mark();
void flush_profile_summary();
void clear_profile_summary();
void set_profile_summary_output_directory(std::string path);

#if defined(FEI_ENABLE_TRACY)

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

#if defined(FEI_ENABLE_PROFILE_SUMMARY)

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

} // namespace fei

#define FEI_PROFILE_CONCAT_IMPL(a, b) a##b
#define FEI_PROFILE_CONCAT(a, b) FEI_PROFILE_CONCAT_IMPL(a, b)
#define FEI_PROFILE_UNIQUE_NAME(name) FEI_PROFILE_CONCAT(name, __COUNTER__)

#if defined(FEI_ENABLE_TRACY)
#    define FEI_PROFILE_TRACY_FRAME() FrameMark
#    define FEI_PROFILE_TRACY_SCOPE(name) ZoneScopedN(name);
#    define FEI_PROFILE_TRACY_FUNCTION() ZoneScoped;
#    define FEI_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line) \
        ::fei::DynamicProfileScope FEI_PROFILE_UNIQUE_NAME(             \
            fei_tracy_profile_scope_                                    \
        ) {name, file, function, line};
#else
#    define FEI_PROFILE_TRACY_FRAME()
#    define FEI_PROFILE_TRACY_SCOPE(name)
#    define FEI_PROFILE_TRACY_FUNCTION()
#    define FEI_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line)
#endif

#if defined(FEI_ENABLE_PROFILE_SUMMARY)
#    define FEI_PROFILE_SUMMARY_FRAME() ::fei::profile_frame_mark()
#    define FEI_PROFILE_SUMMARY_SCOPE(                      \
        kind,                                               \
        schedule_id,                                        \
        name,                                               \
        file,                                               \
        function,                                           \
        line                                                \
    )                                                       \
        ::fei::SummaryProfileScope FEI_PROFILE_UNIQUE_NAME( \
            fei_summary_profile_scope_                      \
        ) {kind, schedule_id, name, file, function, line};
#else
#    define FEI_PROFILE_SUMMARY_FRAME()
#    define FEI_PROFILE_SUMMARY_SCOPE( \
        kind,                          \
        schedule_id,                   \
        name,                          \
        file,                          \
        function,                      \
        line                           \
    )
#endif

#define FEI_PROFILE_FRAME()    \
    FEI_PROFILE_TRACY_FRAME(); \
    FEI_PROFILE_SUMMARY_FRAME()

#define FEI_PROFILE_SCOPE(name)          \
    FEI_PROFILE_TRACY_SCOPE(name)        \
    FEI_PROFILE_SUMMARY_SCOPE(           \
        ::fei::ProfileZoneKind::Generic, \
        0,                               \
        name,                            \
        __FILE__,                        \
        __func__,                        \
        __LINE__                         \
    )

#define FEI_PROFILE_FUNCTION()           \
    FEI_PROFILE_TRACY_FUNCTION()         \
    FEI_PROFILE_SUMMARY_SCOPE(           \
        ::fei::ProfileZoneKind::Generic, \
        0,                               \
        __func__,                        \
        __FILE__,                        \
        __func__,                        \
        __LINE__                         \
    )

#define FEI_PROFILE_DYNAMIC_SCOPE(name, file, function, line)   \
    FEI_PROFILE_TRACY_DYNAMIC_SCOPE(name, file, function, line) \
    FEI_PROFILE_SUMMARY_SCOPE(                                  \
        ::fei::ProfileZoneKind::Generic,                        \
        0,                                                      \
        name,                                                   \
        file,                                                   \
        function,                                               \
        line                                                    \
    )

#define FEI_PROFILE_SYSTEM_SCOPE(schedule_id, profile_info) \
    FEI_PROFILE_TRACY_DYNAMIC_SCOPE(                        \
        (profile_info).name,                                \
        (profile_info).file,                                \
        (profile_info).function,                            \
        (profile_info).line                                 \
    )                                                       \
    FEI_PROFILE_SUMMARY_SCOPE(                              \
        ::fei::ProfileZoneKind::System,                     \
        schedule_id,                                        \
        (profile_info).name,                                \
        (profile_info).file,                                \
        (profile_info).function,                            \
        (profile_info).line                                 \
    )
