/**
 * @file koralInspector.h
 * @brief An editor for any reflected object: what a property panel is built from.
 *
 * @code
 * struct Light { glm::vec3 position; glm::vec3 color{1.f}; float intensity = 1.f; };
 * inline void KoralReflect(kor::TypeBuilder<Light>& t) {
 *     t.Name("Light");
 *     t.Field("position", &Light::position);
 *     t.Field("color", &Light::color).Color();
 *     t.Field("intensity", &Light::intensity).Range(0.f, 10.f);
 * }
 *
 * void MyScene::RenderUI() {
 *     ImGui::Begin("Properties");
 *     if (kgui::Inspect("Light", _light)) _lightsDirty = true;
 *     ImGui::End();
 * }
 * @endcode
 *
 * Every field gets the editor its kind calls for: a checkbox, a drag (a slider where the field has a
 * range), a text box, a colour picker for a field marked Color(), a combo for an enum, a tree for a
 * struct, and a tree with + and − for an array. A read-only field is shown disabled, a field's tooltip
 * on hover. A quaternion is edited as Euler angles in degrees.
 */

#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

#include <glm/gtc/quaternion.hpp>

#include <reflect.h>

namespace kgui
{
    namespace detail
    {
        inline int ResizeString(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* text = static_cast<std::string*>(data->UserData);
                text->resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = text->data();
            }
            return 0;
        }

        template<typename T>
        bool Scalar(const char* label, const ImGuiDataType type, void* value, const kor::FieldInfo* field)
        {
            if (field && field->min && field->max) {
                const T min = static_cast<T>(*field->min), max = static_cast<T>(*field->max);
                return ImGui::SliderScalar(label, type, value, &min, &max);
            }
            const float speed = type == ImGuiDataType_Float || type == ImGuiDataType_Double ? 0.01f : 0.2f;
            return ImGui::DragScalar(label, type, value, speed);
        }

        inline bool Value(const char* label, kor::Ref value, const kor::FieldInfo* field);

        inline bool Fields(kor::Ref value)
        {
            bool changed = false;
            for (const auto& field : value.Type().fields) {
                ImGui::PushID(field.name.c_str());
                changed |= Value(field.name.c_str(), value.Field(field.name), &field);
                ImGui::PopID();
            }
            return changed;
        }

