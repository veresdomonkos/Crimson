#include <catch2/catch_test_macros.hpp>

#include <crimson/renderer/camera_data.hpp>
#include <crimson/scene/scene_camera.hpp>
#include <crimson_editor/scene_view.hpp>

using namespace crimson;
using crimson::editor::SceneView;

namespace
{
    constexpr float kAspect = 16.0f / 9.0f;

    bool SameView(const CameraView& a, const CameraView& b)
    {
        return a.View == b.View && a.Projection == b.Projection && a.Position == b.Position;
    }

    struct Fixture
    {
        Scene World;
        PerspectiveCamera Camera{ glm::vec3(0.0f, 3.0f, 8.0f), 70.0f, kAspect };
        SceneView View{ World, Camera, kAspect };
        Entity Eye = MakeEye();

        Entity MakeEye()
        {
            const Entity entity = World.CreateEntity("Eye");
            entity.AddComponent<CameraComponent>(CameraComponent{ .FieldOfViewDegrees = 40.0f });
            entity.GetComponent<TransformComponent>().Position = glm::vec3(2.0f, 1.0f, -4.0f);
            return entity;
        }
    };
}

TEST_CASE_METHOD(Fixture, "The Scene view shows the editor camera unless looking through one", "[scene view]")
{
    CHECK_FALSE(View.IsLookingThrough());
    CHECK(SameView(View.GetView(), Camera.GetCameraView()));
}

TEST_CASE_METHOD(Fixture, "Looking through a camera shows its view and leaves the editor camera where it was", "[scene view]")
{
    const CameraView before = Camera.GetCameraView();

    View.LookThrough(Eye);
    CHECK(View.IsLookingThrough());
    CHECK(View.GetLookThrough() == Eye);
    CHECK(SameView(View.GetView(), ComputeCameraView(Eye.GetComponent<CameraComponent>(), World.ComputeWorldMatrix(Eye), kAspect)));

    // Moving the camera moves the view at once
    Eye.GetComponent<TransformComponent>().Position = glm::vec3(0.0f, 9.0f, 0.0f);
    CHECK(View.GetView().Position == glm::vec3(0.0f, 9.0f, 0.0f));

    View.StopLookingThrough();
    CHECK_FALSE(View.IsLookingThrough());
    CHECK(SameView(View.GetView(), before));
}

TEST_CASE_METHOD(Fixture, "Only a camera can be looked through", "[scene view]")
{
    const Entity plain = World.CreateEntity("Plain");

    View.LookThrough(plain);
    CHECK_FALSE(View.IsLookingThrough());

    View.LookThrough(Entity{});
    CHECK_FALSE(View.IsLookingThrough());

    // Anything else stops looking through
    View.LookThrough(Eye);
    REQUIRE(View.IsLookingThrough());
    View.LookThrough(plain);
    CHECK_FALSE(View.IsLookingThrough());
}

TEST_CASE_METHOD(Fixture, "Looking through stops when the camera goes", "[scene view]")
{
    View.LookThrough(Eye);

    SECTION("It is destroyed")
    {
        World.DestroyEntity(Eye);

        CHECK(SameView(View.GetView(), Camera.GetCameraView()));
        View.Update();
        CHECK_FALSE(View.IsLookingThrough());

        // A new entity taking the slot isn't the camera that was looked through
        const Entity other = World.CreateEntity("Other");
        other.AddComponent<CameraComponent>();
        View.Update();
        CHECK_FALSE(View.IsLookingThrough());
        CHECK(SameView(View.GetView(), Camera.GetCameraView()));
    }

    SECTION("It loses its camera")
    {
        Eye.RemoveComponent<CameraComponent>();

        CHECK(SameView(View.GetView(), Camera.GetCameraView()));
        View.Update();
        CHECK_FALSE(View.IsLookingThrough());
    }
}
