#pragma once

/**
 * @file builderDescriptions.h
 * @brief Every builder Koral has, described: its settings, what each takes, and a way to build with one from
 *        values alone.
 *
 * What a tool needs to offer Koral's objects without knowing them in advance — an editor that shows one node
 * per builder, whose inputs are the builder's settings. The descriptions are generated from the C interface
 * (scripts/builders/generate.py), which has a builder for every object, so they keep step with it; the same
 * descriptions ship as JSON, in share/Koral/builders.json.
 *
 * A setting that is never given leaves the builder's own default in place, so no default is restated here.
 * What a build needs and was not given fails it, with the builder's own error.
 *
 * @code
 * const auto* image = kor::builders::Find("Image");
 * auto built = kor::builders::Build(*image, {}, {
 *     { image->FindSetting("format"), { kor::builders::Value::Of(std::uint64_t(kor::Image::Format::eRGBA8_UNORM)) } },
 *     { image->FindSetting("extent"), { Value::Of(64u), Value::Of(64u), Value::Of(1u) } },
 * });
 * @endcode
 */

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "api.h"
#include "error.h"
#include "koral_c.h"

namespace kor::builders
{
    /** @brief What a value is. */
    enum class Kind : std::uint8_t {
        eBool,
        eInt,       ///< A signed integer; Type::bits says how wide.
        eUInt,      ///< An unsigned integer; Type::bits says how wide.
        eFloat,     ///< A float (32 bits) or double (64).
        eString,    ///< Text; an empty Value passes null where the C interface takes it.
        eBytes,     ///< Raw bytes: initial data.
        eEnum,      ///< One of Type::enumeration's values.
        eFlags,     ///< Any of Type::enumeration's values, or-ed together.
        eChoice,    ///< One of a few numbered alternatives (Type::enumeration lists them) that are not a C++ enumeration.
        eResource,  ///< A Koral object; Type::name says which kind ("Image"), or "Resource" for any.
        eArray,     ///< Type::length of Type::element.
        eList,      ///< Any number of Type::element.
        eStruct,    ///< Type::fields, in order.
        eUnion,     ///< One of Type::fields.
    };

    struct Field;

    /** @brief One value an enumeration (or a choice) has. */
    struct Enumerator {
        std::string_view name;
        std::uint64_t value = 0;
    };

    /** @brief A C++ enumeration a setting takes, by its qualified name ("kor::Image::Format"), with its values. */
    struct Enumeration {
        std::string_view name;
        bool flags = false;
        std::span<const Enumerator> values;
    };

    /** @brief The type of a setting's argument, or of a field of one. */
    struct Type {
        Kind kind = Kind::eUInt;
        std::uint8_t bits = 32;                 ///< For integers and floats.
        std::string_view name;                  ///< The enumeration, struct or resource kind it names; empty otherwise.
        std::uint32_t length = 0;               ///< For an array.
        const Type* element = nullptr;          ///< For an array or a list.
        std::span<const Field> fields;          ///< For a struct or a union.
        const Enumeration* enumeration = nullptr;   ///< For an enum, flags or a choice.
    };

    /** @brief One argument of a setting, or one field of a struct. */
    struct Field {
        std::string_view name;
        const Type* type = nullptr;
        std::string_view doc;
    };

    struct Value;
    /** @brief Calls one C function with values. Generated. */
    using Invoke = void (*)(KoralBuilder* builder, std::span<const Value> arguments);

    /** @brief One thing a builder can be told: a C function, and what it takes besides the builder. */
    struct Setting {
        std::string_view name;          ///< "format", "extent", "color" (an Add* setting).
        std::string_view function;      ///< "koral_image_builder_set_format".
        bool repeatable = false;        ///< An Add*: given any number of times, each adding one.
        std::span<const Field> arguments;
        std::string_view doc;
        Invoke invoke = nullptr;
    };

    using Create = KoralBuilder* (*)(std::span<const Value> arguments);
    using Finish = KoralResource* (*)(KoralBuilder* builder);

    /** @brief A builder: what it makes, what making one takes, and every setting it has. */
    struct Description {
        std::string_view name;          ///< "Image", "ComputePipeline".
        std::string_view result;        ///< The kind of resource it builds, as Type::name spells resources.
        std::span<const Field> constructor;     ///< What making the builder takes: an ImageView's image.
        std::span<const Setting> settings;
        std::string_view doc;
        Create create = nullptr;
        Finish finish = nullptr;

        [[nodiscard]] KORAL_API const Setting* FindSetting(std::string_view name) const;
    };

    /** @brief Every builder, in the order the C interface declares them. */
    [[nodiscard]] KORAL_API std::span<const Description> All();
    /** @brief The builder so named, or null. */
    [[nodiscard]] KORAL_API const Description* Find(std::string_view name);
    /** @brief Every enumeration a builder takes. */
    [[nodiscard]] KORAL_API std::span<const Enumeration> Enumerations();

    /**
     * @brief A value for an argument or a field.
     *
     * Integers, enumerations, flags and choices are integers (signed or not, as convenient); floats are
     * doubles; arrays, lists and structs are lists of values (a struct's in its fields' order); a union is
     * two values, which of its fields and that field's value. An empty value is null for a string or a
     * resource, and zero for anything else.
     */
    struct KORAL_API Value {
        std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, KoralResource*,
                     std::vector<std::byte>, std::vector<Value>> data;

        template <typename T>
        static Value Of(T value)
        {
            Value v;
            if constexpr (std::is_same_v<T, bool>) v.data = value;
            else if constexpr (std::is_floating_point_v<T>) v.data = static_cast<double>(value);
            else if constexpr (std::is_enum_v<T>) v.data = static_cast<std::uint64_t>(value);
            else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) v.data = static_cast<std::int64_t>(value);
            else if constexpr (std::is_integral_v<T>) v.data = static_cast<std::uint64_t>(value);
            else if constexpr (std::is_convertible_v<T, std::string>) v.data = std::string(value);
            else if constexpr (std::is_same_v<T, KoralResource*>) v.data = value;
            else v.data = std::move(value);
            return v;
        }
        static Value List(std::vector<Value> values) { Value v; v.data = std::move(values); return v; }

        [[nodiscard]] std::uint64_t UInt() const;
        [[nodiscard]] std::int64_t Int() const;
        [[nodiscard]] double Float() const;
        [[nodiscard]] bool Bool() const;
        /** @brief The text, or null for an empty value. Lives as long as this value. */
        [[nodiscard]] const char* CString() const;
        [[nodiscard]] KoralResource* Resource() const;
        [[nodiscard]] std::span<const std::byte> Bytes() const;
        [[nodiscard]] std::span<const Value> Items() const;
        [[nodiscard]] bool Empty() const;
    };

    /** @brief One setting given, with its arguments. */
    struct Call {
        const Setting* setting = nullptr;
        std::vector<Value> arguments;
    };

    /**
     * @brief Builds what @p description builds: makes the builder from @p constructor, gives it @p calls in
     *        order, and builds.
     * @return The resource, owned by the caller (koral_resource_release); poisoned rather than an error when
     *         the builder itself refused, as its Build would be. An error only when the calls do not fit the
     *         description: a setting not its own, or more arguments than a setting takes (fewer are fine: the
     *         rest are empty).
     */
    [[nodiscard]] KORAL_API Result<KoralResource*> Build(const Description& description, const std::vector<Value>& constructor,
                                                         const std::vector<Call>& calls);
}
