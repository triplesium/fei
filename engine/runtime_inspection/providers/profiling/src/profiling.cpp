#include "runtime_inspection_profiling/profiling.hpp"

#include "ecs/world.hpp"

#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets::runtime_inspection::profiling {
namespace {

using Json = nlohmann::json;

InspectionError invalid_request(std::string message) {
    return InspectionError {
        .kind = InspectionErrorKind::InvalidRequest,
        .message = std::move(message),
    };
}

Result<Json, InspectionError> parse_object(std::string_view text) {
    try {
        auto value = Json::parse(text);
        if (!value.is_object()) {
            return failure(
                invalid_request("Profiling request must be a JSON object")
            );
        }
        return value;
    } catch (const Json::exception& error) {
        return failure(invalid_request(
            "Profiling request is not valid JSON: " + std::string(error.what())
        ));
    }
}

Status<InspectionError> require_fields(
    const Json& value,
    std::initializer_list<std::string_view> allowed,
    std::initializer_list<std::string_view> required
) {
    for (const auto& [key, unused] : value.items()) {
        (void)unused;
        bool found = false;
        for (const auto field : allowed) {
            if (key == field) {
                found = true;
                break;
            }
        }
        if (!found) {
            return failure(invalid_request(
                "Unexpected profiling request field '" + key + "'"
            ));
        }
    }
    for (const auto field : required) {
        if (!value.contains(field)) {
            return failure(invalid_request(
                "Profiling request is missing field '" + std::string(field) +
                "'"
            ));
        }
    }
    return {};
}

Result<EmptyRequest, InspectionError>
parse_empty_request(std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {}, {}); !fields) {
        return failure(std::move(fields.error()));
    }
    return EmptyRequest {};
}

Result<CompactSummaryRequest, InspectionError>
parse_compact_summary_request(std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"catalog_revision"}, {});
        !fields) {
        return failure(std::move(fields.error()));
    }
    CompactSummaryRequest result;
    if (request->contains("catalog_revision")) {
        const auto& revision = request->at("catalog_revision");
        if (!revision.is_number_unsigned()) {
            return failure(invalid_request(
                "Profiling catalog revision must be an unsigned integer"
            ));
        }
        result.catalog_revision = revision.get<std::uint64_t>();
    }
    return result;
}

Result<FrameHistoryRequest, InspectionError>
parse_frame_history_request(std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"after_frame"}, {}); !fields) {
        return failure(std::move(fields.error()));
    }
    FrameHistoryRequest result;
    if (request->contains("after_frame")) {
        const auto& frame = request->at("after_frame");
        if (!frame.is_number_unsigned()) {
            return failure(invalid_request(
                "Profiling frame history cursor must be an unsigned integer"
            ));
        }
        result.after_frame = frame.get<std::uint64_t>();
    }
    return result;
}

