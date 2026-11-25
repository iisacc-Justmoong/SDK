#pragma once

#include <iiLocalizer>
#include <LVUI/NativeAppStrings.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Application-owned view model: it composes two independent SDK interfaces.
class LabelAdapter final {
public:
    // Preserve the original one-key example when no usage registry is supplied.
    LabelAdapter(std::shared_ptr<lvui::native::AppStrings> strings, std::string source)
        : strings_(std::move(strings)), source_(std::move(source)), uses_{{source_,source_,source_}}, legacy_(true) {}

    // Bind an original message to application-owned locations and distinct string keys.
    LabelAdapter(std::shared_ptr<lvui::native::AppStrings> strings, std::string source,
                 std::vector<ii::localizer::LabelUsage> uses)
        : strings_(std::move(strings)), source_(std::move(source)), uses_(std::move(uses)) {}

    // Prepare selected updates and publish one catalog; PO context is independent.
    // An LVUI observer may throw after its catalog change has already committed.
    [[nodiscard]] ii::core::Result<void> publish(std::string_view locale,
            const ii::localizer::Catalog& catalog, std::span<const std::string> arguments,
            std::optional<std::string_view> location = std::nullopt,
            std::optional<std::string_view> po_context = std::nullopt) const {
        if (!strings_) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"A label requires an app string store");
        try {
            if (legacy_ && location)
                return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Exact targeting requires registered label locations");
            const auto selected=ii::localizer::select_label_usages(uses_,source_,location);
            if (!selected) return ii::core::fail(ii::core::ErrorCode::invalid_argument,selected.error().message);
            if (selected->empty()) return ii::core::fail(ii::core::ErrorCode::not_found,"No matching label usage");
            const auto normalized=lvui::native::AppStrings::normalize_locale(locale);
            if (!normalized) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid LVUI locale");
            if (!catalog.language().empty()) {
                const auto actual=lvui::native::AppStrings::normalize_locale(catalog.language());
                if (!actual || *actual!=*normalized)
                    return ii::core::fail(ii::core::ErrorCode::invalid_argument,"PO Language does not match its target locale");
            }
            const auto message=catalog.translate(source_,arguments,po_context);
            if (!message) return ii::core::fail(ii::core::ErrorCode::invalid_argument,message.error().message);
            auto previous=strings_->catalog(locale);
            if (!previous && previous.error().code!=ii::core::ErrorCode::not_found)
                return std::unexpected(previous.error());
            auto values=previous?std::move(*previous):lvui::native::AppStrings::Entries{};
            for (const auto index:*selected) values.insert_or_assign(uses_[index].key,message->text);
            return strings_->replace_catalog(locale,std::move(values));
        } catch (...) {
            return ii::core::fail(ii::core::ErrorCode::failed,"Label translation publication failed",std::current_exception());
        }
    }
private:
    std::shared_ptr<lvui::native::AppStrings> strings_;
    std::string source_;
    std::vector<ii::localizer::LabelUsage> uses_;
    bool legacy_=false;
};
