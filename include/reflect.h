//
// What a type is made of, at run time: for an inspector, saving and loading, undo, carrying state
// across a hot reload, and bindings to other languages.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <kmath/matrix.h>
#include <kmath/quaternion.h>

#include "api.h"
#include "error.h"

/**
 * @file reflect.h
 * @brief Run-time descriptions of types: their fields, and how to reach them in an object.
 *
 * A type says what it is made of once, and everything that walks objects generically works with
 * it — kgui::Inspect draws an editor for it, ToJson / FromJson save and load it, a scene's state is
 * carried across a hot reload with it, and a binding to another language reads the same TypeInfo:
 *
 * @code
 * struct Light {
 *     kor::Vec3 position;
 *     kor::Vec3 color {1.f};
 *     float intensity = 1.f;
 *     std::vector<std::string> tags;
 * };
 * KORAL_REFLECT(Light, position, color, intensity, tags)
 *
 * Light light;
 * std::string saved = kor::ToJson(light);          // {"position":[0,0,0],"color":[1,1,1],...}
 * kor::FromJson(light, saved);
 * kor::Ref(light).Field("intensity").As<float>() = 2.f;
 * @endcode
 *
 * For more than a list of fields — a range, a tooltip, a private field — write the function the macro
 * would have, next to the type (it is found by argument-dependent lookup, so in the type's namespace):
 *
 * @code
 * inline void KoralReflect(kor::TypeBuilder<Light>& type) {
 *     type.Name("Light");
 *     type.Field("intensity", &Light::intensity).Range(0.f, 100.f).Tooltip("In candela");
 *     type.Field("color", &Light::color).Color();
 * }
 * @endcode
 *
 * Built in: bool, the integer and floating-point types, std::string, kor's vectors (float, int, uint),
 * kor::Quat and kor::Mat4, std::vector of any of these or of a reflected type, and enums (named with
 * KORAL_REFLECT_ENUM, or as plain integers without).
 */
namespace kor
{
    /** @brief What kind of value a type is. */
    enum class TypeKind : std::uint8_t {
        eBool,
        eInt8, eInt16, eInt32, eInt64,
        eUInt8, eUInt16, eUInt32, eUInt64,
        eFloat, eDouble,
        eString,
        eVec2, eVec3, eVec4,
        eIVec2, eIVec3, eIVec4,
        eUVec2, eUVec3, eUVec4,
        eQuat,
        eMat4,
        eEnum,     ///< Named values over an integer. @see TypeInfo::enumerators
        eStruct,   ///< Named fields. @see TypeInfo::fields
        eArray,    ///< A std::vector. @see TypeInfo::element
    };

    struct TypeInfo;

    /** @brief One field of a struct: its name, its type, where it is, and how to show it. */
    struct FieldInfo {
        std::string name;
        const TypeInfo* type = nullptr;
        /** The field inside an object of the struct. */
        std::function<void*(void*)> address;

        std::optional<double> min;     ///< For numbers: the smallest value an editor offers.
        std::optional<double> max;     ///< For numbers: the largest.
        std::string tooltip;
        bool color = false;            ///< A vec3/vec4 that is a colour: an editor shows a picker.
        bool readOnly = false;         ///< Shown, not edited.
        bool transient = false;        ///< Not saved: derived, or only meaningful while running.
    };

    /** @brief A type, as run-time code sees it. The same object for the life of the process. */
    struct KORAL_API TypeInfo {
        std::string name;
        TypeKind kind = TypeKind::eStruct;
        std::size_t size = 0;

        std::vector<FieldInfo> fields;   ///< eStruct.

        /** eEnum: every named value. */
        std::vector<std::pair<std::string, std::int64_t>> enumerators;
        std::function<std::int64_t(const void*)> enumRead;
        std::function<void(void*, std::int64_t)> enumWrite;

        /** eArray: what it holds, and how to reach it. */
        const TypeInfo* element = nullptr;
        std::function<std::size_t(const void*)> arraySize;
        std::function<void(void*, std::size_t)> arrayResize;
        std::function<void*(void*, std::size_t)> arrayAt;

        /** A default-made object of the type, owned by the caller; null for one that cannot be. */
        std::function<std::shared_ptr<void>()> make;

        /** @brief The field named @p name, or null. */
        [[nodiscard]] const FieldInfo* Field(std::string_view name) const;
        /** @brief The enumerator named @p name. */
        [[nodiscard]] std::optional<std::int64_t> EnumValue(std::string_view name) const;
        /** @brief The name of @p value, or empty. */
        [[nodiscard]] std::string_view EnumName(std::int64_t value) const;
    };

