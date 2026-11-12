#pragma once
#include <string>

#include <glm/vec3.hpp>

namespace crimson::editor::ui::props
{
    // The Inspector's rows: a label in a column on the left, and the value filling the rest of the row. Every field
    // takes what it resets to, or nothing if it can't be reset. Right-clicking a row's label or its value offers
    // "Reset to default". Vector fields mark their axes with a thin colored stripe, which is only drawn.
    //
    //   if (props::BeginTable("Transform"))
    //   {
    //       props::Vec3("Position", transform.Position, 0.05f, &kDefaults.Position);
    //       props::EndTable();
    //   }

    bool BeginTable(const char* id);
    void EndTable();

    // For a row the fields below don't cover: BeginRow, then the widgets, each followed by ResetTarget if
    // right-clicking it should offer the reset, then EndRow. EndRow returns true when the reset was picked.
    void BeginRow(const char* label);
    void ResetTarget();
    bool EndRow(bool resettable);

    // Each returns true when the value changed, by editing or by a reset
    bool Float(const char* label, float& value, float speed, float min = 0.0f, float max = 0.0f, const float* reset = nullptr);
    bool Int(const char* label, int& value, float speed, int min = 0, int max = 0, const int* reset = nullptr);
    bool String(const char* label, std::string& value, const std::string* reset = nullptr); // up to 255 bytes
    bool Slider(const char* label, float& value, float min, float max, const float* reset = nullptr);
    bool Floats(const char* label, float* values, int count, float speed, const float* reset = nullptr);
    bool Vec3(const char* label, glm::vec3& value, float speed, const glm::vec3* reset = nullptr);
    bool Color(const char* label, float* linearColor, int channels, const float* reset = nullptr);
    bool Checkbox(const char* label, bool& value, const bool* reset = nullptr);
    bool Combo(const char* label, int& value, const char* const* items, int count, const int* reset = nullptr);

    // A read-only row, for details such as a texture's size
    void Text(const char* label, const char* value);
}
