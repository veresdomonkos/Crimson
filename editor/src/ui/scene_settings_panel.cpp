#include "crimson_editor/ui/scene_settings_panel.hpp"

#include <array>
#include <string>
#include <vector>

#include <crimson/scene/scene_camera.hpp>
#include <crimson/scene/scene_serializer.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/linear_color.hpp"
#include "crimson_editor/ui/property_fields.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor::ui
{
    // The cameras that can be the main one, in the order the Hierarchy shows them. What is inside an instance can't be
    // referenced.
    static void CollectCameras(Scene& scene, entt::entity handle, std::vector<Entity>& cameras)
    {
        const Entity entity = scene.GetEntity(handle);
        if (entity.HasComponent<InstancedComponent>())
            return;

        if (entity.HasComponent<CameraComponent>())
            cameras.push_back(entity);

        for (const entt::entity child : entity.GetComponent<RelationshipComponent>().Children)
            CollectCameras(scene, child, cameras);
    }

    void SceneSettingsPanel::Draw()
    {
        ui::linear_color::Edit("Ambient", &m_context.Session.GetSettings().Ambient.x, 3);
        DrawMainCamera();
        DrawPostProcessing();
    }

    void SceneSettingsPanel::DrawMainCamera()
    {
        Scene& scene = m_context.Session.GetScene();
        SceneSettings& settings = m_context.Session.GetSettings();

        const auto nameOf = [](Entity entity) { return entity.GetComponent<NameComponent>().Name; };

        std::string preview;
        if (!settings.MainCamera)
        {
            const Entity first = FindMainCamera(scene, {});
            preview = "Automatic (" + (first ? nameOf(first) : std::string("no cameras")) + ")";
        }
        else if (const Entity referenced = FindEntity(scene, settings.MainCamera); referenced && referenced.HasComponent<CameraComponent>())
        {
            preview = nameOf(referenced);
        }
        else
        {
            preview = "Missing camera";
        }

        const ui::Combo combo("Main Camera", preview);
        if (!combo)
            return;

        AssetID chosen = settings.MainCamera;

        if (ui::Selectable("Automatic", !settings.MainCamera))
            chosen = {};

        std::vector<Entity> cameras;
        for (const entt::entity root : scene.GetRoots())
            CollectCameras(scene, root, cameras);

        for (const Entity camera : cameras)
        {
            const AssetID id = camera.GetComponent<NameComponent>().Id;

            const ui::IdScope scope(id.ToString());
            if (ui::Selectable(nameOf(camera), id == settings.MainCamera))
                chosen = id;
        }

        if (chosen != settings.MainCamera)
        {
            settings.MainCamera = chosen;

            // A step at once, the state from before it is already recorded
            m_context.Session.MarkEdited();
        }
    }

    void SceneSettingsPanel::DrawPostProcessing()
    {
        ui::SectionTitle("Post-processing");

        PostProcessSettings& post = m_context.Session.GetSettings().PostProcessing;

        constexpr std::array kToneMapperNames = { "ACES", "None" };

        // The label sits after the widgets, as it does for the other fields of this panel
        ui::NextItemWidth(ui::ItemWidth() - (ui::TextSize("Exposure").x + ui::InnerSpacing().x));
        bool changed;
        {
            const ui::IdScope scope("Exposure");
            changed = ui::props::SliderField(post.Exposure, 0.05f, 16.0f, "%.2f", ui::props::SliderScale::Logarithmic);
        }

        {
            ui::Row row(true);
            row.Gap(ui::InnerSpacing().x);
            ui::Text("Exposure");
        }

        {
            const int current = static_cast<int>(post.Mapper);
            const ui::Combo combo("Tone Mapper", kToneMapperNames[current]);

            if (combo)
            {
                for (int mapper = 0; mapper < static_cast<int>(kToneMapperNames.size()); ++mapper)
                {
                    if (ui::Selectable(kToneMapperNames[mapper], mapper == current))
                    {
                        post.Mapper = static_cast<ToneMapper>(mapper);
                        changed = true;
                    }
                }
            }
        }

        if (changed)
            m_context.Session.MarkEdited();
    }
}
