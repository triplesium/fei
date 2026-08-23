#include "artifact_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

using namespace ets;
using namespace ets::agentd;

TEST_CASE(
    "Artifact store preserves binary data and metadata",
    "[agentd][artifact]"
) {
    ArtifactStore store;
    const std::vector<std::byte> data {
        std::byte {0x89},
        std::byte {0x50},
        std::byte {0x4e},
        std::byte {0x47},
    };

    auto metadata = store.store("image/png", data);
    REQUIRE(metadata);
    CHECK(metadata->content_type == "image/png");
    CHECK(metadata->size == data.size());

    auto artifact = store.find(metadata->id);
    REQUIRE(artifact);
    CHECK(artifact->metadata.id == metadata->id);
    CHECK(artifact->data == data);
}

TEST_CASE(
    "Artifact store evicts oldest captures within its bounds",
    "[agentd][artifact]"
) {
    ArtifactStore store(2, 5);

    auto first =
        store.store("image/png", std::vector<std::byte>(2, std::byte {1}));
    auto second =
        store.store("image/png", std::vector<std::byte>(2, std::byte {2}));
    REQUIRE(first);
    REQUIRE(second);

    auto third =
        store.store("image/png", std::vector<std::byte>(3, std::byte {3}));
    REQUIRE(third);
    CHECK_FALSE(store.find(first->id));
    CHECK(store.find(second->id));
    CHECK(store.find(third->id));
}