    /**
     * @brief Every reflected type the process has described so far, by name — for code that meets a
     *        type only as a name: a file, another language.
     */
    [[nodiscard]] KORAL_API const TypeInfo* FindType(std::string_view name);

    namespace detail {
        KORAL_API void RegisterType(const TypeInfo* type);
    }

    template<typename T> struct TypeBuilder;
    template<typename T> const TypeInfo& TypeOf();

    /** @brief Refines a field as it is added: `.Range(0, 1).Tooltip("...")`. */
    class FieldBuilder {
    public:
        explicit FieldBuilder(FieldInfo& field) : _field(field) {}
        FieldBuilder& Range(const double min, const double max) { _field.min = min; _field.max = max; return *this; }
        FieldBuilder& Tooltip(std::string text) { _field.tooltip = std::move(text); return *this; }
        FieldBuilder& Color() { _field.color = true; return *this; }
        FieldBuilder& ReadOnly() { _field.readOnly = true; return *this; }
        FieldBuilder& Transient() { _field.transient = true; return *this; }
    private:
        FieldInfo& _field;
    };

    /** @brief What a type's KoralReflect function fills in. */
    template<typename T>
    struct TypeBuilder {
        TypeInfo& info;

        TypeBuilder& Name(std::string name) { info.name = std::move(name); return *this; }

        /** @brief A field, by pointer to member. Its type must be reflectable too. */
        template<typename M, typename U = T> requires std::is_class_v<U>
        FieldBuilder Field(std::string name, M U::* member) {
            FieldInfo field;
            field.name = std::move(name);
            field.type = &TypeOf<std::remove_cv_t<M>>();
            field.address = [member](void* object) -> void* { return &(static_cast<U*>(object)->*member); };
            info.fields.push_back(std::move(field));
            return FieldBuilder(info.fields.back());
        }

        /** @brief For an enum: one named value. */
        TypeBuilder& Value(std::string name, const T value) requires std::is_enum_v<T> {
            info.enumerators.emplace_back(std::move(name), static_cast<std::int64_t>(value));
            return *this;
        }
    };

    namespace detail {
        template<typename T> struct IsVector : std::false_type {};
        template<typename T, typename A> struct IsVector<std::vector<T, A>> : std::true_type { using Element = T; };

        template<typename T>
        concept Described = requires(TypeBuilder<T>& builder) { KoralReflect(builder); };

