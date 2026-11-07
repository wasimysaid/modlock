//go:build deps_only

#include "wasm/modifier_definition.h"

#include <cmath>
#include <cstring>

#include "modlock/gameinterop/keyvalues.h"
#include "modlock/gameinterop/virtual_slots.h"

namespace modlock::wasm {

std::expected<unsigned char*, std::string> FindModifierDefinition(
    gameinterop::AbilityDefinitions::Lookup lookup, std::string_view entry,
    const gameinterop::BoundedReader& read) {
  if (!lookup || entry.empty() || entry.size() > 255 || entry.find('/') == entry.npos ||
      entry.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-") != entry.npos) {
    return std::unexpected("modifier entry must be ability/modifier");
  }
  auto* definition =
      static_cast<unsigned char*>(lookup(2, gameinterop::MakeMemberName(entry).hash));
  if (!definition) {
    return std::unexpected("modifier definition is not loaded");
  }
  int32_t scope = 0;
  const char* name = nullptr;
  if (!read(definition + 8, &scope, sizeof(scope)) || scope != 2 ||
      !read(definition + 16, &name, sizeof(name)) || !name) {
    return std::unexpected("modifier definition header is invalid");
  }
  for (size_t i = 0; i <= entry.size(); ++i) {
    char letter = 0;
    if (!read(name + i, &letter, sizeof(letter)) ||
        letter != (i == entry.size() ? '\0' : entry[i])) {
      return std::unexpected("modifier definition name does not match the lookup");
    }
  }
  return definition;
}

std::expected<unsigned char*, std::string> CheckedModifierFloat(
    unsigned char* definition, const gameinterop::SchemaField& field, size_t class_size,
    std::uintptr_t expected_vtable, const gameinterop::BoundedReader& read) {
  if (!definition || !field.type || std::string_view(field.type) != "float32" || field.list ||
      field.storage != gameinterop::SchemaStorage::kNumber || field.size < sizeof(float) ||
      field.offset < 40 || field.offset > class_size || class_size - field.offset < sizeof(float)) {
    return std::unexpected("modifier field is not a direct float within its schema class");
  }
  std::uintptr_t vtable = 0;
  if (!expected_vtable || !read(definition, &vtable, sizeof(vtable)) || vtable != expected_vtable) {
    return std::unexpected("modifier definition does not have the requested schema class");
  }
  float value = 0;
  if (!read(definition + field.offset, &value, sizeof(value)) || !std::isfinite(value)) {
    return std::unexpected("modifier field cannot be read as a finite float");
  }
  return definition + field.offset;
}

std::expected<unsigned char*, std::string> ResolveModifierFloat(
    const gameinterop::ModuleImage& module, void* schema, const char* module_name,
    unsigned char* definition, std::string_view path, const gameinterop::BoundedReader& read) {
  const auto dot = path.find('.');
  if (dot == path.npos || dot == 0 || dot + 1 == path.size() ||
      path.find_first_of("/[]") != path.npos || path.find('.', dot + 1) != path.npos) {
    return std::unexpected("modifier field must be a direct Class.field path");
  }
  const std::string type(path.substr(0, dot));
  const std::string name(path.substr(dot + 1));
  auto scope = gameinterop::FindTypeScopeOf(schema, module_name);
  if (!scope) {
    return std::unexpected(scope.error());
  }
  auto info = gameinterop::FindDeclaredClassOf(*scope, type.c_str());
  if (!info || !gameinterop::SchemaClassDerivesFrom(info.value_or(nullptr), "CModifierVData")) {
    return std::unexpected("requested class is not a modifier definition");
  }
  auto field = gameinterop::SchemaFieldOf(schema, module_name, type.c_str(), name.c_str());
  auto size = gameinterop::SchemaClassSizeOf(schema, module_name, type.c_str());
  auto table = gameinterop::FindVtable(module, type);
  if (!field || !size || !table) {
    return std::unexpected(!field ? field.error() : !size ? size.error() : table.error());
  }
  return CheckedModifierFloat(definition, *field, *size, module.base() + *table, read);
}

}  // namespace modlock::wasm
