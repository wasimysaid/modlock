#include "wasm/input_changes.h"

#include <array>
#include <cstring>

#include <gtest/gtest.h>

namespace {

using modlock::gameinterop::AbilityInputHook;
using modlock::wasm::InputChanges;
constexpr uint64_t kItem = uint64_t{1} << 37;
constexpr uint64_t kAttack = 1;
constexpr uint64_t kJump = 2;

class InputChangesTest : public testing::Test {
 protected:
  InputChanges policy;
  std::array<unsigned char, 32> pawn{};
  std::array<unsigned char, 48> movement{};

  std::array<uint64_t, 3> Next(int32_t slot, uint64_t held, uint64_t changed) {
    const uint32_t controller = (17u << 15) | static_cast<uint32_t>(slot + 1);
    void* services = movement.data();
    std::memcpy(pawn.data() + 4, &controller, sizeof(controller));
    std::memcpy(pawn.data() + 16, &services, sizeof(services));
    std::array<uint64_t, 3> buttons{held, changed, 0};
    std::memcpy(movement.data() + 8, buttons.data(), sizeof(buttons));
    const auto read = [&](const void* source, void* target, size_t size) {
      const auto address = reinterpret_cast<std::uintptr_t>(source);
      for (const auto [base, span] :
           {std::pair{pawn.data(), pawn.size()}, std::pair{movement.data(), movement.size()}}) {
        const auto start = reinterpret_cast<std::uintptr_t>(base);
        if (address >= start && address - start <= span && size <= span - (address - start)) {
          std::memcpy(target, source, size);
          return true;
        }
      }
      return false;
    };
    auto processed = AbilityInputHook::Process(
        pawn.data(), {4, 16, 8}, read,
        [&](void* target, const void* source, size_t size) {
          EXPECT_EQ(target, movement.data() + 8);
          EXPECT_EQ(size, sizeof(buttons));
          std::memcpy(target, source, size);
          return true;
        },
        [&](AbilityInputHook::Input& input) {
          const auto blocked = policy.Blocked(input.slot);
          policy.Record(input.slot, input.buttons[0] & input.buttons[1],
                        ~input.buttons[0] & input.buttons[1]);
          auto injected = policy.ApplyPress(input);
          policy.ApplyRemap(input);
          injected = policy.RemapButtons(injected);
          return blocked & ~injected;
        });
    EXPECT_TRUE(processed) << (processed ? "" : processed.error());
    std::memcpy(buttons.data(), movement.data() + 8, sizeof(buttons));
    return buttons;
  }
};

TEST_F(InputChangesTest, ScriptedItemPassesOnceWithoutReleasingOtherBlockedButtons) {
  policy.Block(kItem | kAttack);
  policy.Press(2, kItem);
  EXPECT_EQ(Next(2, kAttack | kJump, kAttack | kJump),
            (std::array<uint64_t, 3>{kItem | kJump, kItem | kJump, 0}));
  EXPECT_EQ(policy.Blocked(2), kItem | kAttack);
  EXPECT_EQ(Next(2, kItem | kAttack | kJump, kItem | kAttack | kJump),
            (std::array<uint64_t, 3>{kJump, kJump, 0}));
}

TEST_F(InputChangesTest, InjectionBelongsOnlyToItsPlayer) {
  policy.Block(kItem);
  policy.Press(2, kItem);
  EXPECT_EQ(Next(3, kItem, kItem), (std::array<uint64_t, 3>{0, 0, 0}));
  EXPECT_EQ(Next(2, 0, 0), (std::array<uint64_t, 3>{kItem, kItem, 0}));
  EXPECT_EQ(Next(2, kItem, kItem), (std::array<uint64_t, 3>{0, 0, 0}));
}

TEST_F(InputChangesTest, WatchedEdgesContainRealInputAndNeverInjectedPresses) {
  policy.Block(kItem);
  policy.Watch(kItem | kJump);
  policy.Press(2, kItem);
  Next(2, kJump, kJump);
  auto changes = policy.TakeChanges();
  ASSERT_EQ(changes.size(), 1);
  EXPECT_EQ(changes[0].pressed, kJump);
  Next(2, kItem, kItem | kJump);
  changes = policy.TakeChanges();
  ASSERT_EQ(changes.size(), 1);
  EXPECT_EQ(changes[0].pressed, kItem);
  EXPECT_EQ(changes[0].released, kJump);
}

TEST_F(InputChangesTest, DepartedPlayerAndEndedWorldCannotLeaveAnExemption) {
  policy.Block(kItem);
  policy.Press(2, kItem);
  policy.Leave(2);
  EXPECT_EQ(Next(2, kItem, kItem), (std::array<uint64_t, 3>{0, 0, 0}));
  policy.Press(2, kItem);
  policy.DropPresses();
  EXPECT_EQ(Next(2, kItem, kItem), (std::array<uint64_t, 3>{0, 0, 0}));
}

TEST_F(InputChangesTest, CancellationPreservesOtherQueuedButtonsAndOtherPlayers) {
  policy.Block(kItem | kAttack);
  policy.Press(2, kItem | kAttack);
  policy.Press(3, kItem);
  policy.CancelPress(2, kItem);
  EXPECT_EQ(Next(2, kItem | kJump, kItem | kJump),
            (std::array<uint64_t, 3>{kAttack | kJump, kAttack | kJump, 0}));
  EXPECT_EQ(Next(3, 0, 0), (std::array<uint64_t, 3>{kItem, kItem, 0}));
  EXPECT_EQ(Next(2, kAttack, kAttack), (std::array<uint64_t, 3>{0, 0, 0}));
}

TEST_F(InputChangesTest, CancellationNeverSuppressesRealInput) {
  policy.Press(2, kJump);
  policy.CancelPress(2, kJump);
  EXPECT_EQ(Next(2, kJump, kJump), (std::array<uint64_t, 3>{kJump, kJump, 0}));
  EXPECT_EQ(Next(2, 0, 0), (std::array<uint64_t, 3>{0, 0, 0}));
}

TEST_F(InputChangesTest, OnlyScriptedPressesBypassBlocksAfterRemapping) {
  modlock::wasm::RemapInputRequest remap;
  remap.set_from(kJump);
  remap.set_to(kItem);
  remap.set_repeat(true);
  policy.Remap(remap);
  policy.Block(kItem | kAttack);
  policy.Press(2, kJump);
  EXPECT_EQ(Next(2, kAttack, kAttack), (std::array<uint64_t, 3>{kItem, kItem, 0}));
  EXPECT_EQ(Next(2, kJump | kAttack, kJump | kAttack),
            (std::array<uint64_t, 3>{0, 0, 0}));
}

}  // namespace