        inline bool Value(const char* label, kor::Ref value, const kor::FieldInfo* field)
        {
            using kor::TypeKind;
            const auto& type = value.Type();
            void* address = value.Address();
            const bool readOnly = field && field->readOnly;
            if (readOnly) ImGui::BeginDisabled();

            bool changed = false;
            switch (type.kind) {
            case TypeKind::eBool: changed = ImGui::Checkbox(label, static_cast<bool*>(address)); break;
            case TypeKind::eInt8: changed = Scalar<std::int8_t>(label, ImGuiDataType_S8, address, field); break;
            case TypeKind::eInt16: changed = Scalar<std::int16_t>(label, ImGuiDataType_S16, address, field); break;
            case TypeKind::eInt32: changed = Scalar<std::int32_t>(label, ImGuiDataType_S32, address, field); break;
            case TypeKind::eInt64: changed = Scalar<std::int64_t>(label, ImGuiDataType_S64, address, field); break;
            case TypeKind::eUInt8: changed = Scalar<std::uint8_t>(label, ImGuiDataType_U8, address, field); break;
            case TypeKind::eUInt16: changed = Scalar<std::uint16_t>(label, ImGuiDataType_U16, address, field); break;
            case TypeKind::eUInt32: changed = Scalar<std::uint32_t>(label, ImGuiDataType_U32, address, field); break;
            case TypeKind::eUInt64: changed = Scalar<std::uint64_t>(label, ImGuiDataType_U64, address, field); break;
            case TypeKind::eFloat: changed = Scalar<float>(label, ImGuiDataType_Float, address, field); break;
            case TypeKind::eDouble: changed = Scalar<double>(label, ImGuiDataType_Double, address, field); break;
            case TypeKind::eString: {
                auto& text = *static_cast<std::string*>(address);
                changed = ImGui::InputText(label, text.data(), text.capacity() + 1,
                                           ImGuiInputTextFlags_CallbackResize, ResizeString, &text);
                break;
            }
            case TypeKind::eVec2: changed = ImGui::DragFloat2(label, static_cast<float*>(address), 0.01f); break;
            case TypeKind::eVec3:
                changed = field && field->color ? ImGui::ColorEdit3(label, static_cast<float*>(address))
                                                : ImGui::DragFloat3(label, static_cast<float*>(address), 0.01f);
                break;
            case TypeKind::eVec4:
                changed = field && field->color ? ImGui::ColorEdit4(label, static_cast<float*>(address))
                                                : ImGui::DragFloat4(label, static_cast<float*>(address), 0.01f);
                break;
            case TypeKind::eIVec2: changed = ImGui::DragInt2(label, static_cast<int*>(address)); break;
            case TypeKind::eIVec3: changed = ImGui::DragInt3(label, static_cast<int*>(address)); break;
            case TypeKind::eIVec4: changed = ImGui::DragInt4(label, static_cast<int*>(address)); break;
            case TypeKind::eUVec2: changed = ImGui::DragScalarN(label, ImGuiDataType_U32, address, 2); break;
            case TypeKind::eUVec3: changed = ImGui::DragScalarN(label, ImGuiDataType_U32, address, 3); break;
            case TypeKind::eUVec4: changed = ImGui::DragScalarN(label, ImGuiDataType_U32, address, 4); break;
            case TypeKind::eQuat: {
                auto& rotation = *static_cast<glm::quat*>(address);
                glm::vec3 degrees = glm::degrees(glm::eulerAngles(rotation));
                if (ImGui::DragFloat3(label, &degrees.x, 0.5f)) {
                    rotation = glm::quat(glm::radians(degrees));
                    changed = true;
                }
                break;
            }
            case TypeKind::eMat4:
                if (ImGui::TreeNode(label)) {
                    auto& matrix = *static_cast<glm::mat4*>(address);
                    for (int column = 0; column < 4; ++column) {
                        ImGui::PushID(column);
                        changed |= ImGui::DragFloat4("##column", &matrix[column].x, 0.01f);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                break;
            case TypeKind::eEnum: {
                const auto current = type.enumRead(address);
                if (type.enumerators.empty()) {
                    auto value64 = current;
                    if (ImGui::InputScalar(label, ImGuiDataType_S64, &value64)) { type.enumWrite(address, value64); changed = true; }
                    break;
                }
                const std::string preview(type.EnumName(current));
                if (ImGui::BeginCombo(label, preview.c_str())) {
                    for (const auto& [name, enumerator] : type.enumerators) {
                        if (ImGui::Selectable(name.c_str(), enumerator == current) && enumerator != current) {
                            type.enumWrite(address, enumerator);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case TypeKind::eStruct:
                if (ImGui::TreeNode(label)) {
                    changed = Fields(value);
                    ImGui::TreePop();
                }
                break;
            case TypeKind::eArray:
                if (ImGui::TreeNode(label, "%s [%zu]", label, value.Size())) {
                    if (ImGui::SmallButton("+")) { value.Resize(value.Size() + 1); changed = true; }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-") && value.Size() > 0) { value.Resize(value.Size() - 1); changed = true; }
                    for (std::size_t i = 0; i < value.Size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        const std::string element = "[" + std::to_string(i) + "]";
                        changed |= Value(element.c_str(), value.At(i), nullptr);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                break;
            }

            if (readOnly) ImGui::EndDisabled();
            if (field && !field->tooltip.empty()) ImGui::SetItemTooltip("%s", field->tooltip.c_str());
            return changed;
        }
    }

    /**
     * @brief Draws an editor for every field of @p value, under @p label.
     * @return Whether anything was changed this frame.
     *
     * A struct is shown as its fields directly — the label heads them — and anything else as the one
     * editor its kind calls for.
     */
    inline bool Inspect(const char* label, const kor::Ref value)
    {
        if (!value.Valid()) return false;
        ImGui::PushID(label);
        bool changed;
        if (value.Type().kind == kor::TypeKind::eStruct) {
            ImGui::SeparatorText(label);
            changed = detail::Fields(value);
        } else {
            changed = detail::Value(label, value, nullptr);
        }
        ImGui::PopID();
        return changed;
    }
}
