#include "wasm/pawn_services.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <span>

#include "gtest/gtest.h"
#include "modlock/gameinterop/entity_abi.h"

namespace {

using modlock::gameinterop::SchemaField;
using modlock::wasm::FindPawnService;

struct Fixture {
  std::array<unsigned char, 96> pawn{};
  std::array<unsigned char, 128> observer{}, movement{}, replacement{};
  std::map<std::string, SchemaField> fields;
  std::set<std::string> ancestors{"CBasePlayerPawn", "CCitadelObserverPawn"};
  int queries = 0;
  int reads = 0;
  bool readable = true;

  void Bind(const std::string& name, size_t offset, unsigned char* pointer) {
    fields.insert_or_assign(name, SchemaField{offset, sizeof(pointer), true});
    std::memcpy(pawn.data() + offset, &pointer, sizeof(pointer));
  }

  std::expected<unsigned char*, std::string> Address(const std::string& name, size_t offset) {
    const auto service = FindPawnService(name);
    if (!service) {
      return std::unexpected("not a pawn service");
    }
    return service->FieldAddress(
        pawn.data(), offset,
        [this](std::string_view base) { return ancestors.contains(std::string(base)); },
        [this](std::string_view field) -> std::expected<SchemaField, std::string> {
          ++queries;
          const auto found = fields.find(std::string(field));
          if (found == fields.end()) {
            return std::unexpected("field missing from schema");
          }
          return found->second;
        },
        [this](const void* address, void* out, size_t size) {
          ++reads;
          const auto start = reinterpret_cast<uintptr_t>(pawn.data());
          const auto source = reinterpret_cast<uintptr_t>(address);
          if (!readable || source < start || source - start > pawn.size() ||
              size > pawn.size() - (source - start)) {
            return false;
          }
          std::memcpy(out, address, size);
          return true;
        });
  }
};

TEST(PawnServices, ObserverFieldsUseTheirOwnComponentAndSupportReadAndWrite) {
  Fixture fixture;
  fixture.Bind("m_pObserverServices", 24, fixture.observer.data());
  fixture.Bind("m_pMovementServices", 56, fixture.movement.data());
  const uint32_t target = 0x123456;
  std::memcpy(fixture.observer.data() + 44, &target, sizeof(target));
  const auto address = fixture.Address("CPlayer_ObserverServices", 44);
  ASSERT_TRUE(address) << address.error();
  uint32_t read_target = 0;
  std::memcpy(&read_target, *address, sizeof(read_target));
  EXPECT_EQ(read_target, target);

  const uint32_t next_target = 0x234567;
  std::memcpy(*address, &next_target, sizeof(next_target));
  std::memcpy(&read_target, fixture.observer.data() + 44, sizeof(read_target));
  EXPECT_EQ(read_target, next_target);
  EXPECT_EQ(fixture.movement, (std::array<unsigned char, 128>{}));
  EXPECT_EQ(fixture.Address("CPlayer_MovementServices", 44).value_or(nullptr),
            fixture.movement.data() + 44);
  EXPECT_EQ(fixture.Address("CCitadelObserver_MovementServices", 44).value_or(nullptr),
            fixture.movement.data() + 44);
}

TEST(PawnServices, FollowsRelocatedSchemaFieldsAndReplacementComponents) {
  for (const size_t pointer_offset : {16, 64}) {
    for (const size_t field_offset : {8, 48}) {
      Fixture fixture;
      fixture.Bind("m_pObserverServices", pointer_offset, fixture.observer.data());
      EXPECT_EQ(fixture.Address("CPlayer_ObserverServices", field_offset).value_or(nullptr),
                fixture.observer.data() + field_offset);
      fixture.Bind("m_pObserverServices", pointer_offset, fixture.replacement.data());
      EXPECT_EQ(fixture.Address("CPlayer_ObserverServices", field_offset).value_or(nullptr),
                fixture.replacement.data() + field_offset);
    }
  }
}

TEST(PawnServices, RefusesWrongPawnAncestryBeforeLookingUpOrReadingPointers) {
  Fixture fixture;
  fixture.Bind("m_pObserverServices", 24, fixture.observer.data());
  fixture.ancestors.clear();
  EXPECT_FALSE(fixture.Address("CPlayer_ObserverServices", 44));
  fixture.ancestors = {"CBasePlayerPawn", "CCitadelObserverPawn"};
  EXPECT_FALSE(fixture.Address("CCitadelPlayer_MovementServices", 44));
  EXPECT_EQ(fixture.queries, 0);
  EXPECT_EQ(fixture.reads, 0);
}

TEST(PawnServices, MissingOrUndersizedSchemaDoesNotReadNativeMemory) {
  Fixture fixture;
  auto missing = fixture.Address("CPlayer_ObserverServices", 44);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(), "field missing from schema");
  fixture.Bind("m_pObserverServices", 24, fixture.observer.data());
  fixture.fields.at("m_pObserverServices").size = sizeof(void*) - 1;
  EXPECT_FALSE(fixture.Address("CPlayer_ObserverServices", 44));
  EXPECT_EQ(fixture.reads, 0);
}

TEST(PawnServices, NullAndUnreadablePointersAreRefused) {
  Fixture fixture;
  fixture.Bind("m_pObserverServices", 24, nullptr);
  EXPECT_FALSE(fixture.Address("CPlayer_ObserverServices", 44));
  fixture.Bind("m_pObserverServices", 24, fixture.observer.data());
  fixture.readable = false;
  EXPECT_FALSE(fixture.Address("CPlayer_ObserverServices", 44));
}

}  // namespace