Result<FrameDetailsRequest, InspectionError> parse_frame_request(
    std::string_view request_json,
    std::size_t maximum_frames,
    std::string_view description
) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields = require_fields(*request, {"frames"}, {"frames"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    const auto& frames_json = request->at("frames");
    if (!frames_json.is_array() || frames_json.empty() ||
        frames_json.size() > maximum_frames) {
        return failure(invalid_request(
            std::string(description) + " requires between 1 and " +
            std::to_string(maximum_frames) + " frame numbers"
        ));
    }

    FrameDetailsRequest result;
    result.frames.reserve(frames_json.size());
    for (const auto& frame : frames_json) {
        if (!frame.is_number_unsigned()) {
            return failure(invalid_request(
                std::string(description) +
                " frame numbers must be unsigned integers"
            ));
        }
        result.frames.push_back(frame.get<std::uint64_t>());
    }
    return result;
}

Json frame_stats_json(const FrameProfileStats& stats) {
    return Json {
        {"available", stats.frame_count > 0},
        {"frame_count", stats.frame_count},
        {"fps", stats.fps},
        {"latest_frame_ms", stats.latest_frame_ms},
        {"average_frame_ms", stats.average_frame_ms},
    };
}

Json entry_json(const ProfileEntrySnapshot& entry) {
    return Json {
        {"schedule_id", entry.schedule_id},
        {"system_id", entry.system_id},
        {"schedule_name", entry.schedule_name},
        {"symbol_kind", profile_symbol_kind_name(entry.symbol.kind)},
        {"symbol_module", entry.symbol.module_id},
        {"symbol_id", entry.symbol.value},
        {"name", entry.name},
        {"file", entry.file},
        {"function", entry.function},
        {"line", entry.line},
        {"count", entry.count},
        {"total_ms", entry.total_ms},
        {"self_ms", entry.self_ms},
        {"mean_ms", entry.mean_ms},
        {"self_mean_ms", entry.self_mean_ms},
        {"min_ms", entry.min_ms},
        {"max_ms", entry.max_ms},
    };
}

Json summary_json(const ProfileSummarySnapshot& snapshot) {
    Json systems = Json::array();
    for (const auto& system : snapshot.systems) {
        systems.push_back(entry_json(system));
    }
    Json zones = Json::array();
    for (const auto& zone : snapshot.zones) {
        zones.push_back(entry_json(zone));
    }
    return Json {
        {"available", snapshot.available},
        {"frame_stats", frame_stats_json(snapshot.frame_stats)},
        {"systems", std::move(systems)},
        {"zones", std::move(zones)},
    };
}

struct CompactSummaryCatalogEntry {
    bool system {false};
    ProfileEntrySnapshot metadata;
};

struct CompactSummaryCatalog {
    std::uint64_t revision {1};
    std::unordered_map<std::string, std::uint32_t> indices;
    std::vector<CompactSummaryCatalogEntry> entries;
};

CompactSummaryCatalog& compact_summary_catalog() {
    static CompactSummaryCatalog catalog;
    return catalog;
}

std::string
compact_summary_key(const ProfileEntrySnapshot& entry, bool system) {
    if (system) {
        return "system|" + std::to_string(entry.schedule_id) + '|' +
               std::to_string(entry.system_id);
    }
    return "zone|" + std::to_string(entry.schedule_id) + '|' +
           std::to_string(entry.system_id) + '|' + entry.name + '|' +
           entry.file + '|' + std::to_string(entry.line);
}

bool same_compact_summary_metadata(
    const ProfileEntrySnapshot& left,
    const ProfileEntrySnapshot& right
) {
    return left.kind == right.kind && left.schedule_id == right.schedule_id &&
           left.system_id == right.system_id &&
           left.schedule_name == right.schedule_name &&
           left.symbol.kind == right.symbol.kind &&
           left.symbol.module_id == right.symbol.module_id &&
           left.symbol.value == right.symbol.value && left.name == right.name &&
           left.file == right.file && left.function == right.function &&
           left.line == right.line;
}

std::uint32_t ensure_compact_summary_entry(
    CompactSummaryCatalog& catalog,
    const ProfileEntrySnapshot& entry,
    bool system
) {
    auto key = compact_summary_key(entry, system);
    const auto existing = catalog.indices.find(key);
    if (existing != catalog.indices.end()) {
        auto& cached = catalog.entries[existing->second];
        if (!same_compact_summary_metadata(cached.metadata, entry)) {
            cached = CompactSummaryCatalogEntry {
                .system = system,
                .metadata = entry,
            };
            ++catalog.revision;
        }
        return existing->second;
    }

    const auto index = static_cast<std::uint32_t>(catalog.entries.size());
    catalog.indices.emplace(std::move(key), index);
    catalog.entries.push_back(
        CompactSummaryCatalogEntry {
            .system = system,
            .metadata = entry,
        }
    );
    ++catalog.revision;
    return index;
}

Json compact_summary_json(
    const ProfileSummarySnapshot& snapshot,
    std::uint64_t requested_revision
) {
    auto& catalog = compact_summary_catalog();
    std::vector<std::pair<std::uint32_t, const ProfileEntrySnapshot*>> values;
    values.reserve(snapshot.systems.size() + snapshot.zones.size());
    for (const auto& entry : snapshot.systems) {
        values.emplace_back(
            ensure_compact_summary_entry(catalog, entry, true),
            &entry
        );
    }
    for (const auto& entry : snapshot.zones) {
        values.emplace_back(
            ensure_compact_summary_entry(catalog, entry, false),
            &entry
        );
    }

    Json entries = Json::array();
    if (requested_revision != catalog.revision) {
        for (std::uint32_t index = 0; index < catalog.entries.size(); ++index) {
            const auto& cached = catalog.entries[index];
            const auto& entry = cached.metadata;
            entries.push_back(
                Json::array(
                    {index,
                     cached.system ? 1 : 0,
                     entry.schedule_id,
                     entry.system_id,
                     entry.schedule_name,
                     profile_symbol_kind_name(entry.symbol.kind),
                     entry.symbol.module_id,
                     entry.symbol.value,
                     entry.name,
                     entry.file,
                     entry.function,
                     entry.line}
                )
            );
        }
    }

    Json encoded_values = Json::array();
    for (const auto& [index, entry] : values) {
        encoded_values.push_back(
            Json::array(
                {index,
                 entry->count,
                 entry->total_ms,
                 entry->self_ms,
                 entry->min_ms,
                 entry->max_ms}
            )
        );
    }
    return Json {
        {"available", snapshot.available},
        {"frame_stats",
         Json::array(
             {snapshot.frame_stats.frame_count,
              snapshot.frame_stats.fps,
              snapshot.frame_stats.latest_frame_ms,
              snapshot.frame_stats.average_frame_ms}
         )},
        {"catalog_revision", catalog.revision},
        {"entries", std::move(entries)},
        {"values", std::move(encoded_values)},
    };
}

Json frame_history_json(const FrameHistoryResponse& response) {
    Json frames = Json::array();
    for (const auto& frame : response.frames) {
        frames.push_back(
            Json {{"frame", frame.frame}, {"duration_ms", frame.duration_ms}}
        );
    }
    return Json {
        {"available", response.available},
        {"frames", std::move(frames)},
    };
}

Json frame_details_json(const ProfileFrameDetailsSnapshot& snapshot) {
    Json details = Json::array();
    for (const auto& detail : snapshot.details) {
        Json systems = Json::array();
        for (const auto& system : detail.systems) {
            systems.push_back(entry_json(system));
        }
        Json zones = Json::array();
        for (const auto& zone : detail.zones) {
            zones.push_back(entry_json(zone));
        }
        details.push_back(
            Json {
                {"frame", detail.frame},
                {"duration_ms", detail.duration_ms},
                {"systems", std::move(systems)},
                {"zones", std::move(zones)},
            }
        );
    }
    return Json {
        {"available", snapshot.available},
        {"details", std::move(details)},
    };
}

Json frame_archive_json(const ProfileFrameArchiveSnapshot& snapshot) {
    Json entries = Json::array();
    for (const auto& entry : snapshot.entries) {
        entries.push_back(
            Json::array(
                {entry.kind == ProfileZoneKind::System ? 1 : 0,
                 entry.schedule_id,
                 entry.system_id,
                 entry.schedule_name,
                 profile_symbol_kind_name(entry.symbol.kind),
                 entry.symbol.module_id,
                 entry.symbol.value,
                 entry.name,
                 entry.file,
                 entry.function,
                 entry.line}
            )
        );
    }

    Json frames = Json::array();
    for (const auto& frame : snapshot.frames) {
        Json records = Json::array();
        for (const auto& record : frame.records) {
            records.push_back(
                Json::array(
                    {record.entry_index,
                     record.count,
                     record.total_ms,
                     record.self_ms,
                     record.min_ms,
                     record.max_ms}
                )
            );
        }
        frames.push_back(
            Json::array({frame.frame, frame.duration_ms, std::move(records)})
        );
    }
    return Json {
        {"available", snapshot.available},
        {"entries", std::move(entries)},
        {"frames", std::move(frames)},
    };
}

Json gpu_summary_json(const GpuProfileSummarySnapshot& snapshot) {
    Json entries = Json::array();
    for (const auto& entry : snapshot.entries) {
        entries.push_back(
            Json {
                {"name", entry.name},
                {"count", entry.count},
                {"latest_ms", entry.latest_ms},
                {"total_ms", entry.total_ms},
                {"mean_ms", entry.mean_ms},
                {"min_ms", entry.min_ms},
                {"max_ms", entry.max_ms},
            }
        );
    }
    return Json {
        {"available", snapshot.available},
        {"entries", std::move(entries)},
    };
}

Json capture_status_json(const ProfileCaptureStatus& status) {
    return Json {
        {"available", status.available},
        {"recording", status.recording},
        {"bounded", status.bounded},
        {"frame_limit", status.frame_limit},
        {"frames_remaining", status.frames_remaining},
    };
}

Result<std::string, InspectionError> checked_json(Json value) {
    auto text = value.dump();
    if (text.size() > c_max_profiling_response_bytes) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::ResponseTooLarge,
                .message = "Profiling response exceeds the maximum size of " +
                           std::to_string(c_max_profiling_response_bytes) +
                           " bytes",
            }
        );
    }
    return text;
}

} // namespace

