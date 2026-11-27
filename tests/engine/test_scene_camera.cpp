#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include <glm/gtc/epsilon.hpp>

#include <crimson/scene/scene_camera.hpp>

using namespace crimson;
using Catch::Matchers::WithinAbs;

constexpr float kAspect = 16.0f / 9.0f;

static bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
{
    return glm::all(glm::epsilonEqual(a, b, epsilon));
}

static bool Near(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-4f)
{
    for (int column = 0; column < 4; ++column)
    {
        if (!glm::all(glm::epsilonEqual(a[column], b[column], epsilon)))
            return false;
    }

    return true;
}

static glm::vec3 ToNdc(const glm::mat4& projection, const glm::vec3& viewSpace)
{
    const glm::vec4 clip = projection * glm::vec4(viewSpace, 1.0f);
    return glm::vec3(clip) / clip.w;
}

static Entity AddCamera(Scene& scene, const char* name, Entity parent = {})
{
    const Entity entity = scene.CreateEntity(name, parent);
    entity.AddComponent<CameraComponent>();
    return entity;
}

static AssetID IdOf(Entity entity)
{
    return entity.GetComponent<NameComponent>().Id;
}

TEST_CASE("A camera looks along its -Z axis from its position", "[scene camera]")
{
    Scene scene;
    const Entity entity = scene.CreateEntity("Camera");
    auto& transform = entity.GetComponent<TransformComponent>();
    transform.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    transform.RotationDegrees = glm::vec3(0.0f, 90.0f, 0.0f);

    const glm::mat4 world = scene.ComputeWorldMatrix(entity);
    const CameraView view = ComputeCameraView(CameraComponent{}, world, kAspect);

    CHECK(view.Position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(Near(glm::vec3(view.View * glm::vec4(view.Position, 1.0f)), glm::vec3(0.0f)));

    // The entity's own -Z axis is straight ahead
    const glm::vec3 forward = glm::normalize(glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
    CHECK(Near(glm::vec3(view.View * glm::vec4(view.Position + forward, 1.0f)), glm::vec3(0.0f, 0.0f, -1.0f)));
}

TEST_CASE("A camera's view leaves out the scale", "[scene camera]")
{
    Scene scene;

    const auto place = [](Entity entity, const glm::vec3& scale)
    {
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.Position = glm::vec3(4.0f, -1.0f, 2.0f);
        transform.RotationDegrees = glm::vec3(20.0f, 35.0f, 10.0f);
        transform.Scale = scale;
    };

    const Entity plain = scene.CreateEntity("Plain");
    place(plain, glm::vec3(1.0f));
    const glm::mat4 expected = ComputeCameraView(CameraComponent{}, scene.ComputeWorldMatrix(plain), kAspect).View;

    SECTION("Its own scale")
    {
        const Entity scaled = scene.CreateEntity("Scaled");
        place(scaled, glm::vec3(2.0f, 3.0f, 4.0f));

        CHECK(Near(ComputeCameraView(CameraComponent{}, scene.ComputeWorldMatrix(scaled), kAspect).View, expected));
    }

    SECTION("A parent's scale that differs per axis")
    {
        const Entity parent = scene.CreateEntity("Parent");
        parent.GetComponent<TransformComponent>().Scale = glm::vec3(1.0f, 5.0f, 0.5f);

        const Entity child = scene.CreateEntity("Child", parent);
        place(child, glm::vec3(1.0f));

        // The view is still a rotation, so nothing in it is stretched
        const CameraView view = ComputeCameraView(CameraComponent{}, scene.ComputeWorldMatrix(child), kAspect);
        const glm::mat3 rotation(view.View);
        CHECK_THAT(glm::length(rotation[0]), WithinAbs(1.0f, 1e-4f));
        CHECK_THAT(glm::length(rotation[1]), WithinAbs(1.0f, 1e-4f));
        CHECK_THAT(glm::length(rotation[2]), WithinAbs(1.0f, 1e-4f));
        CHECK_THAT(glm::dot(rotation[0], rotation[1]), WithinAbs(0.0f, 1e-4f));
    }

    SECTION("No scale at all")
    {
        const Entity flat = scene.CreateEntity("Flat");
        flat.GetComponent<TransformComponent>().Scale = glm::vec3(0.0f);

        const CameraView view = ComputeCameraView(CameraComponent{}, scene.ComputeWorldMatrix(flat), kAspect);
        for (int column = 0; column < 4; ++column)
        {
            CHECK(std::isfinite(view.View[column].x));
            CHECK(std::isfinite(view.View[column].y));
            CHECK(std::isfinite(view.View[column].z));
            CHECK(std::isfinite(view.View[column].w));
        }
    }
}

TEST_CASE("A perspective camera's projection matches the editor camera's", "[scene camera]")
{
    const CameraComponent camera{ .FieldOfViewDegrees = 50.0f, .NearClip = 0.2f, .FarClip = 300.0f };
    const CameraView view = ComputeCameraView(camera, glm::mat4(1.0f), kAspect);

    const PerspectiveCamera editorCamera(glm::vec3(0.0f), 50.0f, kAspect, 0.2f, 300.0f);
    CHECK(Near(view.Projection, editorCamera.GetProjection()));

    CHECK_THAT(ToNdc(view.Projection, glm::vec3(0.0f, 0.0f, -0.2f)).z, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(ToNdc(view.Projection, glm::vec3(0.0f, 0.0f, -300.0f)).z, WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("An orthographic camera maps its size to the edges of the view at any distance", "[scene camera]")
{
    const CameraComponent camera{ .Projection = CameraProjection::Orthographic, .OrthographicSize = 3.0f, .NearClip = 0.5f, .FarClip = 100.0f };
    const CameraView view = ComputeCameraView(camera, glm::mat4(1.0f), kAspect);

    for (const float distance : { camera.NearClip + 1.0f, camera.FarClip - 1.0f })
    {
        const glm::vec3 top = ToNdc(view.Projection, glm::vec3(0.0f, 3.0f, -distance));
        const glm::vec3 bottom = ToNdc(view.Projection, glm::vec3(0.0f, -3.0f, -distance));
        const glm::vec3 right = ToNdc(view.Projection, glm::vec3(3.0f * kAspect, 0.0f, -distance));
        const glm::vec3 left = ToNdc(view.Projection, glm::vec3(-3.0f * kAspect, 0.0f, -distance));

        CHECK_THAT(top.y, WithinAbs(1.0f, 1e-4f));
        CHECK_THAT(bottom.y, WithinAbs(-1.0f, 1e-4f));
        CHECK_THAT(right.x, WithinAbs(1.0f, 1e-4f));
        CHECK_THAT(left.x, WithinAbs(-1.0f, 1e-4f));
    }

    CHECK_THAT(ToNdc(view.Projection, glm::vec3(0.0f, 0.0f, -0.5f)).z, WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(ToNdc(view.Projection, glm::vec3(0.0f, 0.0f, -100.0f)).z, WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("The main camera is the referenced one while it can be used", "[scene camera]")
{
    Scene scene;
    const Entity first = AddCamera(scene, "First");
    const Entity second = AddCamera(scene, "Second");

    CHECK(FindMainCamera(scene, IdOf(second)) == second);
    CHECK(FindMainCamera(scene, IdOf(first)) == first);
}

TEST_CASE("The main camera falls back to the first active camera in hierarchy order", "[scene camera]")
{
    Scene scene;
    const Entity firstRoot = scene.CreateEntity("First Root");
    const Entity secondRoot = AddCamera(scene, "Second Root");
    const Entity child = AddCamera(scene, "Child", firstRoot);
    const Entity referenced = AddCamera(scene, "Referenced", secondRoot);

    SECTION("An unset reference")
    {
        CHECK(FindMainCamera(scene, {}) == child);
    }

    SECTION("A reference to nothing")
    {
        CHECK(FindMainCamera(scene, AssetID::Generate()) == child);
    }

    SECTION("A reference to an entity without a camera")
    {
        CHECK(FindMainCamera(scene, IdOf(firstRoot)) == child);
    }

    SECTION("A reference to an inactive camera")
    {
        referenced.GetComponent<NameComponent>().Active = false;
        CHECK(FindMainCamera(scene, IdOf(referenced)) == child);
    }

    SECTION("A reference under an inactive parent")
    {
        secondRoot.GetComponent<NameComponent>().Active = false;
        CHECK(FindMainCamera(scene, IdOf(referenced)) == child);
    }

    SECTION("The first camera is inactive")
    {
        child.GetComponent<NameComponent>().Active = false;
        CHECK(FindMainCamera(scene, {}) == secondRoot);
    }

    SECTION("The first camera's parent is inactive")
    {
        firstRoot.GetComponent<NameComponent>().Active = false;
        CHECK(FindMainCamera(scene, {}) == secondRoot);
    }
}

TEST_CASE("There is no main camera without an active camera", "[scene camera]")
{
    Scene scene;
    CHECK_FALSE(FindMainCamera(scene, {}));

    const Entity camera = AddCamera(scene, "Camera");
    camera.GetComponent<NameComponent>().Active = false;

    CHECK_FALSE(FindMainCamera(scene, {}));
    CHECK_FALSE(FindMainCamera(scene, IdOf(camera)));
}

TEST_CASE("A camera squashed along one axis still has a view", "[scene camera]")
{
    Scene scene;
    const Entity entity = scene.CreateEntity("Camera");
    auto& transform = entity.GetComponent<TransformComponent>();

    const auto isFinite = [&]
    {
        const CameraView view = ComputeCameraView(CameraComponent{}, scene.ComputeWorldMatrix(entity), kAspect);
        for (int column = 0; column < 4; ++column)
        {
            const glm::vec4& v = view.View[column];
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w))
                return false;
        }

        return true;
    };

    SECTION("Pitched straight down with no height")
    {
        transform.RotationDegrees = glm::vec3(-90.0f, 0.0f, 0.0f);
        transform.Scale = glm::vec3(1.0f, 0.0f, 1.0f);
        CHECK(isFinite());
    }

    SECTION("Pitched straight up with no depth")
    {
        transform.RotationDegrees = glm::vec3(90.0f, 0.0f, 0.0f);
        transform.Scale = glm::vec3(1.0f, 1.0f, 0.0f);
        CHECK(isFinite());
    }
}
