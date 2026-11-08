#include "wasm/ability_upgrades.h"

#include <array>
#include <cstring>
#include <limits>

#include "gameinterop/utl_vector.h"
#include "gtest/gtest.h"
#include "wasm/definition_changes.h"

namespace {

using modlock::gameinterop::SchemaField;
using modlock::gameinterop::SchemaStorage;
using modlock::wasm::AbilityUpgradeLayout;
using modlock::wasm::ApplyAbilityUpgrades;
using modlock::wasm::Definition;
using modlock::wasm::DefinitionUndo;
using NativeVector = modlock::gameinterop::UtlVector<unsigned char>;
using Bonuses = std::array<float, 2>;

struct Property {
  const char* name;
  const char* bonus;
  const char* street_bonus;
  Bonuses values;
  int32_t upgrade_type;
  std::array<unsigned char, 28> rest;
};

struct Tier {
  NativeVector properties;
  std::array<unsigned char, 16> rest;
};

struct Ability {
  std::array<unsigned char, 64> rest;
  NativeVector tiers;
};

constexpr char kNames[][32] = {"BulletAmp", "BulletAmpDuration", "AbilityCastRange",
                               "AbilityCooldown"};

AbilityUpgradeLayout Layout() {
  return {sizeof(Ability),
          sizeof(Tier),
          sizeof(Property),
          {.offset = offsetof(Ability, tiers),
           .size = sizeof(NativeVector),
           .networked = false,
           .storage = SchemaStorage::kOpaque,
           .list = true,
           .type = "CUtlVector< AbilityUpgrade_t >"},
          {.offset = offsetof(Tier, properties),
           .size = sizeof(NativeVector),
           .networked = false,
           .storage = SchemaStorage::kOpaque,
           .list = true,
           .type = "CUtlVector< PropertyUpgrade_t >"},
          {.offset = offsetof(Property, name),
           .size = sizeof(char*),
           .networked = false,
           .storage = SchemaStorage::kText,
           .list = false,
           .type = "CUtlString"},
          {.offset = offsetof(Property, street_bonus),
           .size = sizeof(char*),
           .networked = false,
           .storage = SchemaStorage::kText,
           .list = false,
           .type = "CUtlString"},
          {.offset = offsetof(Property, upgrade_type),
           .size = sizeof(int32_t),
           .networked = false,
           .storage = SchemaStorage::kNumber,
           .list = false,
           .type = "EAbilityUpgradeType"}};
}

class AbilityUpgrades : public testing::Test {
 protected:
  Ability ability{};
  std::array<Tier, 3> tiers{};
  std::array<std::array<Property, 2>, 3> properties{};
  std::vector<std::pair<const void*, size_t>> regions;
  Definition change;
  DefinitionUndo undo;
  int reads = 0;
  int writes = 0;

  void SetUp() override {
    ability.tiers = {3, 0, reinterpret_cast<unsigned char*>(tiers.data()), 3, 0};
    for (size_t index = 0; index < tiers.size(); ++index) {
      tiers[index].properties = {index == 0 ? 2 : 1, 0,
                                 reinterpret_cast<unsigned char*>(properties[index].data()), 2, 0};
    }
    properties[0][0] = {.name = kNames[0], .values = {20, 30}, .upgrade_type = 1};
    properties[0][1] = {.name = kNames[1], .values = {6, 7}, .upgrade_type = 1};
    properties[1][0] = {.name = kNames[2], .values = {1181, 1500}, .upgrade_type = 1};
    properties[2][0] = {.name = kNames[3], .values = {-11.5, -8}, .upgrade_type = 1};
    regions = {{&ability, sizeof(ability)},
               {tiers.data(), sizeof(tiers)},
               {properties.data(), sizeof(properties)},
               {kNames, sizeof(kNames)}};
    change.set_file(Definition::FILE_ABILITIES);
    change.set_entry("citadel_ability_hook");
    Add(2, "AbilityCastRange", 236.22);
    Add(3, "AbilityCooldown", -0.5);
  }

  void Add(uint32_t tier, const char* name, float value) {
    auto* upgrade = change.add_upgrades();
    upgrade->set_tier(tier);
    (*upgrade->mutable_properties())[name] = value;
  }