Result<SummaryResponse, InspectionError>
SummaryProvider::inspect(World&, const EmptyRequest&) const {
    return SummaryResponse {.snapshot = profile_summary_snapshot()};
}

Result<CompactSummaryResponse, InspectionError> CompactSummaryProvider::inspect(
    World&,
    const CompactSummaryRequest& request
) const {
    return CompactSummaryResponse {
        .snapshot = profile_summary_snapshot(),
        .catalog_revision = request.catalog_revision,
    };
}

Result<FrameHistoryResponse, InspectionError> FrameHistoryProvider::inspect(
    World&,
    const FrameHistoryRequest& request
) const {
    auto snapshot = profile_frame_history_snapshot(request.after_frame);
    return FrameHistoryResponse {
        .available = snapshot.available,
        .frames = std::move(snapshot.frames),
    };
}

Result<FrameDetailsResponse, InspectionError> FrameDetailsProvider::inspect(
    World&,
    const FrameDetailsRequest& request
) const {
    return FrameDetailsResponse {
        .snapshot = profile_frame_details_snapshot(request.frames),
    };
}

Result<FrameArchiveResponse, InspectionError> FrameArchiveProvider::inspect(
    World&,
    const FrameDetailsRequest& request
) const {
    return FrameArchiveResponse {
        .snapshot = profile_frame_archive_snapshot(request.frames),
    };
}

