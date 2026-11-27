#include "TextureBinding.h"

#include <memory>
#include <string>

namespace {
using namespace ii::pbr::exchange;

// This host-provided implementation demonstrates replacement at the interface.
class AlternateConverter final : public TextureConverter {
public:
    MaterialExchangeResult execute(const TextureConversionInput& input) const override
    {
        MaterialExchangeResult result;
        result.original = input.original;
        result.texture = input.metadata;
        result.converted = "alternate:" + input.original;
        return result;
    }
};
}

int main()
{
    const TextureConversionInput input{"source", {}};
    std::unique_ptr<const TextureConverter> converter = std::make_unique<AlternateConverter>();
    auto alternate = converter->execute(input);
    if (alternate.original != input.original || alternate.converted != "alternate:source") return 1;
    converter = std::make_unique<MaterialXTextureConverter>(std::vector<MaterialLibrary>{});
    const auto native = converter->execute(input);
    if (native.original != input.original || native.converted || native.diagnostics.empty()) return 2;
    if (native.diagnostics.front().code != "invalid_texture_metadata") return 3;
    return 0;
}
