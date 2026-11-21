#include "crimson_editor/ui/scene_settings_panel.hpp"

#include <array>
#include <string>
#include <vector>

#include <imgui.h>

#include <crimson/scene/scene_camera.hpp>
#include <crimson/scene/scene_serializer.hpp>

#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/linear_color.hpp"
#include "crimson_editor/ui/property_fields.hpp"

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
        linear_color::Edit("Ambient", &m_context.Session.GetSettings().Ambient.x, 3);
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

        if (!ImGui::BeginCombo("Main Camera", preview.c_str()))
            return;

        AssetID chosen = settings.MainCamera;

        if (ImGui::Selectable("Automatic", !settings.MainCamera))
            chosen = {};

        std::vector<Entity> cameras;
        for (const entt::entity root : scene.GetRoots())
            CollectCameras(scene, root, cameras);

        for (const Entity camera : cameras)
        {
            const AssetID id = camera.GetComponent<NameComponent>().Id;

            ImGui::PushID(id.ToString().c_str());
            if (ImGui::Selectable(nameOf(camera).c_str(), id == settings.MainCamera))
                chosen = id;
            ImGui::PopID();
        }

        ImGui::EndCombo();

        if (chosen != settings.MainCamera)
        {
            settings.MainCamera = chosen;

            // A step at once, the state from before it is already recorded
            m_context.Session.MarkEdited();
        }
    }

    void SceneSettingsPanel::DrawPostProcessing()
    {
        theme::SectionTitle("Post-processing");

        PostProcessSettings& post = m_context.Session.GetSettings().PostProcessing;

        constexpr std::array kToneMapperNames = { "ACES", "None" };

        // The label sits after the widgets, as it does for the other fields of this panel
        const float labelWidth = ImGui::CalcTextSize("Exposure").x + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - labelWidth);
        ImGui::PushID("Exposure");
        bool changed = props::SliderField(post.Exposure, 0.05f, 16.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::PopID();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted("Exposure");

        int toneMapper = static_cast<int>(post.Mapper);
        if (ImGui::Combo("Tone Mapper", &toneMapper, kToneMapperNames.data(), static_cast<int>(kToneMapperNames.size())))
        {
            post.Mapper = static_cast<ToneMapper>(toneMapper);
            changed = true;
        }

        if (changed)
            m_context.Session.MarkEdited();
    }
}