  bool Contains(const void* pointer, size_t size) const {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    for (const auto& [region, length] : regions) {
      const auto base = reinterpret_cast<std::uintptr_t>(region);
      if (address >= base && address - base <= length && size <= length - (address - base)) {
        return true;
      }
    }
    return false;
  }

  bool Read(const void* pointer, void* output, size_t size) {
    ++reads;
    if (!Contains(pointer, size)) {
      return false;
    }
    std::memcpy(output, pointer, size);
    return true;
  }

  bool Write(void* pointer, const void* input, size_t size) {
    ++writes;
    if (!Contains(pointer, size)) {
      return false;
    }
    std::memcpy(pointer, input, size);
    return true;
  }

  std::expected<void, std::string> Apply(const AbilityUpgradeLayout& layout = Layout()) {
    return ApplyAbilityUpgrades(
        reinterpret_cast<unsigned char*>(&ability), change, layout, undo,
        [&](const void* p, void* out, size_t n) { return Read(p, out, n); },
        [&](void* p, const void* in, size_t n) { return Write(p, in, n); });
  }
};

TEST_F(AbilityUpgrades, ReplacesBothVariantsWithoutChangingOtherRowsOrNativeMetadata) {
  const auto original_ability = ability;
  const auto original_tiers = tiers;
  const auto first_tier = properties[0];
  const auto result = Apply();
  ASSERT_TRUE(result) << result.error();
  EXPECT_EQ(properties[1][0].values, (Bonuses{236.22, 236.22}));
  EXPECT_EQ(properties[2][0].values, (Bonuses{-0.5, -0.5}));
  EXPECT_EQ(std::memcmp(&ability, &original_ability, sizeof(ability)), 0);
  EXPECT_EQ(std::memcmp(tiers.data(), original_tiers.data(), sizeof(tiers)), 0);
  EXPECT_EQ(std::memcmp(properties[0].data(), first_tier.data(), sizeof(first_tier)), 0);
  EXPECT_EQ(properties[1][0].name, kNames[2]);
  EXPECT_EQ(properties[1][0].upgrade_type, 1);
  EXPECT_EQ(writes, 2);
}

TEST_F(AbilityUpgrades, AcceptsZeroBonusesForMultipleExistingPropertiesInOneTier) {
  auto* first = change.add_upgrades();
  first->set_tier(1);
  (*first->mutable_properties())["BulletAmp"] = 0;
  (*first->mutable_properties())["BulletAmpDuration"] = 0;
  const auto result = Apply();
  ASSERT_TRUE(result) << result.error();
  EXPECT_EQ(properties[0][0].values, (Bonuses{0, 0}));
  EXPECT_EQ(properties[0][1].values, (Bonuses{0, 0}));
  undo.Restore();
  EXPECT_EQ(properties[0][0].values, (Bonuses{20, 30}));
  EXPECT_EQ(properties[0][1].values, (Bonuses{6, 7}));
}

TEST_F(AbilityUpgrades, RejectsMissingLaterPropertyWithoutWritingEarlierValidBonus) {
  (*change.mutable_upgrades(1)->mutable_properties())["NotAProperty"] = 1;
  auto result = Apply();
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("NotAProperty"), std::string::npos);
  EXPECT_EQ(writes, 0);
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
}

