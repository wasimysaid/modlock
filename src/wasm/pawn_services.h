#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "modlock/gameinterop/native_memory.h"

namespace modlock::wasm {

struct PawnService {
  std::string_view pawn_class;
  std::string_view pointer_field;

  template <typename Derives, typename FindField>
  std::expected<unsigned char*, std::string> FieldAddress(
      void* entity, size_t offset, Derives derives, FindField find_field,
      const gameinterop::BoundedReader& read) const {
    if (!derives(pawn_class)) {
      return std::unexpected("the entity does not derive from " + std::string(pawn_class));
    }
    const auto field = find_field(pointer_field);
    if (!field) {
      return std::unexpected(field.error());
    }
    if (field->size < sizeof(void*)) {
      return std::unexpected(std::string(pointer_field) + " cannot hold a services pointer");
    }
    unsigned char* services = nullptr;
    if (!read(static_cast<unsigned char*>(entity) + field->offset, &services, sizeof(services)) ||
        !services) {
      return std::unexpected("the pawn has no readable " + std::string(pointer_field));
    }
    return services + offset;
  }
};

inline std::optional<PawnService> FindPawnService(std::string_view class_name) {
  if (class_name == "CPlayer_ObserverServices") {
    return PawnService{"CBasePlayerPawn", "m_pObserverServices"};
  }
  if (class_name == "CPlayer_MovementServices") {
    return PawnService{"CBasePlayerPawn", "m_pMovementServices"};
  }
  if (class_name == "CCitadelPlayer_MovementServices") {
    return PawnService{"CCitadelPlayerPawn", "m_pMovementServices"};
  }
  if (class_name == "CCitadelObserver_MovementServices") {
    return PawnService{"CCitadelObserverPawn", "m_pMovementServices"};
  }
  return std::nullopt;
}

}  // namespace modlock::wasm
