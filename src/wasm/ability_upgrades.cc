//go:build deps_only

#include "wasm/ability_upgrades.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

#include "gameinterop/utl_vector.h"
#include "wasm/definition_changes.h"

namespace modlock::wasm {
namespace {

using gameinterop::SchemaField;
using gameinterop::SchemaStorage;
using NativeVector = gameinterop::UtlVector<unsigned char>;
using Bonuses = std::array<float, 2>;

bool Fits(const SchemaField& field, size_t owner_size, size_t size) {
  return field.size >= size && field.offset <= owner_size && size <= owner_size - field.offset;
}

bool VectorField(const SchemaField& field, size_t owner_size, std::string_view element) {
  if (!field.list || field.storage != SchemaStorage::kOpaque || !field.type ||
      !Fits(field, owner_size, sizeof(NativeVector))) {
    return false;
  }
  std::string type;
  for (const char letter : std::string_view(field.type)) {
    if (letter != ' ') {
      type += letter;
    }
  }
  return type == "CUtlVector<" + std::string(element) + ">";
}

bool StringField(const SchemaField& field, size_t owner_size) {
  return !field.list && field.storage == SchemaStorage::kText && field.type &&
         std::string_view(field.type) == "CUtlString" && Fits(field, owner_size, sizeof(char*));
}

bool ValidLayout(const AbilityUpgradeLayout& layout) {
  // The engine stores two parsed floats between the Street Brawl string and upgrade type.
  return VectorField(layout.tiers, layout.ability_size, "AbilityUpgrade_t") &&
         VectorField(layout.properties, layout.tier_size, "PropertyUpgrade_t") &&
         StringField(layout.name, layout.property_size) &&
         StringField(layout.street_bonus, layout.property_size) &&
         layout.name.offset + sizeof(char*) <= layout.street_bonus.offset &&
         !layout.upgrade_type.list && layout.upgrade_type.storage == SchemaStorage::kNumber &&
         layout.upgrade_type.type &&
         std::string_view(layout.upgrade_type.type) == "EAbilityUpgradeType" &&
         Fits(layout.upgrade_type, layout.property_size, sizeof(int32_t)) &&
         layout.upgrade_type.offset >= sizeof(Bonuses) + sizeof(char*) &&
         layout.street_bonus.offset == layout.upgrade_type.offset - sizeof(Bonuses) - sizeof(char*);
}

unsigned char* Address(const void* base, size_t offset, size_t size) {
  const auto address = reinterpret_cast<std::uintptr_t>(base);
  const auto maximum = std::numeric_limits<std::uintptr_t>::max();
  if (!address || offset > maximum - address || size > maximum - address - offset) {
    return nullptr;
  }
  return reinterpret_cast<unsigned char*>(address + offset);
}

template <typename T>
bool ReadAt(const void* base, size_t offset, T& value, const gameinterop::BoundedReader& read) {
  const auto* address = Address(base, offset, sizeof(value));
  return address && read(address, &value, sizeof(value));
}

std::expected<NativeVector, std::string> ReadVector(const void* owner, size_t offset, int32_t limit,
                                                    size_t stride,
                                                    const gameinterop::BoundedReader& read) {
  NativeVector vector{};
  if (!ReadAt(owner, offset, vector, read) || vector.count < 0 || vector.count > limit ||
      static_cast<uint32_t>(vector.count) > vector.capacity ||
      (vector.count &&
       (!stride ||
        static_cast<size_t>(vector.count) > std::numeric_limits<size_t>::max() / stride ||
        !Address(vector.data, 0, static_cast<size_t>(vector.count) * stride)))) {
    return std::unexpected("invalid or unreadable upgrade vector");
  }
  return vector;
}

std::expected<std::string, std::string> ReadName(const void* row, size_t offset,
                                                 const gameinterop::BoundedReader& read) {
  const char* pointer = nullptr;
  if (!ReadAt(row, offset, pointer, read) || !pointer) {
    return std::unexpected("unreadable upgrade property name");
  }
  std::string name;
  for (size_t index = 0; index < 256; ++index) {
    char letter = 0;
    if (!ReadAt(pointer, index, letter, read)) {
      return std::unexpected("unreadable upgrade property name");
    }
    if (!letter) {
      return name;
    }
    name += letter;
  }
  return std::unexpected("unterminated upgrade property name");
}

struct BonusWrite {
  unsigned char* storage;
  Bonuses before;
  Bonuses after;
};

}  // namespace

std::expected<void, std::string> ValidateAbilityUpgrades(const Definition& change) {
  if (change.upgrades().empty()) {
    return {};
  }
  if (change.file() != Definition::FILE_ABILITIES) {
    return std::unexpected("upgrades require FILE_ABILITIES");
  }
  if (!change.properties().empty() || !change.fields().empty() || !change.copy_fields().empty()) {
    return std::unexpected(
        "upgrades require a separate definition entry from properties, fields and copies");
  }
  std::set<uint32_t> tiers;
  for (const auto& upgrade : change.upgrades()) {
    if (upgrade.tier() < 1 || upgrade.tier() > 3 || !tiers.insert(upgrade.tier()).second) {
      return std::unexpected("upgrade tiers must be distinct values from 1 to 3");
    }
    if (upgrade.properties().empty()) {
      return std::unexpected("upgrade tier must request at least one property");
    }
    for (const auto& [name, value] : upgrade.properties()) {
      if (name.empty() || name.size() > 255 || name.find('\0') != name.npos ||
          !std::isfinite(value)) {
        return std::unexpected("upgrade properties need bounded names and finite values");
      }
    }
  }
  return {};
}

std::expected<AbilityUpgradeLayout, std::string> ResolveAbilityUpgradeLayout(void* schema,
                                                                             const char* module) {
  auto ability_size = gameinterop::SchemaClassSizeOf(schema, module, "CitadelAbilityVData");
  auto tier_size = gameinterop::SchemaClassSizeOf(schema, module, "AbilityUpgrade_t");
  auto property_size = gameinterop::SchemaClassSizeOf(schema, module, "PropertyUpgrade_t");
  auto tiers =
      gameinterop::SchemaFieldOf(schema, module, "CitadelAbilityVData", "m_vecAbilityUpgrades");
  auto properties =
      gameinterop::SchemaFieldOf(schema, module, "AbilityUpgrade_t", "m_vecPropertyUpgrades");
  auto name = gameinterop::SchemaFieldOf(schema, module, "PropertyUpgrade_t", "m_strPropertyName");
  auto street =
      gameinterop::SchemaFieldOf(schema, module, "PropertyUpgrade_t", "m_strStreetBrawlBonus");
  auto type = gameinterop::SchemaFieldOf(schema, module, "PropertyUpgrade_t", "m_eUpgradeType");
  if (!ability_size || !tier_size || !property_size || !tiers || !properties || !name || !street ||
      !type) {
    return std::unexpected("ability upgrade schema is unavailable");
  }
  AbilityUpgradeLayout layout{*ability_size, *tier_size, *property_size, *tiers,
                              *properties,   *name,      *street,        *type};
  if (!ValidLayout(layout)) {
    return std::unexpected("ability upgrade schema layout is unsupported");
  }
  return layout;
}

std::expected<void, std::string> ApplyAbilityUpgrades(unsigned char* definition,
                                                      const Definition& change,
                                                      const AbilityUpgradeLayout& layout,
                                                      DefinitionUndo& undo,
                                                      const gameinterop::BoundedReader& read,
                                                      const gameinterop::BoundedWriter& write) {
  if (auto valid = ValidateAbilityUpgrades(change); !valid) {
    return valid;
  }
  if (change.upgrades().empty()) {
    return {};
  }
  if (!ValidLayout(layout)) {
    return std::unexpected("ability upgrade schema layout is unsupported");
  }
  auto tiers = ReadVector(definition, layout.tiers.offset, 3, layout.tier_size, read);
  if (!tiers) {
    return std::unexpected(tiers.error());
  }
  std::vector<BonusWrite> writes;
  for (const auto& request : change.upgrades()) {
    if (request.tier() > static_cast<uint32_t>(tiers->count)) {
      return std::unexpected("upgrade tier " + std::to_string(request.tier()) + " is unavailable");
    }
    auto* tier = Address(tiers->data, (request.tier() - 1) * layout.tier_size, layout.tier_size);
    auto properties = ReadVector(tier, layout.properties.offset, 128, layout.property_size, read);
    if (!properties) {
      return std::unexpected(properties.error());
    }
    std::set<std::string> found;
    for (int32_t index = 0; index < properties->count; ++index) {
      auto* row = Address(properties->data, index * layout.property_size, layout.property_size);
      auto name = ReadName(row, layout.name.offset, read);
      if (!name) {
        return std::unexpected(name.error());
      }
      auto requested = request.properties().find(*name);
      if (requested == request.properties().end()) {
        continue;
      }
      auto* storage = Address(row, layout.street_bonus.offset + sizeof(char*), sizeof(Bonuses));
      const auto start = reinterpret_cast<std::uintptr_t>(storage);
      const bool overlap =
          storage && std::ranges::any_of(writes, [&](const BonusWrite& previous) {
            const auto other = reinterpret_cast<std::uintptr_t>(previous.storage);
            return start < other + sizeof(Bonuses) && other < start + sizeof(Bonuses);
          });
      if (!found.insert(*name).second || overlap) {
        return std::unexpected("ambiguous upgrade property " + *name);
      }
      Bonuses before{};
      if (!storage || !read(storage, before.data(), sizeof(before)) || !std::isfinite(before[0]) ||
          !std::isfinite(before[1])) {
        return std::unexpected("unreadable or non-finite upgrade bonus " + *name);
      }
      writes.push_back({storage, before, {requested->second, requested->second}});
    }
    if (found.size() != request.properties().size()) {
      for (const auto& [name, value] : request.properties()) {
        if (!found.contains(name)) {
          return std::unexpected("no property " + name + " in upgrade tier " +
                                 std::to_string(request.tier()));
        }
      }
    }
  }
  for (const auto& target : writes) {
    undo.SaveValue(
        target.storage,
        {reinterpret_cast<const unsigned char*>(target.before.data()), sizeof(target.before)},
        false);
  }
  for (size_t index = 0; index < writes.size(); ++index) {
    const auto& target = writes[index];
    Bonuses actual{};
    if (write(target.storage, target.after.data(), sizeof(target.after)) &&
        read(target.storage, actual.data(), sizeof(actual)) && actual == target.after) {
      continue;
    }
    bool restored = true;
    for (size_t rollback = index + 1; rollback > 0; --rollback) {
      const auto& original = writes[rollback - 1];
      const bool written = write(original.storage, original.before.data(), sizeof(original.before));
      const bool matches =
          read(original.storage, actual.data(), sizeof(actual)) && actual == original.before;
      restored = written && matches && restored;
    }
    return std::unexpected(restored ? "upgrade write or readback failed; original bonuses restored"
                                    : "upgrade write or readback failed; rollback failed");
  }
  return {};
}

}  // namespace modlock::wasm