        template<typename T>
        constexpr std::optional<TypeKind> BuiltinKind() {
            if constexpr (std::is_same_v<T, bool>) return TypeKind::eBool;
            else if constexpr (std::is_same_v<T, std::int8_t>) return TypeKind::eInt8;
            else if constexpr (std::is_same_v<T, std::int16_t>) return TypeKind::eInt16;
            else if constexpr (std::is_same_v<T, std::int32_t>) return TypeKind::eInt32;
            else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, long long>) return TypeKind::eInt64;
            else if constexpr (std::is_same_v<T, std::uint8_t>) return TypeKind::eUInt8;
            else if constexpr (std::is_same_v<T, std::uint16_t>) return TypeKind::eUInt16;
            else if constexpr (std::is_same_v<T, std::uint32_t>) return TypeKind::eUInt32;
            else if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, unsigned long long>) return TypeKind::eUInt64;
            else if constexpr (std::is_same_v<T, float>) return TypeKind::eFloat;
            else if constexpr (std::is_same_v<T, double>) return TypeKind::eDouble;
            else if constexpr (std::is_same_v<T, std::string>) return TypeKind::eString;
            else if constexpr (std::is_same_v<T, kor::Vec2>) return TypeKind::eVec2;
            else if constexpr (std::is_same_v<T, kor::Vec3>) return TypeKind::eVec3;
            else if constexpr (std::is_same_v<T, kor::Vec4>) return TypeKind::eVec4;
            else if constexpr (std::is_same_v<T, kor::IVec2>) return TypeKind::eIVec2;
            else if constexpr (std::is_same_v<T, kor::IVec3>) return TypeKind::eIVec3;
            else if constexpr (std::is_same_v<T, kor::IVec4>) return TypeKind::eIVec4;
            else if constexpr (std::is_same_v<T, kor::UVec2>) return TypeKind::eUVec2;
            else if constexpr (std::is_same_v<T, kor::UVec3>) return TypeKind::eUVec3;
            else if constexpr (std::is_same_v<T, kor::UVec4>) return TypeKind::eUVec4;
            else if constexpr (std::is_same_v<T, kor::Quat>) return TypeKind::eQuat;
            else if constexpr (std::is_same_v<T, kor::Mat4>) return TypeKind::eMat4;
            else return std::nullopt;
        }

        KORAL_API std::string_view KindName(TypeKind kind);
    }

    /**
     * @brief The description of @p T: built the first time it is asked for, the same object after.
     *
     * A struct or enum needs a KoralReflect function (KORAL_REFLECT, KORAL_REFLECT_ENUM); a builtin
     * or a std::vector of reflectable elements needs nothing.
     */
    template<typename T>
    const TypeInfo& TypeOf() {
        static const TypeInfo* info = [] {
            auto* made = new TypeInfo();   // never freed: types are described once per process
            made->size = sizeof(T);
            if constexpr (std::is_default_constructible_v<T>)
                made->make = [] { return std::static_pointer_cast<void>(std::make_shared<T>()); };
            if constexpr (constexpr auto kind = detail::BuiltinKind<T>(); kind.has_value()) {
                made->kind = *kind;
                made->name = std::string(detail::KindName(*kind));
            } else if constexpr (detail::IsVector<T>::value) {
                using E = typename detail::IsVector<T>::Element;
                made->kind = TypeKind::eArray;
                made->element = &TypeOf<E>();
                made->name = "array<" + made->element->name + ">";
                made->arraySize = [](const void* v) { return static_cast<const T*>(v)->size(); };
                made->arrayResize = [](void* v, const std::size_t n) { static_cast<T*>(v)->resize(n); };
                made->arrayAt = [](void* v, const std::size_t i) -> void* { return &(*static_cast<T*>(v))[i]; };
            } else if constexpr (std::is_enum_v<T>) {
                made->kind = TypeKind::eEnum;
                made->enumRead = [](const void* v) { return static_cast<std::int64_t>(*static_cast<const T*>(v)); };
                made->enumWrite = [](void* v, const std::int64_t value) { *static_cast<T*>(v) = static_cast<T>(value); };
                if constexpr (detail::Described<T>) {
                    TypeBuilder<T> builder{*made};
                    KoralReflect(builder);
                }
                if (made->name.empty()) made->name = "enum";
            } else {
                static_assert(detail::Described<T>,
                    "kor::TypeOf<T>: T is not reflected. Add KORAL_REFLECT(T, fields...) next to it, or a "
                    "KoralReflect(kor::TypeBuilder<T>&) function in its namespace.");
                made->kind = TypeKind::eStruct;
                TypeBuilder<T> builder{*made};
                KoralReflect(builder);
            }
            detail::RegisterType(made);
            return made;
        }();
        return *info;
    }

    class ConstRef;

    /**
     * @brief An object of any reflected type, by address: what code that does not know the type
     *        works with. Does not own the object.
     */
    class KORAL_API Ref {
    public:
        Ref() = default;
        Ref(void* object, const TypeInfo& type) : _object(object), _type(&type) {}
        template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, Ref> && !std::is_same_v<std::remove_cvref_t<T>, ConstRef>)
        Ref(T& object) : _object(&object), _type(&TypeOf<std::remove_cv_t<T>>()) {}   // NOLINT(*-explicit-constructor)

        [[nodiscard]] bool Valid() const { return _object && _type; }
        [[nodiscard]] const TypeInfo& Type() const { return *_type; }
        [[nodiscard]] void* Address() const { return _object; }

        /** @brief The object as a @p T. Throws std::bad_cast if it is not one. */
        template<typename T> [[nodiscard]] T& As() const {
            if (!_type || (_type != &TypeOf<T>() && _type->name != TypeOf<T>().name)) throw std::bad_cast();
            return *static_cast<T*>(_object);
        }

        /** @brief A struct's field by name; an invalid Ref when there is none. */
        [[nodiscard]] Ref Field(std::string_view name) const;
        /** @brief An array's element. */
        [[nodiscard]] Ref At(std::size_t index) const;
        /** @brief An array's length. */
        [[nodiscard]] std::size_t Size() const;
        void Resize(std::size_t size) const;

    private:
        void* _object = nullptr;
        const TypeInfo* _type = nullptr;
    };

    /** @brief A Ref that cannot change what it refers to. */
    class KORAL_API ConstRef {
    public:
        ConstRef() = default;
        ConstRef(const void* object, const TypeInfo& type) : _ref(const_cast<void*>(object), type) {}
        ConstRef(const Ref& ref) : _ref(ref) {}   // NOLINT(*-explicit-constructor)
        template<typename T> requires (!std::is_same_v<std::remove_cvref_t<T>, Ref> && !std::is_same_v<std::remove_cvref_t<T>, ConstRef>)
        ConstRef(const T& object) : _ref(const_cast<T&>(object)) {}   // NOLINT(*-explicit-constructor)

        [[nodiscard]] bool Valid() const { return _ref.Valid(); }
        [[nodiscard]] const TypeInfo& Type() const { return _ref.Type(); }
        [[nodiscard]] const void* Address() const { return _ref.Address(); }
        template<typename T> [[nodiscard]] const T& As() const { return _ref.As<T>(); }
        [[nodiscard]] ConstRef Field(std::string_view name) const { return _ref.Field(name); }
        [[nodiscard]] ConstRef At(std::size_t index) const { return _ref.At(index); }
        [[nodiscard]] std::size_t Size() const { return _ref.Size(); }

    private:
        Ref _ref;
    };

    // ---- saving and loading ----------------------------------------------------------------------

    /**
     * @brief The object as JSON: a struct as an object of its fields (all but transient ones), an
     *        array as an array, a vector or quaternion as an array of its components, an enum by name.
     */
    [[nodiscard]] KORAL_API std::string ToJson(ConstRef value, bool pretty = false);

    /**
     * @brief Loads @p json into the object. What the JSON leaves out keeps its current value, and
     *        what it names that the type does not have is ignored — so a file from before a field
     *        was added, or after one was removed, still loads.
     * @return eInvalidArgument naming the path of the first value of the wrong kind (`lights[2].color`).
     */
    KORAL_API VoidResult FromJson(Ref value, std::string_view json);

    /** @brief A copy of every field of @p from into @p to, which must be the same type. Through JSON. */
    KORAL_API VoidResult CopyFields(ConstRef from, Ref to);
}

