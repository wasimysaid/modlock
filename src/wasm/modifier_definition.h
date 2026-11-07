#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "modlock/gameinterop/ability_definitions.h"
#include "modlock/gameinterop/entity_abi.h"

namespace modlock::wasm {

std::expected<unsigned char*, std::string> FindModifierDefinition(
    gameinterop::AbilityDefinitions::Lookup lookup, std::string_view entry,
    const gameinterop::BoundedReader& read = gameinterop::ReadNative);

std::expected<unsigned char*, std::string> CheckedModifierFloat(
    unsigned char* definition, const gameinterop::SchemaField& field, size_t class_size,
    std::uintptr_t expected_vtable,
    const gameinterop::BoundedReader& read = gameinterop::ReadNative);

std::expected<unsigned char*, std::string> ResolveModifierFloat(
    const gameinterop::ModuleImage& module, void* schema, const char* module_name,
    unsigned char* definition, std::string_view path,
    const gameinterop::BoundedReader& read = gameinterop::ReadNative);

}  // namespace modlock::wasm
