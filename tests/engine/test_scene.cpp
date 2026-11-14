#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <glm/glm.hpp>

#include <crimson/scene/entity.hpp>

using namespace crimson;
using Catch::Approx;

static void CheckNear(const glm::vec3& a, const glm::vec3& b)
{
    CHECK(a.x == Approx(b.x).margin(0.0001));
    CHECK(a.y == Approx(b.y).margin(0.0001));
    CHECK(a.z == Approx(b.z).margin(0.0001));
}

static void CheckNear(const glm::mat4& a, const glm::mat4& b, float margin = 0.0001f)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            CHECK(a[column][row] == Approx(b[column][row]).margin(margin));
}

// A box from -1 to 1 that raycasts can hit, with no GPU behind it
static Entity CreateBox(Scene& scene, const char* name, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f))
{
    const Entity entity = scene.CreateEntity(name);
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Position = position;
    transform.Scale = scale;

    auto& renderer = entity.AddComponent<MeshRendererComponent>();
    renderer.Resolved.SubMeshes.push_back({});
    renderer.Resolved.Bounds = { glm::vec3(-1.0f), glm::vec3(1.0f) };

    return entity;
}

TEST_CASE("Every entity has a name, a transform and a relationship", "[scene]")
{
    Scene scene;
    const Entity entity = scene.CreateEntity("Thing");

    CHECK(entity);
    CHECK(entity.GetComponent<NameComponent>().Name == "Thing");
    CHECK(entity.HasComponent<TransformComponent>());
    CHECK(entity.HasComponent<RelationshipComponent>());
    REQUIRE(scene.GetRoots().size() == 1);
    CHECK(scene.GetRoots()[0] == entity.GetHandle());
}

TEST_CASE("A default entity is invalid", "[scene]")
{
    CHECK_FALSE(Entity{});
}

TEST_CASE("A child isn't a root and its parent lists it", "[scene]")
{
    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity child = scene.CreateEntity("Child", parent);

    CHECK(scene.GetRoots().size() == 1);
    CHECK(child.GetComponent<RelationshipComponent>().Parent == parent.GetHandle());
    CHECK(parent.GetComponent<RelationshipComponent>().Children == std::vector{ child.GetHandle() });
}

TEST_CASE("Destroying an entity also destroys its children", "[scene]")
{
    Scene scene;
    const Entity root = scene.CreateEntity("Root");
    const Entity keep = scene.CreateEntity("Keep");
    const Entity child = scene.CreateEntity("Child", root);
    const Entity grandchild = scene.CreateEntity("Grandchild", child);

    scene.DestroyEntity(root);

    CHECK_FALSE(root);
    CHECK_FALSE(child);
    CHECK_FALSE(grandchild);
    CHECK(keep);
    REQUIRE(scene.GetRoots().size() == 1);
    CHECK(scene.GetRoots()[0] == keep.GetHandle());

    SECTION("Destroying it again does nothing")
    {
        scene.DestroyEntity(root);
        CHECK(keep);
    }
}

TEST_CASE("Destroying a child removes it from its parent", "[scene]")
{
    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity child = scene.CreateEntity("Child", parent);

    scene.DestroyEntity(child);

    CHECK(parent);
    CHECK(parent.GetComponent<RelationshipComponent>().Children.empty());
}

TEST_CASE("Reparenting refuses to make a loop", "[scene]")
{
    Scene scene;
    const Entity a = scene.CreateEntity("A");
    const Entity b = scene.CreateEntity("B", a);
    const Entity c = scene.CreateEntity("C", b);

    CHECK_FALSE(scene.SetParent(a, a));
    CHECK_FALSE(scene.SetParent(a, c));

    CHECK((a.GetComponent<RelationshipComponent>().Parent == entt::null));
    CHECK(c.GetComponent<RelationshipComponent>().Parent == b.GetHandle());
}

