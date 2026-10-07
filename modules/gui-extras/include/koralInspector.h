/**
 * @file koralInspector.h
 * @brief An editor for any reflected object: what a property panel is built from.
 *
 * @code
 * struct Light { kor::Vec3 position; kor::Vec3 color{1.f}; float intensity = 1.f; };
 * inline void KoralReflect(kor::TypeBuilder<Light>& t) {
 *     t.Name("Light");
 *     t.Field("position", &Light::position);
 *     t.Field("color", &Light::color).Color();
 *     t.Field("intensity", &Light::intensity).Range(0.f, 10.f);
 * }
 * kui::Ui _ui { kgui::Inspector(_light, [this] { _lightsDirty = true; }) };
 * @endcode
 *
 * Every field gets the editor its kind calls for: a checkbox, a drag (kept to the field's range where
 * it has one), a text box, a colour picker for a field marked Color(), a dropdown for an enum, a tree
 * for a struct, and a tree with + and − for an array. A read-only field is shown disabled, a field's
 * tooltip on hover. A quaternion is edited as Euler angles in degrees.
 *
 * The object is edited where it is — the inspector holds a reference to it, which must outlive the
 * widget. It is read again every frame, so a value changed by the program shows straight away.
 */

#pragma once

#include <functional>
#include <string>

#include <reflect.h>

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    /**
     * @brief Editors for every field of @p value. @p onChanged is called after each edit; @p label, when
     *        given, heads them.
     */
    KGUI_API kui::Widget Inspector(kor::Ref value, std::function<void()> onChanged = {}, std::string label = {});
    /**
     * @brief The same, for an object that can move: @p value is asked where it is each time it is read or
     *        written — a component an ECS keeps in an array that is reshuffled, say. An invalid Ref is
     *        nothing to inspect.
     */
    KGUI_API kui::Widget Inspector(std::function<kor::Ref()> value, std::function<void()> onChanged = {}, std::string label = {});
}
