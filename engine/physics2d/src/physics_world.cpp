#include "physics2d/physics_world.hpp"

#include "core/transform.hpp"
#include "math/common.hpp"

#include <algorithm>
#include <box2d/box2d.h>
#include <cstdint>
#include <unordered_map>

namespace fei {

namespace {

b2BodyType to_box2d(RigidBodyType2d type) {
    switch (type) {
        case RigidBodyType2d::Static:
            return b2_staticBody;
        case RigidBodyType2d::Kinematic:
            return b2_kinematicBody;
        case RigidBodyType2d::Dynamic:
            return b2_dynamicBody;
    }
    return b2_staticBody;
}

b2Vec2 to_box2d(Vector2 value) {
    return b2Vec2 {.x = value.x, .y = value.y};
}

Vector2 from_box2d(b2Vec2 value) {
    return Vector2 {value.x, value.y};
}

} // namespace

struct PhysicsWorld2d::Impl {
    struct BodyRecord {
        b2BodyId body_id {b2_nullBodyId};
        b2ShapeId shape_id {b2_nullShapeId};
        RigidBody2d body;
        Collider2d collider;
        PhysicsMaterial2d material;
        Vector2 input_position;
        float input_rotation {0.0f};
    };

    b2WorldId world_id {b2_nullWorldId};
    std::unordered_map<Entity, BodyRecord> bodies;
    std::unordered_map<std::uint64_t, Entity> body_entities;
    std::unordered_map<std::uint64_t, Entity> shape_entities;

    Impl() {
        auto definition = b2DefaultWorldDef();
        world_id = b2CreateWorld(&definition);
    }

    ~Impl() {
        if (B2_IS_NON_NULL(world_id)) {
            b2DestroyWorld(world_id);
        }
    }

    void destroy(Entity entity) {
        auto found = bodies.find(entity);
        if (found == bodies.end()) {
            return;
        }

        body_entities.erase(b2StoreBodyId(found->second.body_id));
        shape_entities.erase(b2StoreShapeId(found->second.shape_id));
        if (b2Body_IsValid(found->second.body_id)) {
            b2DestroyBody(found->second.body_id);
        }
        bodies.erase(found);
    }

