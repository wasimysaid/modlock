#pragma once

#include <expected>
#include <string>

#include "modlock/gameinterop/entity_abi.h"
#include "src/wasm/manifest.pb.h"

namespace modlock::wasm {

class DefinitionUndo;

struct AbilityUpgradeLayout {
  size_t ability_size;
  size_t tier_size;
  size_t property_size;
  gameinterop::SchemaField tiers;
  gameinterop::SchemaField properties;
  gameinterop::SchemaField name;
  gameinterop::SchemaField street_bonus;
  gameinterop::SchemaField upgrade_type;
};

std::expected<void, std::string> ValidateAbilityUpgrades(const Definition& change);

std::expected<AbilityUpgradeLayout, std::string> ResolveAbilityUpgradeLayout(void* schema,
                                                                             const char* module);

std::expected<void, std::string> ApplyAbilityUpgrades(
    unsigned char* definition, const Definition& change, const AbilityUpgradeLayout& layout,
    DefinitionUndo& undo, const gameinterop::BoundedReader& read = gameinterop::ReadNative,
    const gameinterop::BoundedWriter& write = gameinterop::WriteNative);

}  // namespace modlock::wasm
