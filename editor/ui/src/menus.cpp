#include "crimson_editor/ui/menus.hpp"

#include <string>
#include <utility>

#include <imgui.h>

#include "imgui_convert.hpp"
#include "placement.hpp"

namespace crimson::editor::ui
{
    Menu::Menu(std::string_view label)
    {
        m_open = ImGui::BeginMenu(std::string(label).c_str());
    }

    Menu::~Menu()
    {
        if (m_open)
            ImGui::EndMenu();
    }

    bool MenuItem(std::string_view label, std::string_view shortcut, bool enabled, bool checked)
    {
        const std::string labelText(label);
        const std::string shortcutText(shortcut);

        return ImGui::MenuItem(labelText.c_str(), shortcutText.empty() ? nullptr : shortcutText.c_str(), checked, enabled);
    }

    ContextMenu::ContextMenu(std::string_view id)
    {
        const std::string name(id);
        m_open = ImGui::BeginPopupContextItem(name.empty() ? nullptr : name.c_str());
    }

    ContextMenu::~ContextMenu()
    {
        if (m_open)
            ImGui::EndPopup();
    }

    BackgroundContextMenu::BackgroundContextMenu(std::string_view id)
    {
        constexpr ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems;
        m_open = ImGui::BeginPopupContextWindow(std::string(id).c_str(), flags);
    }

    BackgroundContextMenu::~BackgroundContextMenu()
    {
        if (m_open)
            ImGui::EndPopup();
    }

    Popup::Popup(std::string_view id, PopupOptions options)
    {
        if (options.Position)
            ImGui::SetNextWindowPos(ToImVec2(*options.Position), ImGuiCond_Appearing);

        m_open = ImGui::BeginPopup(std::string(id).c_str(), options.NoNavigation ? ImGuiWindowFlags_NoNavInputs : ImGuiWindowFlags_None);
    }

    Popup::~Popup()
    {
        if (m_open)
            ImGui::EndPopup();
    }

    bool Popup::IsAppearing() const
    {
        return ImGui::IsWindowAppearing();
    }

    void Popup::Close()
    {
        ImGui::CloseCurrentPopup();
    }

    void Popup::Open(std::string_view id)
    {
        ImGui::OpenPopup(std::string(id).c_str());
    }

    bool Popup::IsOpen(std::string_view id)
    {
        return ImGui::IsPopupOpen(std::string(id).c_str());
    }

    Modal::Modal(std::string_view title, std::string_view id, std::function<bool()> shown, ModalOptions options)
        : m_shown(std::move(shown))
    {
        // The part after ### is the popup's id, so the title can say what it is doing
        const std::string name = std::string(title) + "###" + std::string(id);
        const std::string key = "###" + std::string(id);

        if (m_shown() && !ImGui::IsPopupOpen(key.c_str()))
            ImGui::OpenPopup(name.c_str());

        ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

        if (options.ScreenWidth > 0.0f)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * options.ScreenWidth, 0.0f), ImGuiCond_Appearing);
            flags = ImGuiWindowFlags_NoSavedSettings;
        }

        m_open = ImGui::BeginPopupModal(name.c_str(), nullptr, flags);

        // Nothing wants it open anymore
        if (m_open && !m_shown())
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_open = false;
        }
    }

    Modal::~Modal()
    {
        if (!m_open)
            return;

        // What the content did to the state closes it now, rather than a frame later
        if (!m_shown())
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    bool Modal::IsAppearing() const
    {
        return ImGui::IsWindowAppearing();
    }
}
