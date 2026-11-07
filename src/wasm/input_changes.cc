//go:build deps_only

#include "wasm/input_changes.h"

#include <utility>

#include "wasm/game_internal.h"

namespace modlock::wasm {

void InputChanges::Block(uint64_t buttons) {
  blocked_ = buttons;
}

void InputChanges::BlockPlayer(int32_t slot, uint64_t buttons) {
  if (buttons == 0) {
    slot_blocked_.erase(slot);
  } else {
    slot_blocked_[slot] = buttons;
  }
}

void InputChanges::Press(int32_t slot, uint64_t buttons) {
  presses_[slot] |= buttons;
}

void InputChanges::CancelPress(int32_t slot, uint64_t buttons) {
  const auto press = presses_.find(slot);
  if (press == presses_.end()) {
    return;
  }
  press->second &= ~buttons;
  if (press->second == 0) {
    presses_.erase(press);
  }
}

void InputChanges::Watch(uint64_t buttons) {
  watched_ = buttons;
}

void InputChanges::Remap(const wasm::RemapInputRequest& remap) {
  remap_ = remap;
}

uint64_t InputChanges::Blocked(int32_t slot) const {
  const auto found = slot_blocked_.find(slot);
  return blocked_ | (found == slot_blocked_.end() ? 0 : found->second);
}

void InputChanges::Record(int32_t slot, uint64_t pressed, uint64_t released) {
  if ((pressed | released) & watched_ && changes_.size() < kMaxChanges) {
    changes_.push_back({slot, pressed & watched_, released & watched_});
  }
}

uint64_t InputChanges::ApplyPress(gameinterop::AbilityInputHook::Input& input) {
  // Take the pending press for this slot.
  auto press = presses_.find(input.slot);
  if (press == presses_.end()) {
    return 0;
  }

  // A press is down and newly changed for this one input.
  const uint64_t buttons = press->second;
  input.buttons[0] |= buttons;
  input.buttons[1] |= buttons;
  input.buttons[2] &= ~buttons;
  presses_.erase(press);
  return buttons;
}

void InputChanges::ApplyRemap(gameinterop::AbilityInputHook::Input& input) const {
  // Move each state's from buttons to the to buttons.
  if (remap_.from() == 0) {
    return;
  }
  const bool held = input.buttons[0] & remap_.from();
  for (auto& state : input.buttons) {
    state = RemapButtons(state);
  }

  // A repeating remap presses the to buttons on every input the from buttons
  // are held.
  if (held && remap_.repeat()) {
    input.buttons[1] |= remap_.to();
  }
}

uint64_t InputChanges::RemapButtons(uint64_t buttons) const {
  return buttons & remap_.from() ? (buttons & ~remap_.from()) | remap_.to() : buttons;
}

std::vector<InputChanges::ButtonChange> InputChanges::TakeChanges() {
  return std::exchange(changes_, {});
}

void InputChanges::Leave(int32_t slot) {
  slot_blocked_.erase(slot);
  presses_.erase(slot);
}

void InputChanges::DropPresses() {
  presses_.clear();
}

void InputChanges::Clear() {
  *this = {};
}

}  // namespace modlock::wasm