Result<GpuSummaryResponse, InspectionError>
GpuSummaryProvider::inspect(World&, const EmptyRequest&) const {
    return GpuSummaryResponse {.snapshot = gpu_profile_summary_snapshot()};
}

Result<ControlResponse, InspectionError>
ControlProvider::inspect(World&, const ControlRequest& request) const {
    const auto current = profile_capture_status();
    if ((request.action == "start" || request.action == "capture") &&
        !current.available) {
        return failure(
            InspectionError {
                .kind = InspectionErrorKind::Unsupported,
                .message = "CPU profiling summary is not enabled in this "
                           "runtime build",
            }
        );
    }

    if (request.action == "status") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'status' does not accept frames"
            ));
        }
    } else if (request.action == "start") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'start' does not accept frames"
            ));
        }
        start_profile_capture();
    } else if (request.action == "capture") {
        if (request.frames == 0 ||
            request.frames > c_max_profile_capture_frames) {
            return failure(invalid_request(
                "Profiling capture frames must be between 1 and " +
                std::to_string(c_max_profile_capture_frames)
            ));
        }
        start_profile_capture(request.frames);
    } else if (request.action == "stop") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'stop' does not accept frames"
            ));
        }
        stop_profile_capture();
    } else if (request.action == "clear") {
        if (request.frames != 0) {
            return failure(invalid_request(
                "Profiling action 'clear' does not accept frames"
            ));
        }
        clear_profile_summary();
        clear_gpu_profile_summary();
    } else {
        return failure(
            invalid_request("Unknown profiling action '" + request.action + "'")
        );
    }
    return ControlResponse {.status = profile_capture_status()};
}

