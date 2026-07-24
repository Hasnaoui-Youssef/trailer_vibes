#include "dap/protocol/dap_types.hpp"

#include "llvm/ADT/StringSwitch.h"

using namespace llvm;

namespace dap::protocol {

llvm::StringRef SymbolTypeToString(SymbolType type) {
  switch (type) {
  case SymbolType::eSymbolTypeInvalid: return "Invalid";
  case SymbolType::eSymbolTypeAbsolute: return "Absolute";
  case SymbolType::eSymbolTypeCode: return "Code";
  case SymbolType::eSymbolTypeResolver: return "Resolver";
  case SymbolType::eSymbolTypeData: return "Data";
  case SymbolType::eSymbolTypeTrampoline: return "Trampoline";
  case SymbolType::eSymbolTypeRuntime: return "Runtime";
  case SymbolType::eSymbolTypeException: return "Exception";
  case SymbolType::eSymbolTypeSourceFile: return "SourceFile";
  case SymbolType::eSymbolTypeHeaderFile: return "HeaderFile";
  case SymbolType::eSymbolTypeObjectFile: return "ObjectFile";
  case SymbolType::eSymbolTypeCommonBlock: return "CommonBlock";
  case SymbolType::eSymbolTypeBlock: return "Block";
  case SymbolType::eSymbolTypeLocal: return "Local";
  case SymbolType::eSymbolTypeParam: return "Param";
  case SymbolType::eSymbolTypeVariable: return "Variable";
  case SymbolType::eSymbolTypeVariableType: return "VariableType";
  case SymbolType::eSymbolTypeLineEntry: return "LineEntry";
  case SymbolType::eSymbolTypeLineHeader: return "LineHeader";
  case SymbolType::eSymbolTypeScopeBegin: return "ScopeBegin";
  case SymbolType::eSymbolTypeScopeEnd: return "ScopeEnd";
  case SymbolType::eSymbolTypeAdditional: return "Additional";
  case SymbolType::eSymbolTypeCompiler: return "Compiler";
  case SymbolType::eSymbolTypeInstrumentation: return "Instrumentation";
  case SymbolType::eSymbolTypeUndefined: return "Undefined";
  case SymbolType::eSymbolTypeObjCClass: return "ObjCClass";
  case SymbolType::eSymbolTypeObjCMetaClass: return "ObjCMetaClass";
  case SymbolType::eSymbolTypeObjCIVar: return "ObjCIVar";
  case SymbolType::eSymbolTypeReExported: return "ReExported";
  }
  return "Invalid";
}

SymbolType SymbolTypeFromString(llvm::StringRef str) {
  return llvm::StringSwitch<SymbolType>(str)
      .Case("Invalid", SymbolType::eSymbolTypeInvalid)
      .Case("Absolute", SymbolType::eSymbolTypeAbsolute)
      .Case("Code", SymbolType::eSymbolTypeCode)
      .Case("Resolver", SymbolType::eSymbolTypeResolver)
      .Case("Data", SymbolType::eSymbolTypeData)
      .Case("Trampoline", SymbolType::eSymbolTypeTrampoline)
      .Case("Runtime", SymbolType::eSymbolTypeRuntime)
      .Case("Exception", SymbolType::eSymbolTypeException)
      .Case("SourceFile", SymbolType::eSymbolTypeSourceFile)
      .Case("HeaderFile", SymbolType::eSymbolTypeHeaderFile)
      .Case("ObjectFile", SymbolType::eSymbolTypeObjectFile)
      .Case("CommonBlock", SymbolType::eSymbolTypeCommonBlock)
      .Case("Block", SymbolType::eSymbolTypeBlock)
      .Case("Local", SymbolType::eSymbolTypeLocal)
      .Case("Param", SymbolType::eSymbolTypeParam)
      .Case("Variable", SymbolType::eSymbolTypeVariable)
      .Case("VariableType", SymbolType::eSymbolTypeVariableType)
      .Case("LineEntry", SymbolType::eSymbolTypeLineEntry)
      .Case("LineHeader", SymbolType::eSymbolTypeLineHeader)
      .Case("ScopeBegin", SymbolType::eSymbolTypeScopeBegin)
      .Case("ScopeEnd", SymbolType::eSymbolTypeScopeEnd)
      .Case("Additional", SymbolType::eSymbolTypeAdditional)
      .Case("Compiler", SymbolType::eSymbolTypeCompiler)
      .Case("Instrumentation", SymbolType::eSymbolTypeInstrumentation)
      .Case("Undefined", SymbolType::eSymbolTypeUndefined)
      .Case("ObjCClass", SymbolType::eSymbolTypeObjCClass)
      .Case("ObjCMetaClass", SymbolType::eSymbolTypeObjCMetaClass)
      .Case("ObjCIVar", SymbolType::eSymbolTypeObjCIVar)
      .Case("ReExported", SymbolType::eSymbolTypeReExported)
      .Default(SymbolType::eSymbolTypeInvalid);
}

bool fromJSON(const llvm::json::Value &Params, PersistenceData &PD,
              llvm::json::Path P) {
  json::ObjectMapper O(Params, P);
  return O && O.mapOptional("module_path", PD.module_path) &&
         O.mapOptional("symbol_name", PD.symbol_name);
}

llvm::json::Value toJSON(const PersistenceData &PD) {
  json::Object result{
      {"module_path", PD.module_path},
      {"symbol_name", PD.symbol_name},
  };

  return result;
}

bool fromJSON(const llvm::json::Value &Params, SourceLLDBData &SLD,
              llvm::json::Path P) {
  json::ObjectMapper O(Params, P);
  return O && O.mapOptional("persistenceData", SLD.persistenceData);
}

llvm::json::Value toJSON(const SourceLLDBData &SLD) {
  json::Object result;
  if (SLD.persistenceData)
    result.insert({"persistenceData", SLD.persistenceData});
  return result;
}

bool fromJSON(const llvm::json::Value &Params, Symbol &DS, llvm::json::Path P) {
  json::ObjectMapper O(Params, P);
  std::string type_str;
  if (!(O && O.map("id", DS.id) && O.map("isDebug", DS.isDebug) &&
        O.map("isSynthetic", DS.isSynthetic) &&
        O.map("isExternal", DS.isExternal) && O.map("type", type_str) &&
        O.map("fileAddress", DS.fileAddress) &&
        O.mapOptional("loadAddress", DS.loadAddress) &&
        O.map("size", DS.size) && O.map("name", DS.name)))
    return false;

  DS.type = SymbolTypeFromString(type_str);
  return true;
}

llvm::json::Value toJSON(const Symbol &DS) {
  json::Object result{
      {"id", DS.id},
      {"isDebug", DS.isDebug},
      {"isSynthetic", DS.isSynthetic},
      {"isExternal", DS.isExternal},
      {"type", SymbolTypeToString(DS.type)},
      {"fileAddress", DS.fileAddress},
      {"loadAddress", DS.loadAddress},
      {"size", DS.size},
      {"name", DS.name},
  };

  return result;
}

} // namespace dap::protocol
