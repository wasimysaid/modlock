#include "wasm/modifier_definition.h"

#include <array>
#include <cstring>
#include <limits>

#include "gtest/gtest.h"
#include "wasm/definition_changes.h"

namespace {

std::array<unsigned char, 128> definition;
constexpr char kName[] = "citadel_ability_hook/modifier_citadel_hooktarget";
constexpr std::uintptr_t kVtable = 0x12345678;
int32_t requested_scope = 0;
uint32_t requested_hash = 0;

void* Lookup(int32_t scope, uint32_t hash) {
  requested_scope = scope;
  requested_hash = hash;
  return definition.data();
}

bool Read(const void* source, void* out, size_t size) {
  const auto address = reinterpret_cast<std::uintptr_t>(source);
  for (const auto [base, span] :
       {std::pair{reinterpret_cast<std::uintptr_t>(definition.data()), definition.size()},
        std::pair{reinterpret_cast<std::uintptr_t>(kName), sizeof(kName)}}) {
    if (address >= base && address - base <= span && size <= span - (address - base)) {
      std::memcpy(out, source, size);
      return true;
    }
  }
  return false;
}

void Reset() {
  definition.fill(0);
  const int32_t scope = 2;
  const char* name = kName;
  const float original = 2000;
  std::memcpy(definition.data(), &kVtable, sizeof(kVtable));
  std::memcpy(definition.data() + 8, &scope, sizeof(scope));
  std::memcpy(definition.data() + 16, &name, sizeof(name));
  std::memcpy(definition.data() + 64, &original, sizeof(original));
}

modlock::gameinterop::SchemaField Field() {
  return {.offset = 64,
          .size = 4,
          .networked = false,
          .storage = modlock::gameinterop::SchemaStorage::kNumber,
          .list = false,
          .type = "float32"};
}

TEST(ModifierDefinitions, ResolvesTheModifierRegistryWithoutReadingAbilityFlags) {
  Reset();
  definition[0x2a] = 0xff;
  const auto found = modlock::wasm::FindModifierDefinition(Lookup, kName, Read);
  ASSERT_TRUE(found) << found.error();
  EXPECT_EQ(*found, definition.data());
  EXPECT_EQ(requested_scope, 2);
  EXPECT_NE(requested_hash, 0u);
  EXPECT_EQ(definition[0x2a], 0xff);
}

TEST(ModifierDefinitions, RejectsWrongScopeNameAndUnreadableHeaders) {
  Reset();
  definition[8] = 4;
  EXPECT_FALSE(modlock::wasm::FindModifierDefinition(Lookup, kName, Read));
  Reset();
  EXPECT_FALSE(modlock::wasm::FindModifierDefinition(Lookup, "ability/different", Read));
  EXPECT_FALSE(modlock::wasm::FindModifierDefinition(Lookup, "ability;quit/modifier", Read));
  EXPECT_FALSE(modlock::wasm::FindModifierDefinition(
      Lookup, kName, [](const void*, void*, size_t) { return false; }));
}

TEST(ModifierDefinitions, AcceptsOnlyTheActualClassAndAFloatWithinItsBounds) {
  Reset();
  auto field = Field();
  const auto storage = modlock::wasm::CheckedModifierFloat(definition.data(), field,
                                                           definition.size(), kVtable, Read);
  ASSERT_TRUE(storage) << storage.error();
  EXPECT_EQ(*storage, definition.data() + 64);
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), field, definition.size(),
                                                   kVtable + 8, Read));
  field.type = "int32";
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), field, definition.size(),
                                                   kVtable, Read));
  field = Field();
  field.offset = definition.size() - 1;
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), field, definition.size(),
                                                   kVtable, Read));
  field.offset = 8;
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), field, definition.size(),
                                                   kVtable, Read));
  field = Field();
  field.list = true;
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), field, definition.size(),
                                                   kVtable, Read));
}

TEST(ModifierDefinitions, RejectsNonFiniteOrUnreadableValues) {
  Reset();
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(definition.data() + 64, &invalid, sizeof(invalid));
  EXPECT_FALSE(modlock::wasm::CheckedModifierFloat(definition.data(), Field(), definition.size(),
                                                   kVtable, Read));
  Reset();
  EXPECT_FALSE(
      modlock::wasm::CheckedModifierFloat(definition.data(), Field(), definition.size(), kVtable,
                                          [](const void*, void*, size_t) { return false; }));
}

class EmptyImage : public modlock::gameinterop::ModuleImage {
 public:
  std::uintptr_t base() const override { return 0; }

  std::span<const uint8_t> image_bytes() const override { return {}; }
};

TEST(ModifierDefinitions, RejectsPointerTraversalAndMapPathsBeforeAccessingTheSchema) {
  for (const char* path : {"Class.pointer/Modifier.speed", "Class.float[Enum.Value]",
                           "Class.member.other", "speed", "Class."}) {
    EXPECT_FALSE(modlock::wasm::ResolveModifierFloat(EmptyImage{}, nullptr, "server.dll", nullptr,
                                                     path, Read));
  }
}

TEST(ModifierDefinitions, UndoRetainsTheOriginalAcrossWorldsAndRepeatedChanges) {
  Reset();
  modlock::wasm::DefinitionUndo undo;
  auto* storage = definition.data() + 64;
  undo.Save(storage, sizeof(float), false);
  const float first = 650;
  std::memcpy(storage, &first, sizeof(first));
  undo.ForgetWorld();
  undo.Save(storage, sizeof(float), false);
  const float second = 700;
  std::memcpy(storage, &second, sizeof(second));
  undo.Restore();
  float restored = 0;
  std::memcpy(&restored, storage, sizeof(restored));
  EXPECT_EQ(restored, 2000);
}

}  // namespace
