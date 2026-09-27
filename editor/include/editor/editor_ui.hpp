#include "imgui.h"
#include <vector>
#include <string>

#include "glm/fwd.hpp"

struct Entity {
    int id;
    std::string name;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    float rotation[3] = { 0.0f, 0.0f, 0.0f };
    float scale[3]    = { 1.0f, 1.0f, 1.0f };
    float color[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
};

class EditorUI {
private:
    std::vector<Entity> m_Entities;
    int m_SelectedEntityIndex = -1;
    glm::uint32_t m_ViewportTextureID = 0;

public:
    EditorUI() {
        m_Entities.push_back({ 0, "Main Camera", { 0.0f, 2.0f, -10.0f } });
        m_Entities.push_back({ 1, "Directional Light", { 5.0f, 10.0f, 5.0f } });
        m_Entities.push_back({ 2, "Player Cube", { 0.0f, 0.0f, 0.0f } });
        m_Entities.push_back({ 3, "Floor Plane", { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 10.0f, 1.0f, 10.0f } });
    }

    void OnImGuiRender() {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        RenderMainMenuBar();
        RenderHierarchy();
        RenderInspector();
        RenderViewport();
        RenderConsole();
    }

private:
    void RenderMainMenuBar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {}
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {}
                if (ImGui::MenuItem("Save", "Ctrl+S")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void RenderHierarchy() {
        ImGui::Begin("Hierarchy");

        for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            
            if (m_SelectedEntityIndex == i) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            // Entitások listázása fa csomópontként
            bool opened = ImGui::TreeNodeEx((void*)(intptr_t)m_Entities[i].id, flags, "%s", m_Entities[i].name.c_str());

            // Kijelölés kezelése
            if (ImGui::IsItemClicked()) {
                m_SelectedEntityIndex = i;
            }

            if (opened) {
                // Ide jöhetnének a gyermek entitások (Child Objects)
                ImGui::TreePop();
            }
        }

        // Kattintás az üres területre -> kijelölés megszüntetése
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            m_SelectedEntityIndex = -1;
        }

        ImGui::End();
    }

    void RenderInspector() {
        ImGui::Begin("Inspector");

        if (m_SelectedEntityIndex >= 0 && m_SelectedEntityIndex < static_cast<int>(m_Entities.size())) {
            Entity& entity = m_Entities[m_SelectedEntityIndex];

            // Entitás neve
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, entity.name.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                entity.name = std::string(buffer);
            }

            ImGui::Separator();

            // Transform komponens
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", entity.position, 0.1f);
                ImGui::DragFloat3("Rotation", entity.rotation, 1.0f);
                ImGui::DragFloat3("Scale", entity.scale, 0.1f);
            }

            // Material / Color komponens példa
            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::ColorEdit4("Albedo Color", entity.color);
            }
        } else {
            ImGui::TextDisabled("Válassz ki egy entitást a Hierarchy ablakban!");
        }

        ImGui::End();
    }

    void RenderViewport() {
        // A padding-ot kikapcsolhatjuk, hogy a 3D nézet kitöltse az egész ablakot
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene");

        // A rendelkezésre álló terület méretének lekérdezése (pl. Framebuffer átméretezéséhez)
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

        if (m_ViewportTextureID != 0) {
            // Ha van valódi Framebuffer / Render Texture:
            ImGui::Image((void*)(intptr_t)m_ViewportTextureID, viewportSize, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            // Helykitöltő szöveg a demóhoz
            ImVec2 windowCenter = ImGui::GetWindowPos();
            windowCenter.x += viewportSize.x * 0.5f - 80.0f;
            windowCenter.y += viewportSize.y * 0.5f;
            ImGui::SetCursorScreenPos(windowCenter);
            ImGui::Text("3D Scene Viewport\n( Render Target )");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void RenderConsole() {
        ImGui::Begin("Console");
        
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[INFO]: Engine initialized successfully.");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "[WARN]: Material 'Default' is missing normal map.");
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[INFO]: Loaded 4 scene entities.");
        
        ImGui::End();
    }
};