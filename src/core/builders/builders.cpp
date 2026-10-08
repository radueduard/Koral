// Values for builder descriptions, and building from a description: what builderDescriptions.h declares
// besides the generated table (builders.g.cpp).

#include <builderDescriptions.h>

#include <algorithm>
#include <format>

#include "builderValues.h"

namespace kor::builders
{
    std::uint64_t Value::UInt() const
    {
        if (const auto* v = std::get_if<std::uint64_t>(&data)) return *v;
        if (const auto* v = std::get_if<std::int64_t>(&data)) return static_cast<std::uint64_t>(*v);
        if (const auto* v = std::get_if<bool>(&data)) return *v ? 1u : 0u;
        if (const auto* v = std::get_if<double>(&data)) return static_cast<std::uint64_t>(*v);
        return 0;
    }

    std::int64_t Value::Int() const
    {
        if (const auto* v = std::get_if<std::int64_t>(&data)) return *v;
        if (const auto* v = std::get_if<std::uint64_t>(&data)) return static_cast<std::int64_t>(*v);
        if (const auto* v = std::get_if<bool>(&data)) return *v ? 1 : 0;
        if (const auto* v = std::get_if<double>(&data)) return static_cast<std::int64_t>(*v);
        return 0;
    }

    double Value::Float() const
    {
        if (const auto* v = std::get_if<double>(&data)) return *v;
        if (const auto* v = std::get_if<std::int64_t>(&data)) return static_cast<double>(*v);
        if (const auto* v = std::get_if<std::uint64_t>(&data)) return static_cast<double>(*v);
        return 0.0;
    }

    bool Value::Bool() const
    {
        if (const auto* v = std::get_if<bool>(&data)) return *v;
        return UInt() != 0;
    }

    const char* Value::CString() const
    {
        const auto* v = std::get_if<std::string>(&data);
        return v ? v->c_str() : nullptr;
    }

    KoralResource* Value::Resource() const
    {
        const auto* v = std::get_if<KoralResource*>(&data);
        return v ? *v : nullptr;
    }

    std::span<const std::byte> Value::Bytes() const
    {
        if (const auto* v = std::get_if<std::vector<std::byte>>(&data)) return *v;
        return {};
    }

    std::span<const Value> Value::Items() const
    {
        if (const auto* v = std::get_if<std::vector<Value>>(&data)) return *v;
        return {};
    }

    bool Value::Empty() const { return std::holds_alternative<std::monostate>(data); }

    const Setting* Description::FindSetting(const std::string_view settingName) const
    {
        const auto it = std::ranges::find(settings, settingName, &Setting::name);
        return it == settings.end() ? nullptr : &*it;
    }

    const Description* Find(const std::string_view name)
    {
        const auto all = All();
        const auto it = std::ranges::find(all, name, &Description::name);
        return it == all.end() ? nullptr : &*it;
    }

    Result<KoralResource*> Build(const Description& description, const std::vector<Value>& constructor, const std::vector<Call>& calls)
    {
        // Checked before the builder is made, so a mistake here leaves nothing to clean up.
        if (constructor.size() > description.constructor.size())
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                "A {} builder is made from {} values, and was given {}.", description.name, description.constructor.size(), constructor.size()) });
        for (const auto& call : calls) {
            if (!call.setting || std::ranges::none_of(description.settings, [&](const Setting& s) { return &s == call.setting; }))
                return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                    "A setting given to a {} builder is none of its own.", description.name) });
            if (call.arguments.size() > call.setting->arguments.size())
                return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                    "{}'s {} takes {} values, and was given {}.", description.name, call.setting->name,
                    call.setting->arguments.size(), call.arguments.size()) });
        }

        KoralBuilder* builder = description.create(constructor);
        if (!builder)
            return std::unexpected(Error{ .code = ErrorCode::eInvalidArgument, .message = std::format(
                "A {} builder could not be made: {}", description.name, koral_last_error() ? koral_last_error() : "") });
        for (const auto& call : calls) call.setting->invoke(builder, call.arguments);
        KoralResource* built = description.finish(builder);
        koral_builder_destroy(builder);
        if (!built)
            return std::unexpected(Error{ .code = ErrorCode::eBackend, .message = std::format(
                "Building a {} gave nothing: {}", description.name, koral_last_error() ? koral_last_error() : "") });
        return built;
    }
}
