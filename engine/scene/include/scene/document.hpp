#pragma once

#include "asset/loader.hpp"
#include "asset/uuid.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ets {

class World;

inline constexpr std::string_view scene_document_format = "entisium.scene";
inline constexpr std::uint32_t scene_document_version = 1;

struct SceneComponentDocument {
    std::string type;
    serialization::SerializedNode properties;
};

struct SceneEntityDocument {
    AssetUuid id;
    Optional<AssetUuid> parent;
    std::vector<SceneComponentDocument> components;
};

struct SceneDocument {
    std::uint32_t version {scene_document_version};
    std::vector<SceneEntityDocument> entities;
};

enum class SceneDocumentErrorKind : std::uint8_t {
    InvalidYaml,
    InvalidDocument,
    SerializeComponent,
    DeserializeComponent,
};

struct SceneDocumentError {
    SceneDocumentErrorKind kind {SceneDocumentErrorKind::InvalidDocument};
    std::string path;
    std::string message;
};

class SceneEntityBindings {
  public:
    void clear();
    AssetUuid ensure(Entity entity);
    void bind(AssetUuid id, Entity entity);

    [[nodiscard]] Optional<AssetUuid> id(Entity entity) const;
    [[nodiscard]] Optional<Entity> entity(AssetUuid id) const;
    [[nodiscard]] std::vector<Entity> entities() const;

  private:
    std::unordered_map<Entity, AssetUuid> m_by_entity;
    std::unordered_map<AssetUuid, Entity> m_by_id;
};

struct SceneInstantiationResult {
    SceneEntityBindings bindings;
    std::vector<std::string> warnings;
};

Result<SceneDocument, SceneDocumentError>
parse_scene_document(std::string_view source);

Result<std::string, SceneDocumentError>
write_scene_document(const SceneDocument& document);

Result<SceneDocument, SceneDocumentError> capture_scene_document(
    const World& world,
    SceneEntityBindings& bindings,
    const serialization::ValueCodecRegistry* codecs = nullptr,
    const SceneDocument* preserve_unknown_from = nullptr
);

Result<SceneInstantiationResult, SceneDocumentError> instantiate_scene_document(
    const SceneDocument& document,
    World& world,
    const serialization::ValueCodecRegistry* codecs = nullptr
);

class SceneDocumentLoader : public AssetLoader<SceneDocument> {
  public:
    AssetLoadResult<SceneDocument>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace ets