    BodyRecord create(
        Entity entity,
        const Transform2d& transform,
        const RigidBody2d& body,
        const Collider2d& collider,
        const PhysicsMaterial2d& material,
        const LinearVelocity2d* linear_velocity,
        const AngularVelocity2d* angular_velocity
    ) {
        auto body_definition = b2DefaultBodyDef();
        body_definition.type = to_box2d(body.type);
        body_definition.position = to_box2d(transform.position);
        body_definition.rotation = b2MakeRot(transform.rotation * DEG2RAD);
        body_definition.linearDamping = body.linear_damping;
        body_definition.angularDamping = body.angular_damping;
        body_definition.gravityScale = body.gravity_scale;
        body_definition.fixedRotation = body.fixed_rotation;
        body_definition.isBullet = body.bullet;
        if (linear_velocity != nullptr) {
            body_definition.linearVelocity = to_box2d(linear_velocity->value);
        }
        if (angular_velocity != nullptr) {
            body_definition.angularVelocity = angular_velocity->value;
        }

        BodyRecord record {
            .body_id = b2CreateBody(world_id, &body_definition),
            .body = body,
            .collider = collider,
            .material = material,
            .input_position = transform.position,
            .input_rotation = transform.rotation,
        };

        auto shape_definition = b2DefaultShapeDef();
        shape_definition.density = std::max(material.density, 0.0f);
        shape_definition.material.friction = std::max(material.friction, 0.0f);
        shape_definition.material.restitution =
            std::max(material.restitution, 0.0f);
        shape_definition.enableContactEvents = true;

        switch (collider.shape) {
            case ColliderShape2d::Box: {
                const auto polygon =
                    b2MakeBox(collider.half_extents.x, collider.half_extents.y);
                record.shape_id = b2CreatePolygonShape(
                    record.body_id,
                    &shape_definition,
                    &polygon
                );
                break;
            }
            case ColliderShape2d::Circle: {
                const b2Circle circle {
                    .center = b2Vec2 {.x = 0.0f, .y = 0.0f},
                    .radius = collider.radius,
                };
                record.shape_id = b2CreateCircleShape(
                    record.body_id,
                    &shape_definition,
                    &circle
                );
                break;
            }
        }

        body_entities.emplace(b2StoreBodyId(record.body_id), entity);
        shape_entities.emplace(b2StoreShapeId(record.shape_id), entity);
        return record;
    }
};

PhysicsWorld2d::PhysicsWorld2d() : m_impl(std::make_unique<Impl>()) {}

PhysicsWorld2d::~PhysicsWorld2d() = default;

PhysicsWorld2d::PhysicsWorld2d(PhysicsWorld2d&&) noexcept = default;

PhysicsWorld2d& PhysicsWorld2d::operator=(PhysicsWorld2d&&) noexcept = default;

void PhysicsWorld2d::synchronize_body(
    Entity entity,
    const Transform2d& transform,
    const RigidBody2d& body,
    const Collider2d& collider,
    const PhysicsMaterial2d* material,
    const LinearVelocity2d* linear_velocity,
    const AngularVelocity2d* angular_velocity
) {
    if (!collider.valid()) {
        m_impl->destroy(entity);
        return;
    }

    const PhysicsMaterial2d resolved_material =
        material != nullptr ? *material : PhysicsMaterial2d {};
    auto found = m_impl->bodies.find(entity);
    if (found != m_impl->bodies.end() &&
        (found->second.body != body || found->second.collider != collider ||
         found->second.material != resolved_material)) {
        m_impl->destroy(entity);
        found = m_impl->bodies.end();
    }

    if (found == m_impl->bodies.end()) {
        auto record = m_impl->create(
            entity,
            transform,
            body,
            collider,
            resolved_material,
            linear_velocity,
            angular_velocity
        );
        found = m_impl->bodies.emplace(entity, record).first;
    }

    if (body.type != RigidBodyType2d::Dynamic &&
        (found->second.input_position != transform.position ||
         found->second.input_rotation != transform.rotation)) {
        b2Body_SetTransform(
            found->second.body_id,
            to_box2d(transform.position),
            b2MakeRot(transform.rotation * DEG2RAD)
        );
        found->second.input_position = transform.position;
        found->second.input_rotation = transform.rotation;
    }
    if (linear_velocity != nullptr &&
        from_box2d(b2Body_GetLinearVelocity(found->second.body_id)) !=
            linear_velocity->value) {
        b2Body_SetLinearVelocity(
            found->second.body_id,
            to_box2d(linear_velocity->value)
        );
    }
    if (angular_velocity != nullptr &&
        b2Body_GetAngularVelocity(found->second.body_id) !=
            angular_velocity->value) {
        b2Body_SetAngularVelocity(
            found->second.body_id,
            angular_velocity->value
        );
    }
}

bool PhysicsWorld2d::teleport(
    Entity entity,
    Vector2 position,
    float rotation_degrees
) {
    auto found = m_impl->bodies.find(entity);
    if (found == m_impl->bodies.end()) {
        return false;
    }
    b2Body_SetTransform(
        found->second.body_id,
        to_box2d(position),
        b2MakeRot(rotation_degrees * DEG2RAD)
    );
    found->second.input_position = position;
    found->second.input_rotation = rotation_degrees;
    return true;
}

void PhysicsWorld2d::remove_body(Entity entity) {
    m_impl->destroy(entity);
}

bool PhysicsWorld2d::contains(Entity entity) const {
    return m_impl->bodies.contains(entity);
}

std::size_t PhysicsWorld2d::body_count() const {
    return m_impl->bodies.size();
}

void PhysicsWorld2d::step(const PhysicsSettings2d& settings, float timestep) {
    b2World_SetGravity(m_impl->world_id, to_box2d(settings.gravity));
    b2World_Step(
        m_impl->world_id,
        std::max(timestep, 0.0f),
        std::max(settings.substeps, 1)
    );
}

std::vector<PhysicsBodyMovement2d> PhysicsWorld2d::body_movements() const {
    const auto events = b2World_GetBodyEvents(m_impl->world_id);
    std::vector<PhysicsBodyMovement2d> movements;
    movements.reserve(static_cast<std::size_t>(events.moveCount));
    for (int index = 0; index < events.moveCount; ++index) {
        const auto& event = events.moveEvents[index];
        const auto entity =
            m_impl->body_entities.find(b2StoreBodyId(event.bodyId));
        if (entity == m_impl->body_entities.end()) {
            continue;
        }
        movements.push_back(
            PhysicsBodyMovement2d {
                .entity = entity->second,
                .position = from_box2d(event.transform.p),
                .rotation_radians = b2Rot_GetAngle(event.transform.q),
                .linear_velocity =
                    from_box2d(b2Body_GetLinearVelocity(event.bodyId)),
                .angular_velocity = b2Body_GetAngularVelocity(event.bodyId),
            }
        );
    }
    return movements;
}

std::vector<CollisionStarted2d> PhysicsWorld2d::collisions_started() const {
    const auto events = b2World_GetContactEvents(m_impl->world_id);
    std::vector<CollisionStarted2d> collisions;
    collisions.reserve(static_cast<std::size_t>(events.beginCount));
    for (int index = 0; index < events.beginCount; ++index) {
        const auto& event = events.beginEvents[index];
        const auto entity_a =
            m_impl->shape_entities.find(b2StoreShapeId(event.shapeIdA));
        const auto entity_b =
            m_impl->shape_entities.find(b2StoreShapeId(event.shapeIdB));
        if (entity_a == m_impl->shape_entities.end() ||
            entity_b == m_impl->shape_entities.end()) {
            continue;
        }
        collisions.push_back(
            CollisionStarted2d {
                .entity_a = entity_a->second,
                .entity_b = entity_b->second,
                .normal = from_box2d(event.manifold.normal),
            }
        );
    }
    return collisions;
}

std::vector<CollisionEnded2d> PhysicsWorld2d::collisions_ended() const {
    const auto events = b2World_GetContactEvents(m_impl->world_id);
    std::vector<CollisionEnded2d> collisions;
    collisions.reserve(static_cast<std::size_t>(events.endCount));
    for (int index = 0; index < events.endCount; ++index) {
        const auto& event = events.endEvents[index];
        const auto entity_a =
            m_impl->shape_entities.find(b2StoreShapeId(event.shapeIdA));
        const auto entity_b =
            m_impl->shape_entities.find(b2StoreShapeId(event.shapeIdB));
        if (entity_a == m_impl->shape_entities.end() ||
            entity_b == m_impl->shape_entities.end()) {
            continue;
        }
        collisions.push_back(
            CollisionEnded2d {
                .entity_a = entity_a->second,
                .entity_b = entity_b->second,
            }
        );
    }
    return collisions;
}

} // namespace fei