Result<std::string, InspectionError>
profiling_summary_json(World& world, std::string_view request_json) {
    auto request = parse_empty_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = SummaryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(summary_json(response->snapshot));
}

Result<std::string, InspectionError>
profiling_compact_summary_json(World& world, std::string_view request_json) {
    auto request = parse_compact_summary_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = CompactSummaryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(
        compact_summary_json(response->snapshot, response->catalog_revision)
    );
}

Result<std::string, InspectionError>
profiling_frame_history_json(World& world, std::string_view request_json) {
    auto request = parse_frame_history_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = FrameHistoryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(frame_history_json(*response));
}

Result<std::string, InspectionError>
profiling_frame_details_json(World& world, std::string_view request_json) {
    auto request = parse_frame_request(
        request_json,
        c_max_profile_detail_frames,
        "Profiling frame details"
    );
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = FrameDetailsProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(frame_details_json(response->snapshot));
}

Result<std::string, InspectionError>
profiling_frame_archive_json(World& world, std::string_view request_json) {
    auto request = parse_frame_request(
        request_json,
        c_max_profile_archive_frames,
        "Profiling frame archive"
    );
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = FrameArchiveProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(frame_archive_json(response->snapshot));
}

Result<std::string, InspectionError>
profiling_gpu_summary_json(World& world, std::string_view request_json) {
    auto request = parse_empty_request(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    auto response = GpuSummaryProvider {}.inspect(world, *request);
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(gpu_summary_json(response->snapshot));
}

Result<std::string, InspectionError>
control_profiling_json(World& world, std::string_view request_json) {
    auto request = parse_object(request_json);
    if (!request) {
        return failure(std::move(request.error()));
    }
    if (auto fields =
            require_fields(*request, {"action", "frames"}, {"action"});
        !fields) {
        return failure(std::move(fields.error()));
    }
    if (!request->at("action").is_string()) {
        return failure(
            invalid_request("Profiling request field 'action' must be a string")
        );
    }
    std::uint64_t frames = 0;
    if (request->contains("frames")) {
        const auto& value = request->at("frames");
        if (!value.is_number_unsigned()) {
            return failure(invalid_request(
                "Profiling request field 'frames' must be an unsigned integer"
            ));
        }
        frames = value.get<std::uint64_t>();
    }
    auto response = ControlProvider {}.inspect(
        world,
        ControlRequest {
            .action = request->at("action").get<std::string>(),
            .frames = frames,
        }
    );
    if (!response) {
        return failure(std::move(response.error()));
    }
    return checked_json(capture_status_json(response->status));
}

Status<InspectionError>
register_profiling_inspection_providers(InspectionRegistry& registry) {
    auto status = registry.add<SummaryProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_summary_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<CompactSummaryProvider>([](World& world,
                                                     std::string_view payload) {
        return profiling_compact_summary_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<FrameHistoryProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_frame_history_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<FrameDetailsProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_frame_details_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<FrameArchiveProvider>([](World& world,
                                                   std::string_view payload) {
        return profiling_frame_archive_json(world, payload);
    });
    if (!status) {
        return status;
    }
    status = registry.add<GpuSummaryProvider>([](World& world,
                                                 std::string_view payload) {
        return profiling_gpu_summary_json(world, payload);
    });
    if (!status) {
        return status;
    }
    return registry.add<ControlProvider>([](World& world,
                                            std::string_view payload) {
        return control_profiling_json(world, payload);
    });
}

} // namespace ets::runtime_inspection::profiling