TEST_F(AbilityUpgrades, RejectsInvalidRequestsBeforeReadingNativeData) {
  const auto original = change;
  for (const auto file : {Definition::FILE_HEROES, Definition::FILE_BOT_DIFFICULTY,
                          Definition::FILE_NPC_UNITS, Definition::FILE_MODIFIERS}) {
    change.set_file(file);
    EXPECT_FALSE(Apply());
  }
  change = original;
  (*change.mutable_properties())["AbilityCooldown"] = 5;
  EXPECT_FALSE(Apply());
  change = original;
  (*change.mutable_fields())["Class.field"] = 5;
  EXPECT_FALSE(Apply());
  change = original;
  (*change.mutable_copy_fields())["Class.a"] = "Class.b";
  EXPECT_FALSE(Apply());
  change = original;
  Add(2, "AbilityCastRange", 99);
  EXPECT_FALSE(Apply());
  for (uint32_t tier : {0u, 4u}) {
    change = original;
    change.mutable_upgrades(0)->set_tier(tier);
    EXPECT_FALSE(Apply());
  }
  change = original;
  change.mutable_upgrades(0)->mutable_properties()->clear();
  EXPECT_FALSE(Apply());
  for (const auto& name : {std::string{}, std::string(256, 'a'), std::string("a\0b", 3)}) {
    change = original;
    (*change.mutable_upgrades(0)->mutable_properties())[name] = 1;
    EXPECT_FALSE(Apply());
  }
  for (float value :
       {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    change = original;
    (*change.mutable_upgrades(0)->mutable_properties())["AbilityCastRange"] = value;
    EXPECT_FALSE(Apply());
  }
  EXPECT_EQ(reads, 0);
  EXPECT_EQ(writes, 0);
}

class EmptyImage : public modlock::gameinterop::ModuleImage {
 public:
  std::uintptr_t base() const override { return 0; }

  std::span<const uint8_t> image_bytes() const override { return {}; }
};

TEST_F(AbilityUpgrades, InvalidKindsAndMixedEntriesDoNotReachDefinitionLookup) {
  google::protobuf::RepeatedPtrField<Definition> definitions;
  for (const auto file : {Definition::FILE_HEROES, Definition::FILE_MODIFIERS,
                          Definition::FILE_NPC_UNITS, Definition::FILE_BOT_DIFFICULTY}) {
    auto* entry = definitions.Add();
    *entry = change;
    entry->set_file(file);
  }
  *definitions.Add() = change;
  (*definitions.Mutable(definitions.size() - 1)->mutable_properties())["AbilityCooldown"] = 5;
  const auto result =
      modlock::wasm::ChangeDefinitions(EmptyImage{}, nullptr, "server.dll", definitions, undo);
  ASSERT_TRUE(result) << result.error();
  EXPECT_EQ(result->size(), 5u);
  for (const auto& error : *result) {
    EXPECT_TRUE(error.starts_with("citadel_ability_hook: upgrades require")) << error;
  }
}

TEST_F(AbilityUpgrades, RejectsChangedSchemaBeforeReadingNativeData) {
  auto layout = Layout();
  layout.ability_size = layout.tiers.offset + sizeof(NativeVector) - 1;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.tiers.list = false;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.properties.type = "CUtlVector< OtherType >";
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.properties.size = 8;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.name.storage = SchemaStorage::kOpaque;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.name.offset = layout.street_bonus.offset;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.street_bonus.type = "float32";
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.upgrade_type.offset += 4;
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.upgrade_type.type = "float32";
  EXPECT_FALSE(Apply(layout));
  layout = Layout();
  layout.property_size = layout.upgrade_type.offset;
  EXPECT_FALSE(Apply(layout));
  EXPECT_EQ(reads, 0);
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RejectsMalformedOrUnavailableVectors) {
  const auto original = ability.tiers;
  ability.tiers.count = -1;
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  ability.tiers.count = 4;
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  ability.tiers.capacity = 2;
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  ability.tiers.data = nullptr;
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  ability.tiers.count = 2;
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  ability.tiers.data =
      reinterpret_cast<unsigned char*>(std::numeric_limits<std::uintptr_t>::max() - 4);
  EXPECT_FALSE(Apply());
  ability.tiers = original;
  tiers[1].properties.count = 129;
  EXPECT_FALSE(Apply());
  tiers[1].properties.count = 1;
  tiers[1].properties.data = reinterpret_cast<unsigned char*>(1);
  EXPECT_FALSE(Apply());
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RejectsUnreadableAndUnterminatedNames) {
  properties[1][0].name = reinterpret_cast<const char*>(1);
  EXPECT_FALSE(Apply());
  std::array<char, 256> unterminated;
  unterminated.fill('a');
  regions.push_back({unterminated.data(), unterminated.size()});
  properties[1][0].name = unterminated.data();
  EXPECT_FALSE(Apply());
  properties[1][0].name = nullptr;
  EXPECT_FALSE(Apply());
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RejectsAmbiguousNativeRowsAndNonfiniteOriginalBonuses) {
  tiers[1].properties.count = 2;
  properties[1][1] = properties[1][0];
  EXPECT_FALSE(Apply());
  tiers[1].properties.count = 1;
  properties[1][0].values[1] = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(Apply());
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RejectsOverlappingBonusStorageAcrossTiers) {
  auto* aliased_row = reinterpret_cast<unsigned char*>(&properties[1][0]) + sizeof(float);
  tiers[2].properties.data = aliased_row;
  const auto result = ApplyAbilityUpgrades(
      reinterpret_cast<unsigned char*>(&ability), change, Layout(), undo,
      [&](const void* p, void* out, size_t n) {
        if (p == aliased_row + offsetof(Property, name) && n == sizeof(char*)) {
          const char* name = kNames[3];
          std::memcpy(out, &name, n);
          return true;
        }
        return Read(p, out, n);
      },
      [&](void* p, const void* in, size_t n) { return Write(p, in, n); });
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("ambiguous"), std::string::npos);
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RejectsUnreadableBonusStorageWithoutWriting) {
  const auto result = ApplyAbilityUpgrades(
      reinterpret_cast<unsigned char*>(&ability), change, Layout(), undo,
      [&](const void* p, void* out, size_t n) {
        return p != properties[2][0].values.data() && Read(p, out, n);
      },
      [&](void* p, const void* in, size_t n) { return Write(p, in, n); });
  EXPECT_FALSE(result);
  EXPECT_EQ(writes, 0);
}