// ---- the registration macros ----------------------------------------------------------------------

#define KORAL_REFLECT_EXPAND(x) x
#define KORAL_REFLECT_FIELD_(Type, field) builder.Field(#field, &Type::field);
#define KORAL_REFLECT_VALUE_(Type, value) builder.Value(#value, Type::value);

#define KORAL_REFLECT_FE_1(M, T, a) M(T, a)
#define KORAL_REFLECT_FE_2(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_1(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_3(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_2(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_4(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_3(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_5(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_4(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_6(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_5(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_7(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_6(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_8(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_7(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_9(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_8(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_10(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_9(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_11(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_10(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_12(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_11(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_13(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_12(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_14(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_13(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_15(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_14(M, T, __VA_ARGS__))
#define KORAL_REFLECT_FE_16(M, T, a, ...) M(T, a) KORAL_REFLECT_EXPAND(KORAL_REFLECT_FE_15(M, T, __VA_ARGS__))
#define KORAL_REFLECT_PICK(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define KORAL_REFLECT_FOR_EACH(M, T, ...) KORAL_REFLECT_EXPAND(KORAL_REFLECT_PICK(__VA_ARGS__, \
    KORAL_REFLECT_FE_16, KORAL_REFLECT_FE_15, KORAL_REFLECT_FE_14, KORAL_REFLECT_FE_13, KORAL_REFLECT_FE_12, \
    KORAL_REFLECT_FE_11, KORAL_REFLECT_FE_10, KORAL_REFLECT_FE_9, KORAL_REFLECT_FE_8, KORAL_REFLECT_FE_7, \
    KORAL_REFLECT_FE_6, KORAL_REFLECT_FE_5, KORAL_REFLECT_FE_4, KORAL_REFLECT_FE_3, KORAL_REFLECT_FE_2, \
    KORAL_REFLECT_FE_1)(M, T, __VA_ARGS__))

/**
 * @brief Reflects a struct's fields (up to 16), by name. Next to the type, in its namespace.
 * For private fields, add `KORAL_REFLECT_FRIEND(Type)` inside the class as well.
 */
#define KORAL_REFLECT(Type, ...)                                                  \
    [[maybe_unused]] inline void KoralReflect(::kor::TypeBuilder<Type>& builder) { \
        builder.Name(#Type);                                                      \
        KORAL_REFLECT_FOR_EACH(KORAL_REFLECT_FIELD_, Type, __VA_ARGS__)           \
    }

/** @brief Reflects an enum's values (up to 16), by name. Next to the enum, in its namespace. */
#define KORAL_REFLECT_ENUM(Type, ...)                                             \
    [[maybe_unused]] inline void KoralReflect(::kor::TypeBuilder<Type>& builder) { \
        builder.Name(#Type);                                                      \
        KORAL_REFLECT_FOR_EACH(KORAL_REFLECT_VALUE_, Type, __VA_ARGS__)           \
    }

/** @brief Inside a class with private fields that KORAL_REFLECT lists. */
#define KORAL_REFLECT_FRIEND(Type) friend void KoralReflect(::kor::TypeBuilder<Type>& builder)