TEST_CASE("Reparenting keeps the entity where it is in the world", "[scene]")
{
    Scene scene;

    const Entity parent = scene.CreateEntity("Parent");
    auto& parentTransform = parent.GetComponent<TransformComponent>();
    parentTransform.Position = glm::vec3(10.0f, 0.0f, 0.0f);
    parentTransform.RotationDegrees = glm::vec3(0.0f, 40.0f, 0.0f);
    parentTransform.Scale = glm::vec3(2.0f);

    const Entity entity = scene.CreateEntity("Entity");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    transform.RotationDegrees = glm::vec3(30.0f, 0.0f, 0.0f);

    const glm::mat4 before = scene.ComputeWorldMatrix(entity);

    REQUIRE(scene.SetParent(entity, parent));
    CHECK(entity.GetComponent<RelationshipComponent>().Parent == parent.GetHandle());
    CheckNear(scene.ComputeWorldMatrix(entity), before);

    SECTION("And when becoming a root again")
    {
        REQUIRE(scene.SetParent(entity, Entity{}));
        CHECK((entity.GetComponent<RelationshipComponent>().Parent == entt::null));
        CHECK(scene.GetRoots().size() == 2);
        CheckNear(scene.ComputeWorldMatrix(entity), before);
    }
}

// Converting to angles at gimbal lock loses precision in float, about 0.02 degrees. This pins that it stays that small.
TEST_CASE("Reparenting under a parent turned exactly 90 degrees stays close", "[scene]")
{
    Scene scene;

    const Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().RotationDegrees = glm::vec3(0.0f, 90.0f, 0.0f);

    const Entity entity = scene.CreateEntity("Entity");
    entity.GetComponent<TransformComponent>().RotationDegrees = glm::vec3(30.0f, 0.0f, 0.0f);

    const glm::mat4 before = scene.ComputeWorldMatrix(entity);

    REQUIRE(scene.SetParent(entity, parent));
    CheckNear(scene.ComputeWorldMatrix(entity), before, 0.001f);
}

TEST_CASE("World transforms combine the parents", "[scene]")
{
    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity child = scene.CreateEntity("Child", parent);

    parent.GetComponent<TransformComponent>().Position = glm::vec3(1.0f, 0.0f, 0.0f);
    parent.GetComponent<TransformComponent>().Scale = glm::vec3(2.0f);
    child.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, 3.0f, 0.0f);

    scene.UpdateWorldTransforms();

    CheckNear(glm::vec3(child.GetComponent<TransformComponent>().World[3]), glm::vec3(1.0f, 6.0f, 0.0f));
    CheckNear(child.GetComponent<TransformComponent>().World, scene.ComputeWorldMatrix(child));
}

TEST_CASE("An inactive entity also disables its children", "[scene]")
{
    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity child = scene.CreateEntity("Child", parent);

    parent.GetComponent<NameComponent>().Active = false;
    scene.UpdateWorldTransforms();

    CHECK_FALSE(parent.GetComponent<TransformComponent>().ActiveInHierarchy);
    CHECK_FALSE(child.GetComponent<TransformComponent>().ActiveInHierarchy);
    CHECK(child.GetComponent<NameComponent>().Active);

    parent.GetComponent<NameComponent>().Active = true;
    scene.UpdateWorldTransforms();

    CHECK(child.GetComponent<TransformComponent>().ActiveInHierarchy);
}

TEST_CASE("A transform survives a trip through a matrix", "[scene]")
{
    TransformComponent original;
    original.Position = glm::vec3(1.0f, -2.0f, 3.0f);
    original.Scale = glm::vec3(2.0f, 3.0f, 4.0f);

    SECTION("Rotated")
    {
        original.RotationDegrees = glm::vec3(30.0f, 45.0f, 60.0f);
    }

    SECTION("Mirrored, which a rotation alone can't express")
    {
        original.Scale = glm::vec3(-2.0f, 3.0f, 4.0f);
    }

    TransformComponent copy;
    copy.SetFromMatrix(original.GetLocalMatrix());

    CheckNear(copy.GetLocalMatrix(), original.GetLocalMatrix());
}

TEST_CASE("The stored angles don't jump when the rotation stays the same", "[scene]")
{
    // These angles would come back as different ones for the same rotation, which makes a gizmo's values jump
    TransformComponent transform;
    transform.RotationDegrees = glm::vec3(170.0f, 80.0f, -120.0f);
    const glm::vec3 before = transform.RotationDegrees;

    transform.SetFromMatrix(transform.GetLocalMatrix());

    CHECK(transform.RotationDegrees == before);
}