TEST_F(AbilityUpgrades, RollsBackEarlierAndPartiallyWrittenFailingBonus) {
  int attempts = 0;
  const auto result = ApplyAbilityUpgrades(
      reinterpret_cast<unsigned char*>(&ability), change, Layout(), undo,
      [&](const void* p, void* out, size_t n) { return Read(p, out, n); },
      [&](void* p, const void* in, size_t n) {
        if (++attempts == 2) {
          Write(p, in, sizeof(float));
          return false;
        }
        return Write(p, in, n);
      });
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("original bonuses restored"), std::string::npos);
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
  EXPECT_EQ(properties[2][0].values, (Bonuses{-11.5, -8}));
  EXPECT_EQ(attempts, 4);
}

TEST_F(AbilityUpgrades, RollsBackWhenReadbackDoesNotMatch) {
  int attempts = 0;
  const auto result = ApplyAbilityUpgrades(
      reinterpret_cast<unsigned char*>(&ability), change, Layout(), undo,
      [&](const void* p, void* out, size_t n) { return Read(p, out, n); },
      [&](void* p, const void* in, size_t n) { return ++attempts == 2 || Write(p, in, n); });
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("original bonuses restored"), std::string::npos);
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
  EXPECT_EQ(properties[2][0].values, (Bonuses{-11.5, -8}));
}

TEST_F(AbilityUpgrades, ReportsFailedRollbackAndKeepsOriginalsForUnloadRecovery) {
  int attempts = 0;
  const auto result = ApplyAbilityUpgrades(
      reinterpret_cast<unsigned char*>(&ability), change, Layout(), undo,
      [&](const void* p, void* out, size_t n) { return Read(p, out, n); },
      [&](void* p, const void* in, size_t n) {
        if (++attempts == 1) {
          return Write(p, in, n);
        }
        return false;
      });
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("rollback failed"), std::string::npos);
  EXPECT_EQ(properties[1][0].values, (Bonuses{236.22, 236.22}));
  undo.Restore();
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
  EXPECT_EQ(properties[2][0].values, (Bonuses{-11.5, -8}));
}

TEST_F(AbilityUpgrades, UnloadRestoresOriginalsAfterRepeatedApplicationsAndWorldChanges) {
  ASSERT_TRUE(Apply());
  undo.ForgetWorld();
  (*change.mutable_upgrades(0)->mutable_properties())["AbilityCastRange"] = 500;
  ASSERT_TRUE(Apply());
  EXPECT_EQ(properties[1][0].values, (Bonuses{500, 500}));
  undo.Restore();
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
  EXPECT_EQ(properties[2][0].values, (Bonuses{-11.5, -8}));
  ASSERT_TRUE(Apply());
  undo.Restore();
  EXPECT_EQ(properties[1][0].values, (Bonuses{1181, 1500}));
}

}  // namespace