TEST_CASE("A transform with no scale keeps its rotation", "[scene]")
{
    TransformComponent transform;
    transform.RotationDegrees = glm::vec3(10.0f, 20.0f, 30.0f);
    transform.Scale = glm::vec3(0.0f);
    const glm::vec3 before = transform.RotationDegrees;

    transform.SetFromMatrix(transform.GetLocalMatrix());

    CHECK(transform.RotationDegrees == before);
}

TEST_CASE("A raycast finds the nearest box", "[scene]")
{
    Scene scene;
    const glm::vec3 origin(0.0f, 0.0f, 10.0f);
    const glm::vec3 forward(0.0f, 0.0f, -1.0f);

    SECTION("Misses when nothing is there")
    {
        CreateBox(scene, "Aside", glm::vec3(5.0f, 0.0f, 0.0f));
        scene.UpdateWorldTransforms();
        CHECK_FALSE(scene.Raycast(origin, forward));
    }

    SECTION("Doesn't hit what is behind it")
    {
        CreateBox(scene, "Behind", glm::vec3(0.0f, 0.0f, 20.0f));
        scene.UpdateWorldTransforms();
        CHECK_FALSE(scene.Raycast(origin, forward));
    }

    SECTION("Picks the nearer of two")
    {
        CreateBox(scene, "Far", glm::vec3(0.0f));
        const Entity near = CreateBox(scene, "Near", glm::vec3(0.0f, 0.0f, 5.0f));
        scene.UpdateWorldTransforms();
        CHECK(scene.Raycast(origin, forward) == near);
    }

    SECTION("Skips inactive entities")
    {
        const Entity far = CreateBox(scene, "Far", glm::vec3(0.0f));
        const Entity near = CreateBox(scene, "Near", glm::vec3(0.0f, 0.0f, 5.0f));
        near.GetComponent<NameComponent>().Active = false;
        scene.UpdateWorldTransforms();
        CHECK(scene.Raycast(origin, forward) == far);
    }

    SECTION("Skips entities without a loaded mesh")
    {
        const Entity box = CreateBox(scene, "Box", glm::vec3(0.0f));
        box.GetComponent<MeshRendererComponent>().Resolved.SubMeshes.clear();
        scene.UpdateWorldTransforms();
        CHECK_FALSE(scene.Raycast(origin, forward));
    }

    SECTION("Respects the transform's scale")
    {
        const Entity wide = CreateBox(scene, "Wide", glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 1.0f));
        scene.UpdateWorldTransforms();
        CHECK(scene.Raycast(glm::vec3(5.0f, 0.0f, 10.0f), forward) == wide);
    }

    SECTION("Prefers what is in front over a box the camera is standing in")
    {
        CreateBox(scene, "Room", glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(100.0f));
        const Entity ahead = CreateBox(scene, "Ahead", glm::vec3(0.0f, 0.0f, 5.0f));
        scene.UpdateWorldTransforms();
        CHECK(scene.Raycast(origin, forward) == ahead);
    }
}

TEST_CASE("An entity can be moved among its siblings", "[scene]")
{
    Scene scene;
    const Entity parent = scene.CreateEntity("Parent");
    const Entity a = scene.CreateEntity("A", parent);
    const Entity b = scene.CreateEntity("B", parent);
    const Entity c = scene.CreateEntity("C", parent);

    const auto order = [&] { return parent.GetComponent<RelationshipComponent>().Children; };

    CHECK(scene.GetSiblingIndex(b) == 1);

    scene.SetSiblingIndex(c, 0);
    CHECK(order() == std::vector{ c.GetHandle(), a.GetHandle(), b.GetHandle() });
    CHECK(scene.GetSiblingIndex(a) == 1);

    SECTION("Past the end is the end")
    {
        scene.SetSiblingIndex(c, 10);
        CHECK(order() == std::vector{ a.GetHandle(), b.GetHandle(), c.GetHandle() });
    }

    SECTION("Roots too")
    {
        const Entity other = scene.CreateEntity("Other");
        scene.SetSiblingIndex(other, 0);
        CHECK(scene.GetRoots()[0] == other.GetHandle());
        CHECK(scene.GetSiblingIndex(parent) == 1);
    }
}
