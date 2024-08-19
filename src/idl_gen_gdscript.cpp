// independent of idl_parser, since this code is not needed for most clients

#include "idl_gen_gdscript.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "flatbuffers/base.h"
#include "flatbuffers/code_generators.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

namespace flatbuffers {

// Make the string upper case
static inline std::string ToUpper(std::string val) {
  std::locale loc;
  auto &facet = std::use_facet<std::ctype<char>>(loc);
  facet.toupper(&val[0], &val[0] + val.length());
  return val;
}

// Make the string lower case
static inline std::string ToLower(std::string val) {
  std::locale loc;
  auto &facet = std::use_facet<std::ctype<char>>(loc);
  facet.tolower(&val[0], &val[0] + val.length());
  return val;
}

namespace gdscript {

// FIXME Consolidate the naming across the project
const char *add_element_func[] = {
    "",               // 0 NONE
    "add_element_ubyte",      // 1 UTYPE
    "add_element_bool",      // 2 BOOL
    "add_element_byte",       // 3 CHAR
    "add_element_ubyte",      // 4 UCHAR
    "add_element_short",      // 5 SHORT
    "add_element_ushort",     // 6 USHORT
    "add_element_int",      // 7 INT
    "add_element_uint",     // 8 UINT
    "add_element_long",      // 9 LONG
    "add_element_ulong",     //10 ULONG
    "add_element_float",    //11 FLOAT
    "add_element_double",    //12 DOUBLE
    ""/*STRING*/,""/*VECTOR*/, "", /* STRUCT */ "", /* UNION */ "", /* ARRAY */ "", /* VECTOR64 */
};

const char *vector_create_func[] = {
  "",               // 0 NONE
  "create_vector_uint8",      // 1 UTYPE
  "create_vector_uint8",      // 2 BOOL
  "create_vector_int8",       // 3 CHAR
  "create_vector_uint8",      // 4 UCHAR
  "create_vector_int16",      // 5 SHORT
  "create_vector_uint16",     // 6 USHORT
  "create_vector_int32",      // 7 INT
  "create_vector_uint32",     // 8 UINT
  "create_vector_int64",      // 9 LONG
  "create_vector_uint64",     //10 ULONG
  "create_vector_float32",    //11 FLOAT
  "create_vector_float64",    //12 DOUBLE
  "create_PackedStringArray", //13 STRING
  "", /* VECTOR */ "", /* STRUCT */ "", /* UNION */ "", /* ARRAY */ "", /* VECTOR64 */
};

// PackedByteArray types for encode_* and decode_* functions
const char *pba_types[] = {
    "",        // 0 NONE
    "u8",      // 1 UTYPE
    "u8",      // 2 BOOL
    "s8",      // 3 CHAR
    "u8",      // 4 UCHAR
    "s16",     // 5 SHORT
    "u16",     // 6 USHORT
    "s32",     // 7 INT
    "u32",     // 8 UINT
    "s64",     // 9 LONG
    "u64",     //10 ULONG
    "float",   //11 FLOAT
    "double",  //12 DOUBLE
    ""/*STRING*/,""/* VECTOR */, ""/* STRUCT */,
    ""/* UNION */, ""/* ARRAY */, ""/* VECTOR64 */,
};

const char *element_size[] = {
    "", // 0 NONE
    "1", // 1 UTYPE
    "1", // 2 BOOL
    "1", // 3 CHAR
    "1", // 4 UCHAR
    "2", // 5 SHORT
    "2", // 6 USHORT
    "4", // 7 INT
    "4", // 8 UINT
    "8", // 9 LONG
    "8", //10 ULONG
    "4", //11 FLOAT
    "8", //12 DOUBLE
    "4", //13 STRING - a list of offsets to the strings
    "4", //14 VECTOR - a list of offsets to the tables
    "", /* STRUCT */ "", /* UNION */ "", /* ARRAY */ "", /* VECTOR64 */
};

const char *array_conversion[] = {
    "<Error>",            // 0 NONE
    "<Error>",            // 1 UTYPE
    "<Error>",            // 2 BOOL
    "<Error>",            // 3 CHAR
    "<Error>",            // 4 UCHAR
    "<Error>",            // 5 SHORT
    "<Error>",            // 6 USHORT
    "to_int32_array()",   // 7 INT
    "<Error>",            // 8 UINT
    "to_int64_array()",    // 9 LONG
    "<Error>",            //10 ULONG
    "to_float32_array()", //11 FLOAT
    "to_float64_array()", //12 DOUBLE
    "", /* VECTOR */ "", /* STRUCT */ "", /* UNION */ "", /* ARRAY */ "", /* VECTOR64 */
};

// Extension of IDLOptions for gdscript-generator.
struct IDLOptionsGdscript : public IDLOptions {
  // All fields start with 'g_' prefix to distinguish from the base IDLOptions.

  IDLOptionsGdscript(const IDLOptions &opts) // NOLINT(*-explicit-constructor)
      : IDLOptions(opts) {}
};

class GdscriptGenerator : public BaseGenerator {

 public:
  GdscriptGenerator(const Parser &parser, const std::string &path,
               const std::string &file_name, IDLOptionsGdscript opts)
      : BaseGenerator(parser, path, file_name, "", "::", "gd"),
        opts_(std::move(opts))
  {
    code_.SetPadding("\t");

    // FIXME change this to something line internal types or something. because there will be some builtins that are
    // able to be decoded and some that arent. so I'm not sure how things are going right now.
    static const char *const builtin_types_[] = {
        "String",
        "Vector3",
      "Vector3i",
      "Color",
      nullptr
    };
    for (auto kw = builtin_types_; *kw; kw++) builtin_types.insert(*kw);

    static const char *const keywords_[] = {
      "if",
      "elif",
      "else",
      "for",
      "while",
      "match",
      "break",
      "continue",
      "pass",
      "return",
      "class",
      "class_name",
      "extends",
      "is",
      "in",
      "as",
      "self",
      "signal",
      "func",
      "static",
      "const",
      "enum",
      "var",
      "breakpoint",
      "preload",
      "await",
      "yield",
      "assert",
      "void",
      "PI",
      "TAU",
      "INF",
      "NAN",
      // My used keywords
      "bytes",
      "start",
      // Builtin Types that need escaping.
      "Object",
      nullptr,
    };
    for (auto kw = keywords_; *kw; kw++) keywords.insert(*kw);
    for (auto kw = builtin_types_; *kw; kw++) keywords.insert(*kw);
  }

  // Iterate through all definitions we haven't. Generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() override {
    code_.Clear();
    const auto file_path = GeneratedFileName(path_, file_name_, opts_);

    code_.SetValue( "FILE_NAME", file_name_ + "_generated.gd" );
    code_.SetValue( "FILE_PATH", "res://" + file_path );

    code_ += "# " + std::string(FlatBuffersGeneratedWarning() );
    code_ += "";

    include_map[parser_.root_struct_def_->file] = "";
    for( const auto &include : parser_.GetIncludedFiles() ){
      if( include.schema_name == "godot.fbs" ) continue;
      auto include_name = ToLower( include.schema_name.substr(0, include.schema_name.length() -4) );
      auto schema_path = include.filename.substr(0, include.filename.length() -4);
      auto include_path = GeneratedFileName( schema_path, "", opts_);
      include_map[include.filename] =  include_name + "_inc.";
      code_.SetValue( "INCLUDE_PATH", "res://" + include_path );
      code_.SetValue( "SCHEMA_NAME", include_name );
      code_ += "const {{SCHEMA_NAME}}_inc = preload('{{INCLUDE_PATH}}')";
      code_ += "";
//      const inc_sectorlink = preload('res://flatbuffers/SectorLink_generated.gd')
//      # addons/gdflatbuffers/godot.fbs
//      # flatbuffers/SectorLink.fbs
    }

    // convenience function to get the root table without having to pass its position
    code_.SetValue("ROOT_STRUCT", EscapeKeyword( parser_.root_struct_def_->name ) );
    code_ += "static func GetRoot( data : PackedByteArray ) -> {{ROOT_STRUCT}}:";
    code_.IncrementIdentLevel();
    code_ += "return Get{{ROOT_STRUCT}}( data )";
    code_.DecrementIdentLevel();
    code_ += "";


    // Generate code for all the enum declarations.
    for( const auto &enum_def : parser_.enums_.vec ){
      if( not enum_def->generated ){
        GenEnum( *enum_def );
      }
    }

    // Generate code for all structs, then all tables.
    for( const auto &struct_def : parser_.structs_.vec ){
      if( struct_def->fixed and not struct_def->generated ){
        GenStruct( *struct_def );
      }
    }

    for( const auto &struct_def : parser_.structs_.vec ){
      if( not struct_def->fixed and not struct_def->generated ){
        GenTable( *struct_def );
      }
    }

    const auto final_code = code_.ToString();

    // Save the file
    return SaveFile(file_path.c_str(), final_code, false);
  }

 private:
  CodeWriter code_;

  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> builtin_types;
  std::unordered_map<std::string,std::string> include_map;

  const IDLOptionsGdscript opts_;

  std::string EscapeKeyword(const std::string &name) const {
    return keywords.find(name) == keywords.end() ? name : name + "_";
  }

  bool IsBuiltin( const Type &type ){
    if( type.struct_def ) {
      return builtin_types.find(type.struct_def->name) not_eq builtin_types.end();
    }
    return false;
  }

  bool IsIncluded( const Type &type ){
    if( not type.struct_def ) return false;
    if( type.struct_def->file not_eq parser_.root_struct_def_->file ) return true;
    return false;
  }

  std::string GetInclude(const Type &type ){
    if( not (IsStruct( type ) or IsTable( type )) ) return "";
    if( IsBuiltin(type) ) return "";
    if( type.struct_def->file not_eq parser_.root_struct_def_->file ) return include_map[type.struct_def->file];
    if( type.struct_def not_eq parser_.root_struct_def_ ) return "parent.";
    return "";
  }

  std::string Name(const FieldDef &field) const {
    // the union type field suffix is immutable.
    static size_t union_suffix_len = strlen(UnionTypeFieldSuffix());
    const bool is_union_type = field.value.type.base_type == BASE_TYPE_UTYPE;
    // early return if no case transformation required
    if (opts_.cpp_object_api_field_case_style == IDLOptions::CaseStyle_Unchanged) {
      return EscapeKeyword(field.name);
    }
    std::string name = field.name;
    // do not change the case style of the union type field suffix
    if (is_union_type) {
      FLATBUFFERS_ASSERT(name.length() > union_suffix_len);
      name.erase(name.length() - union_suffix_len, union_suffix_len);
    }
    if (opts_.cpp_object_api_field_case_style == IDLOptions::CaseStyle_Upper) {
      name = ConvertCase(name, Case::kUpperCamel);
    } else if (opts_.cpp_object_api_field_case_style ==
             IDLOptions::CaseStyle_Lower) {
      name = ConvertCase(name, Case::kLowerCamel);
    }
    // restore the union field type suffix
    if (is_union_type) name.append(UnionTypeFieldSuffix(), union_suffix_len);
    name = EscapeKeyword(name);
    return name;
  }

  std::string Name(const Definition &def) const {
    return EscapeKeyword(def.name);
  }

  std::string Name(const EnumVal &ev) const { return EscapeKeyword(ev.name); }

  std::string GetGodotType( const Type &type ){
    if( IsBool(type.base_type ) ){
      return "bool";
    }
    else if( IsEnum(type) ){
      return EscapeKeyword( type.enum_def->name );
    }
    else if( IsInteger(type.base_type) ){
      return "int";
    }
    else if( IsFloat(type.base_type) ){
      return "float";
    }
    else if( IsString( type ) ){
      return "String";
    }
    else if( IsStruct( type ) or IsTable( type ) ){
      if( IsBuiltin( type ) ) {
        return type.struct_def->name;
      } else {
        return EscapeKeyword( type.struct_def->name );
      }
    }
    else if( IsSeries( type ) ){
      if( IsScalar(type.element) ){
        switch( type.element ){
          case BASE_TYPE_UCHAR: return "PackedByteArray";
          case BASE_TYPE_INT: return "PackedInt32Array";
          case BASE_TYPE_LONG: return "PackedInt64Array";
          case BASE_TYPE_FLOAT: return "PackedFloat32Array";
          case BASE_TYPE_DOUBLE: return "PackedFloat64Array";
          default:break;
        }
      } else if( IsString( type.VectorType() ) ){
        return "PackedStringArray";
      }
      return "Array";
    }
    else if( IsUnion( type ) ){
      return "Variant";
    }
    else{
      return "TODO";
    }
  }

  void GenComment(const std::vector<std::string> &dc, const char *prefix_ = "#") {
    std::string text;
    ::flatbuffers::GenComment(dc, &text, nullptr, prefix_);
    code_ += text + "\\";
  }

  void GenDefinitionDebug( const Definition *def ){
    code_ += "# Definition Base Class";

// std::string name;
    code_ += "#  name = " + def->name;

// std::string file;
    code_ += "#  file = " + def->file;

// std::vector<std::string> doc_comment;
    code_ += "#  doc_comment = ?";// + def->doc_comment;

// SymbolTable<Value> attributes;
    code_ += "#  attributes = ??";// + def->attributes;

// bool generated;  // did we already output code for this definition?
    code_ += "#  generated = \\";
    code_ += def->generated ? "true" : "false";

// Namespace *defined_namespace;  // Where it was defined.
    code_ += "#  defined_namespace = ";// + def->defined_namespace;

// // For use with Serialize()
// uoffset_t serialized_location;
    code_ += "#  serialized_location = " + NumToString(def->serialized_location);

// int index;  // Inside the vector it is stored.
    code_ += "#  index = " + NumToString(def->index);

// int refcount;
    code_ += "#  refcount = " + NumToString( def->refcount );

// const std::string *declaration_file;
    code_ += "#  declaration_file = " + def->name;

    code_ += "#  ---- ";
  }


  void GenFieldDebug( const FieldDef &field){
    code_.SetValue("FIELD_NAME", Name(field));
    code_ += "# GenFieldDebug for: '{{FIELD_NAME}}'";
    code_ += "#FieldDef {";

    //FieldDef is derived from Definition
    GenDefinitionDebug( &field );

//  bool deprecated;// Field is allowed to be present in old data, but can't be.
//                  // written in new data nor accessed in new code.
    code_ += "#  deprecated = \\";
    code_ += field.deprecated ? "true" : "false";

//    bool key;         // Field functions as a key for creating sorted vectors.
    code_ += "#  key = \\";
    code_ += field.key ? "true" : "false";

//    bool shared;  // Field will be using string pooling (i.e. CreateSharedString)
//                         // as default serialisation behaviour if field is a string.
    code_ += "#  shared = \\";
    code_ += field.shared ? "true" : "false";

//    bool native_inline;  // Field will be defined inline (instead of as a pointer)
//                         // for native tables if field is a struct.
    code_ += "#  native_inline = \\";
    code_ += field.native_inline ? "true" : "false";

//    bool flexbuffer;     // This field contains FlexBuffer data.
    code_ += "#  flexbuffer = \\";
    code_ += field.flexbuffer ? "true" : "false";

//    bool offset64;       // If the field uses 64-bit offsets.
    code_ += "#  offset64 = \\";
    code_ += field.offset64 ? "true" : "false";

    code_ += "#  IsScalar() = \\";
    code_ += field.IsScalar() ? "true" : "false";
    code_ += "#}";

    //    Value value;
    code_ += "#FieldDef.Value {";
    code_ += "#  constant: " + field.value.constant;
    code_ += "#  offset: " + NumToString(field.value.offset);
    code_ += "#}";

//  Type type;
    const auto &type = field.value.type;
    code_ += "#FieldDef.Value.Type {";
    //    BaseType base_type;
    code_ += "#  base_type: \\";
    code_ += TypeName(type.base_type);

//    BaseType element;       // only set if t == BASE_TYPE_VECTOR or
//                            // BASE_TYPE_VECTOR64
    code_ += "#  element: \\";
    code_ += TypeName(type.element);

//    StructDef *struct_def;  // only set if t or element == BASE_TYPE_STRUCT
    code_ += "#  struct_def: \\";
    code_ += type.struct_def ? "exists" : "<null>";
//    EnumDef *enum_def;      // set if t == BASE_TYPE_UNION / BASE_TYPE_UTYPE,
//                            // or for an integral type derived from an enum.
    code_ += "#  enum_def: \\";
    code_ += type.enum_def ? "exists" : "<null>";

//    uint16_t fixed_length;  // only set if t == BASE_TYPE_ARRAY
    code_ += "#  fixed_length: " + NumToString(type.fixed_length);
    if( type.base_type == BASE_TYPE_ARRAY ){
      code_ += "#  fixed_length is only set when base_type == BASE_TYPE_ARRAY";
    }

    code_ += "#  IsStruct() = \\";
    code_ += IsStruct(type) ? "true" : "false";
    code_ += "#  IsArray() = \\";
    code_ += IsArray(type) ? "true" : "false";
    code_ += "#  IsIncompleteStruct: \\";
    code_ += IsIncompleteStruct(type) ? "true" : "false";
    code_ += "#  IsUnion() = \\";
    code_ += IsUnion(type) ? "true" : "false";
    code_ += "#  IsUnionType() = \\";
    code_ += IsUnionType(type) ? "true" : "false";
    code_ += "#  IsSeries() = \\";
    code_ += IsSeries(type) ? "true" : "false";
    code_ += "#  IsVector() = \\";
    code_ += IsVector(type) ? "true" : "false";
    code_ += "#  IsVectorOfTable() = \\";
    code_ += IsVectorOfTable(type) ? "true" : "false";
    code_ += "#  IsVectorOfStruct: \\";
    code_ += IsVectorOfStruct(type) ? "true" : "false";
    code_ += "#  IsArray() = \\";
    code_ += IsArray(type) ? "true" : "false";
    code_ += "#  IsString() = \\";
    code_ += IsString(type) ? "true" : "false";
    code_ += "#  IsTable() = \\";
    code_ += IsTable(type) ? "true" : "false";
    code_ += "#  IsEnum() = \\";
    code_ += IsEnum(type) ? "true" : "false";

    code_ += "#}";

    if( IsTable(type) and field.value.type.struct_def ) {
      StructDef *struct_def = type.struct_def;

      code_ += "#FieldDef.Value.Type.StructDef {";

      //StructDef is derived from Definition
      GenDefinitionDebug( struct_def );

      // SymbolTable<FieldDef> fields;
      code_ += "#  fields = ? (SymbolTable<FieldDef>)";

      // bool fixed;       // If it's struct, not a table.
      code_ += "#  fixed = \\";
      code_ += struct_def->fixed ? "true" : "false";

      // bool predecl;     // If it's used before it was defined.
      code_ += "#  predecl = \\";
      code_ += struct_def->predecl ? "true" : "false";

      // bool sortbysize;  // Whether fields come in the declaration or size order.
      code_ += "#  sortbysize = \\";
      code_ += struct_def->sortbysize ? "true" : "false";

      // bool has_key;     // It has a key field.
      code_ += "#  has_key = \\";
      code_ += struct_def->has_key ? "true" : "false";

      // size_t minalign;  // What the whole object needs to be aligned to.
      code_ += "#  minalign = " + NumToString(struct_def->minalign );

      // size_t bytesize;  // Size if fixed.
      code_ += "#  bytesize = " + NumToString(struct_def->bytesize );

      // flatbuffers::unique_ptr<std::string> original_location;
      code_ += "#  original_location = \\";
      code_ += struct_def->original_location.get() ? *struct_def->original_location : "";

      // std::vector<voffset_t> reserved_ids;
      code_ += "#  reserved_ids = ? (std::vector<voffset_t>)";
    }

    if( IsEnum(type) ){
      const auto & enum_def = *type.enum_def;
      code_ += "#FieldDef.Value.EnumDef {";

      //StructDef is derived from Definition
      GenDefinitionDebug( &enum_def );

//      bool is_union;
      code_ += "#  is_union: \\";
      code_ += enum_def.is_union ? "true" : "false";
//      type is a union which uses type aliases where at least one type is
//        available under two different names.
//      bool uses_multiple_type_instances;
      code_ += "#  uses_multiple_type_instances: \\";
      code_ += enum_def.uses_multiple_type_instances ? "true" : "false";
      code_ += "#  MinValue() = " + NumToString( enum_def.MinValue() );
      code_ += "#  MaxValue() = " + NumToString( enum_def.MaxValue() );
//      Type underlying_type;
      code_ += "#}";
    }
  }

  std::string GenFieldOffsetName(const FieldDef &field) {
    std::string uname = Name(field);
    std::transform(uname.begin(), uname.end(), uname.begin(), CharToUpper);
    return "VT_" + uname;
  }

  // Generate an enum declaration
  void GenEnum(const EnumDef &enum_def) {
    code_.SetValue("ENUM_NAME", Name(enum_def));

    GenComment(enum_def.doc_comment);
    code_ += "enum " + Name(enum_def) + " {";
    code_.IncrementIdentLevel();

    code_.SetValue("SEP", ",");
    auto add_sep = false;
    for (const auto ev : enum_def.Vals()) {
      if (add_sep) code_ += "{{SEP}}";
      GenComment(ev->doc_comment );
      code_.SetValue("KEY",  ToUpper(Name(*ev)) );
      code_.SetValue("VALUE", enum_def.ToString(*ev) );
      code_ += "{{KEY}} = {{VALUE}}\\";
      add_sep = true;
    }
    code_.DecrementIdentLevel();
    code_ += "";
    code_ += "}\n";
  }

  void GenStruct( const StructDef &struct_def ){
    // Generate class to access the structs fields
    // The generated classes are like a view into a PackedByteArray,
    // it decodes the data on access.
    GenComment(struct_def.doc_comment);

    code_.SetValue("STRUCT_NAME", Name(struct_def));
    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    code_ += "static func Get{{STRUCT_NAME}}( _bytes : PackedByteArray, _start : int ):";
    code_.IncrementIdentLevel();
    code_ += "return {{STRUCT_NAME}}.new( _bytes, _start )";
    code_.DecrementIdentLevel();
    code_ += "";

    // Generate the class definition
    code_ += "class {{STRUCT_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();

    code_.SetValue("BYTES", NumToString(struct_def.bytesize));
    code_ += "func _init( bytes_ : PackedByteArray = [], start_ : int = 0) -> void:";
    code_.IncrementIdentLevel();
    code_ += "if bytes_.is_empty(): ";
    code_.IncrementIdentLevel();
    code_ += "var data = PackedByteArray()";
    code_ += "data.resize( {{BYTES}} )";
    code_ += "bytes = data";
    code_ += "start = 0";
    code_.DecrementIdentLevel();
    code_ += "else: bytes = bytes_; start = start_";
    code_.DecrementIdentLevel();
    code_ += "";

    // How do I set the data for the field? :
    for (const auto &field : struct_def.fields.vec) {
      if( field->deprecated ) continue; // Deprecated fields won't be accessible.

      const auto &type = field->value.type;
      code_.SetValue( "FIELD_NAME", Name(*field) );
      code_.SetValue( "OFFSET" , NumToString(field->value.offset) );
      code_.SetValue( "GODOT_TYPE", GetGodotType(type) );

      // Scalars
      if( field->IsScalar() ) {
        code_.SetValue("PBA", pba_types[ type.base_type ] );
        code_ += "# {{FIELD_NAME}} : {{GODOT_TYPE}}";
        code_ += "var {{FIELD_NAME}} : {{GODOT_TYPE}} :";
        code_.IncrementIdentLevel();
        code_ += "get(): return bytes.decode_{{PBA}}(start + {{OFFSET}})\\";
        code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
        code_ += "set( v ):";
        code_.IncrementIdentLevel();
        code_ += "var data = bytes";
        code_ += "data.encode_{{PBA}}(start + {{OFFSET}}, v )";
        code_ += "bytes = data";
        code_.DecrementIdentLevel();
        code_.DecrementIdentLevel();
        code_ += "";
      }
      // Structs
      else if( IsStruct( type) ){
        code_.SetValue("STRUCT_NAME", Name( struct_def ) );
        code_ += "func {{FIELD_NAME}}_set( value : {{GODOT_TYPE}} ):";
        code_.IncrementIdentLevel();
        code_ += "var parent.Get{{GODOT_TYPE}}(start + {{OFFSET}}, bytes)";
        code_ += "parent.Get{{GODOT_TYPE}}(start + {{OFFSET}}, bytes)";
        code_.DecrementIdentLevel();
        code_ += "";
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return parent.Get{{STRUCT_NAME}}(start + {{OFFSET}}, bytes)";
        code_.DecrementIdentLevel();
        code_ += "";
      }
      else {
        code_ += "#TODO - Unhandled Type";
        code_ += "pass";
        GenFieldDebug( *field );
      }
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenStaticFactory( const StructDef &struct_def ){
    // The root struct has a convenience that the start defaults to zero,
    // and is decoded if zero
    bool is_root = &struct_def == parser_.root_struct_def_;
    code_.SetValue( "DEFAULT", is_root ? "= 0" : "" );
    code_ += "static func Get{{TABLE_NAME}}( _bytes : PackedByteArray, _start : int {{DEFAULT}} ) -> {{TABLE_NAME}}:";
    code_.IncrementIdentLevel();
    code_ += "if _bytes.is_empty(): return null";
    if( is_root ) code_ += "if not _start: _start = _bytes.decode_u32(0)";
    code_ += "var new_{{TABLE_NAME}} = {{TABLE_NAME}}.new()";
    code_ += "new_{{TABLE_NAME}}.start = _start";
    code_ += "new_{{TABLE_NAME}}.bytes = _bytes";
    code_ += "return new_{{TABLE_NAME}}";
    code_ += "";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenVtableEnums( const StructDef &struct_def ){
    // Generate field id constants.
    if( not struct_def.fields.vec.empty() ){
      // We need to add a trailing comma to all elements except the last one as
      // older versions of gcc complain about this.
      code_.SetValue("SEP", "");
      code_ += "enum vtable{";
      code_.IncrementIdentLevel();
      bool sep = false;
      code_.SetValue("SEP", ",");
      for (const auto &field : struct_def.fields.vec) {
        if (field->deprecated) {
          // Deprecated fields won't be accessible.
          continue;
        }
        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
        code_.SetValue("OFFSET_VALUE", NumToString(field->value.offset));
        if( sep ) code_ += "{{SEP}}";
        code_ += "{{OFFSET_NAME}} = {{OFFSET_VALUE}}\\";
        sep = true;
      }
      code_.DecrementIdentLevel();
      code_ += ""; //end the line after the last element
      code_ += "}";
    }
    code_ += "";
  }

  void GenPresenceFunc(const FieldDef &field) {
    // Generate presence funcs
    code_.SetValue("FIELD_NAME", Name(field));

    if (field.IsRequired()) {
      // Required fields are always accessible.
      code_ += "# {{FIELD_NAME}} is required\n";
    }
    code_.SetValue("OFFSET_NAME", GenFieldOffsetName(field));
    code_ += "func {{FIELD_NAME}}_is_present() -> bool:";
    code_.IncrementIdentLevel();
    code_ += "return get_field_offset( vtable.{{OFFSET_NAME}} )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenAccessFunc( const FieldDef &field ) {
        const auto &type = field.value.type;
        code_.SetValue( "FIELD_NAME", Name( field ) );
        code_.SetValue( "GODOT_TYPE", GetGodotType( type ) );

        const bool can_include = (IsStruct(type) or IsTable(type)) and type.struct_def->file not_eq parser_.root_struct_def_->file;
        code_.SetValue( "INCLUDE", IsIncluded(type) ? GetInclude(type) : "" );

        if( not IsSeries( type ) ) {
          code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
          code_.IncrementIdentLevel();

          // Scalar
          if( field.IsScalar() ) {
            code_.SetValue( "PBA", pba_types[ type.base_type ] );
            code_ += "var foffset = get_field_offset( vtable.{{OFFSET_NAME}} )";
            code_ += "if not foffset: return " + field.value.constant + "\\";
            code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
            code_ += "return bytes.decode_{{PBA}}( start + foffset )\\";
            code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
          }
            // Struct
          else if( IsStruct( type ) ) {
            code_.SetValue( "INCLUDE",  GetInclude(type) );
            code_ += "var field_offset = get_field_offset( vtable.{{OFFSET_NAME}} )";
            if( IsBuiltin( type ) ) {
              code_ += "if not field_offset: return {{GODOT_TYPE}}()";
              code_ += "return decode_{{GODOT_TYPE}}( start + field_offset )";
            } else {
              code_ += "if not field_offset: return null";
              code_ += "return {{INCLUDE}}Get{{GODOT_TYPE}}( bytes, start + field_offset )";
            }
          }
            // Table
          else if( IsTable( type ) ) {
            code_.SetValue( "INCLUDE",  GetInclude(type) );
            code_ += "var field_start = get_field_start( vtable.{{OFFSET_NAME}} )";
            code_ += "if not field_start: return null";
            if( IsBuiltin( type ) ) {
              code_ += "return decode_{{GODOT_TYPE}}( field_start )";
            } else {
              code_ += "return {{INCLUDE}}Get{{GODOT_TYPE}}( bytes, field_start )";
            }
          }
            // Union
          else if( IsUnion( type ) ) {
            code_.SetValue( "INCLUDE",  GetInclude(type) );
            code_ += "var field_start = get_field_start( vtable.{{OFFSET_NAME}} )";
            code_ += "if not field_start: return null";
            // match the type
            code_ += "match( {{FIELD_NAME}}_type() ):";
            code_.IncrementIdentLevel();
            code_.SetValue( "ENUM_TYPE", type.enum_def->name );
            for( const auto &val: type.enum_def->Vals() ) {
              if( val->IsZero() )continue;
              code_.SetValue( "ENUM_VALUE", ToUpper( val->name ) );
              code_.SetValue( "GODOT_TYPE", GetGodotType( val->union_type ) );
              code_ += "{{ENUM_TYPE}}.{{ENUM_VALUE}}:";
              code_.IncrementIdentLevel();
              if( IsBuiltin( type ) ) {
                code_ += "return decode_{{GODOT_TYPE}}( field_start )";
              } else {
                code_ += "return {{INCLUDE}}Get{{GODOT_TYPE}}( bytes, field_start )";
              }
              code_.DecrementIdentLevel();
            }
            code_ += "_: pass";
            code_.DecrementIdentLevel();
            code_ += "return null";
          }
            // String
          else if( IsString( type ) ) {
            code_ += "var field_start = get_field_start( vtable.{{OFFSET_NAME}} )";
            code_ += "if not field_start: return ''";
            code_ += "return decode_String( field_start )";
          }
          code_.DecrementIdentLevel();
          code_ += "";
          return;
        }

        // Vector of
        if( IsSeries( type ) ){
          // func {{FIELD_NAME}}_size() -> int
          code_ += "func {{FIELD_NAME}}_size() -> int:";
          code_.IncrementIdentLevel();
          code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
          code_ += "if not array_start: return 0";
          code_ += "return bytes.decode_u32( array_start )";
          code_.DecrementIdentLevel();
          code_ += "";

          // func {{FIELD_NAME}}() -> Array|PackedArray
          code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
          code_ += "if not array_start: return []";
          code_ += "var array_size = bytes.decode_u32( array_start )";
          code_ += "array_start += 4";

          Type element = type.VectorType();
          code_.SetValue("ELEMENT_TYPE", GetGodotType( element ) );
          code_.SetValue("ELEMENT_SIZE", element_size[ element.base_type ] );
          code_.SetValue("PBA", pba_types[ element.base_type ] );
          // Scalar
          if( IsScalar(element.base_type) ){
            switch( element.base_type ) {
              case BASE_TYPE_UTYPE:
              case BASE_TYPE_BOOL:
              case BASE_TYPE_UCHAR:
                code_ += "return bytes.slice( array_start, array_start + array_size )";
                code_.DecrementIdentLevel();
                code_ += "";
                //func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:
                code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
                code_.IncrementIdentLevel();
                code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
                code_ += "if not array_start: return 0";
                code_ += "array_start += 4";
                code_ += "return bytes[array_start + index]";
                code_.DecrementIdentLevel();
                code_ += "";
                return;
              case BASE_TYPE_CHAR:
              case BASE_TYPE_SHORT:
              case BASE_TYPE_USHORT:
              case BASE_TYPE_UINT:
              case BASE_TYPE_ULONG:
                code_ += "var array = []";
                code_ += "array.resize( array_size )";
                code_ += "for i in array_size:";
                code_.IncrementIdentLevel();
                code_ += "array[i] = bytes.decode_{{PBA}}( array_start + i * {{ELEMENT_SIZE}})";
                code_.DecrementIdentLevel();
                code_ += "return array";
                code_.DecrementIdentLevel();
                code_ += "";
                //func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:
                code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
                code_.IncrementIdentLevel();
                code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
                code_ += "if not array_start: return 0";
                code_ += "array_start += 4";
                code_ += "return bytes.decode_{{PBA}}( array_start + index * {{ELEMENT_SIZE}})";
                code_.DecrementIdentLevel();
                code_ += "";
                return;
              case BASE_TYPE_INT:
              case BASE_TYPE_LONG:
              case BASE_TYPE_FLOAT:
              case BASE_TYPE_DOUBLE:
                code_.SetValue("TO_PACKED_FUNC", array_conversion[ element.base_type ] );
                code_ += "var array_end = array_start + array_size * {{ELEMENT_SIZE}}";
                code_ += "return bytes.slice( array_start, array_end ).{{TO_PACKED_FUNC}}";
                code_.DecrementIdentLevel();
                code_ += "";
                //func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:
                code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
                code_.IncrementIdentLevel();
                code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
                code_ += "if not array_start: return 0";
                code_ += "array_start += 4";
                code_ += "return bytes.decode_{{PBA}}( array_start + index * {{ELEMENT_SIZE}})";
                code_.DecrementIdentLevel();
                code_ += "";
                return;
              default:
                // We shouldn't be here.
                GenFieldDebug( field );
            }
          }
          //TODO - Struct
          else if( IsStruct(element ) ){
            code_.SetValue( "INCLUDE",  GetInclude(element) );
            //TODO {{FIELD_NAME}}():
            //TODO {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:
            code_ += "# TODO Vector of Structs";
            code_ += "return []";
            code_.DecrementIdentLevel();
            code_ += "";
            return;
          }
          // Table
          else if( IsTable(element ) ){
            code_.SetValue( "INCLUDE",  GetInclude(element) );
            code_.SetValue("ELEMENT_TYPE", GetGodotType( element ) );
            code_ += "var array : Array; array.resize( array_size )";
            code_ += "for i in array_size:";
            code_.IncrementIdentLevel();
            code_ += "var pos = array_start + i * 4";
            if( IsBuiltin( element ) ) {
              code_ += "array[i] = decode_{{ELEMENT_TYPE}}( pos + bytes.decode_u32( pos ) )";
            } else {
              code_ += "array[i] = {{INCLUDE}}Get{{ELEMENT_TYPE}}( bytes, pos + bytes.decode_u32( pos ) )";
            }

            code_.DecrementIdentLevel();
            code_ += "return array";
            code_.DecrementIdentLevel();
            code_ += "";

            //TODO {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:
            return;
          }
          // String
          else if( IsString(element ) ){
            code_ += "var array : {{GODOT_TYPE}}";
            code_ += "array.resize( array_size )";
            code_ += "for i in array_size:";
            code_.IncrementIdentLevel();
            code_ += "var idx = array_start + i * {{ELEMENT_SIZE}}";
            code_ += "var element_start = idx + bytes.decode_u32( idx )";
            code_ += "array[i] = decode_String( element_start )";
            code_.DecrementIdentLevel();
            code_ += "return array";
            code_.DecrementIdentLevel();
            code_ += "";

            //func {{FIELD_NAME}}_at( index ) -> {{ELEMENT_TYPE}}:
            code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
            code_.IncrementIdentLevel();
            code_ += "var array_start = get_field_start( vtable.{{OFFSET_NAME}} )";
            code_ += "if not array_start: return ''";
            code_ += "array_start += 4";
            code_ += "var string_start = array_start + index * {{ELEMENT_SIZE}}";
            code_ += "string_start += bytes.decode_u32( string_start )";
            code_ += "return decode_String( string_start )";
            code_.DecrementIdentLevel();
            code_ += "";
            return;
          }

          //TODO - Vector
          else if( IsVector(element ) ) {
            // FIXME Wouldn't this be weird? I have to double check it.
          }
          return;
        } // End of IsSeries( type )
        //TODO Vector of Union
        //TODO Fixed length Array
        //TODO Dictionary
        else {
          code_ += " #TODO - Unhandled Type";
          GenFieldDebug( field );
        }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenDebugDict( const StructDef &struct_def ){
    // There are only two options on how we got here
    // Either we are a table, or we are a struct.
    // Generate Pretty Printer
    code_ += "func debug() -> Dictionary:";
    code_.IncrementIdentLevel();
    code_ += "var d : Dictionary = {}";
    code_ += "d['buffer_size'] = bytes.size()";
    code_ += "d['start'] = start";

    // IF we are a table, then we have a vtable.
    if( struct_def.fixed ) {  // Then we are a struct
      code_ += "d[{{FIELD_NAME}}] = {{FIELD_NAME}}()";
    } else {
      code_ += "d['vtable_offset'] = bytes.decode_s32( start )";
      code_ += "d['vtable_start'] = d.start - d.vtable_offset";
      code_ += "d['vtable'] = Dictionary()";
      code_ += "d.vtable['vtable_bytes'] = bytes.decode_u16( d.vtable_start )";
      code_ += "d.vtable['table_size'] = bytes.decode_u16( d.vtable_start + 2 )";
      code_ += "";
      code_ += "for i in ((d.vtable.vtable_bytes / 2) - 2):";
      code_ += "\tvar keys = vtable.keys()";
      code_ += "\tvar offsets = vtable.values()";
      code_ += "\td.vtable[keys[i]] = bytes.decode_u16( d.vtable_start + offsets[i] )";
      code_ += "";
    }

    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      Type field_type = field->value.type;
      Type element_type;
      code_.SetValue( "FIELD_NAME", Name( *field ) );
      code_.SetValue( "FIELD_TYPE", GetGodotType(field_type ) );
      if( IsSeries(field_type ) ){
        element_type = field_type.VectorType();
        code_.SetValue( "ELEMENT_TYPE", TypeName( field_type.element ) );
      }

      code_.SetValue("OFFSET_NAME", GenFieldOffsetName( *field ) );

      code_ += "# {{FIELD_NAME}}:{{FIELD_TYPE}} \\";
      code_ += field->IsRequired() ? "(required)" : "";

      // Field Types:
      code_.SetValue("DICT", Name( *field ) + "_dict" );
      code_ += "var {{DICT}} = {'type':'{{FIELD_TYPE}}'}";
      code_ += "{{DICT}}['offset'] = get_field_offset( vtable.{{OFFSET_NAME}} )";


      if( not struct_def.fixed ){ // If we are a table, then all fields are optional.
        code_ += "if {{FIELD_NAME}}_is_present():";
        code_.IncrementIdentLevel();
      }

      // Scalar
      if( IsScalar( field_type.base_type ) ){
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Struct
      else if( IsStruct(field_type) ){
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Table
      else if( IsTable(field_type ) ){
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}().debug()";
      }
      // TODO * Union
      else if( IsUnion(field_type ) ){
        code_ += "pass # TODO Union";
      }
      // String
      else if( IsString(field_type ) ){
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Vector of
      else if( IsVector( field_type) ) {
        code_ += "{{DICT}}['type'] = '{{FIELD_TYPE}} of {{ELEMENT_TYPE}}'";
        code_ += "{{DICT}}['start'] = get_field_start( vtable.{{OFFSET_NAME}} )";
        code_ +=
            "{{DICT}}['size'] = bytes.decode_u32( get_field_start( vtable.{{OFFSET_NAME}} ) )";
        // Scalar
        if (IsScalar(field_type.element)) {
          code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
        }
        // Struct | Table
        else if( IsStruct(element_type) or IsTable(element_type) ) {
          code_ += "{{DICT}}['value'] = {{FIELD_NAME}}().map(";
          code_ += "\tfunc( element ): return element.debug() if element else null";
          code_ += ")";
        }
        // String
        if( IsString(element_type) ){
          code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
        }
        // TODO     - Vector
      }

      // TODO * Vector of Union
      // TODO * Fixed length Array

      if( not struct_def.fixed ){ // Come back down after : if {{FIELD_NAME}}_is_present():
        code_.DecrementIdentLevel();
        code_ += "";
      }
      code_ += "d['{{FIELD_NAME}}'] = {{DICT}}";
    }
    code_ += "return d ";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  // Generate an accessor struct
  void GenTable( const StructDef &struct_def ) {
    // Generate classes to access the table fields
    // The generated classes are a view into a PackedByteArray,
    // will decode the data on access.

    GenComment(struct_def.doc_comment );

    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    // assumes that TABLE_NAME was set previously
    code_.SetValue("TABLE_NAME", Name(struct_def ) );

    GenStaticFactory( struct_def );

    { // generate Flatbuffer derived class
      code_ += "class {{TABLE_NAME}} extends FlatBuffer:";
      code_.IncrementIdentLevel();
      code_ += "static var parent : GDScript";
      code_ += "";

      GenVtableEnums(struct_def);

      code_ += "func _init() -> void:";
      code_.IncrementIdentLevel();
      code_ += "if not parent:";
      code_ += "\tparent = load( '{{FILE_PATH}}' )";
      code_.DecrementIdentLevel();
      code_ += "";

      // Generate the accessors.
      for (const FieldDef *field : struct_def.fields.vec) {
        code_.SetValue("FIELD_NAME", Name(*field));
        if (field->deprecated) {
          code_ += "# field:'{{FIELD_NAME}}' is deprecated\n";
          // Deprecated fields won't be accessible.
          continue;
        }
        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
        GenPresenceFunc(*field);
        GenAccessFunc(*field);
      }

      GenDebugDict(struct_def);

      code_.DecrementIdentLevel();
      code_ += "";
    }

    GenBuilders(struct_def);

    GenCreateFunc( struct_def );
    GenCreateFunc2( struct_def );

  }

  void GenBuilders(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def ) );

    // Generate a builder struct:
    code_ += "class {{STRUCT_NAME}}Builder extends RefCounted:";
    code_.IncrementIdentLevel();
    code_ += "var fbb_: FlatBufferBuilder";
    code_ += "var start_ : int";
    code_ += "";

    // Add init function
    code_ += "func _init( _fbb : FlatBufferBuilder ):";
    code_.IncrementIdentLevel();
    code_ += "fbb_ = _fbb";
    code_ += "start_ = _fbb.start_table()";
    code_.DecrementIdentLevel();
    code_ += "";

    for( const auto &field : struct_def.fields.vec ){
      if( field->deprecated ) continue;

      // Generate add functions of the forms:
      // func add_{{FIELD_NAME}}( {{FIELD_NAME}} : {{GODOT_TYPE}} ) -> void:
      //   fbb_.add_element_{{GODOT_TYPE}}_default( {{FIELD_OFFSET}}, {{FIELD_NAME}}, {{VALUE_DEFAULT}});

      // func add_{{FIELD_NAME}}( {{FIELD_NAME}}_offset : int ) -> void:
      //   fbb_.add_offset( {{FIELD_OFFSET}, {{FIELD_NAME}} )

      const auto type = field->value.type;
      const bool is_inline = field->IsScalar() or IsStruct( type ) or IsArray( type );
      const bool is_default_scalar = is_inline and not field->IsScalarOptional();

      code_.SetValue("FIELD_NAME", Name( *field ) );
      code_.SetValue("INCLUDE", "" );
      code_.SetValue("FIELD_OFFSET", GenFieldOffsetName( *field ) );
      code_.SetValue("PARAM_NAME", Name( *field ) + (is_inline ? "": "_offset") );
      code_.SetValue("INCLUDE", IsStruct( type ) ? include_map[type.struct_def->file] : "" );
      code_.SetValue("PARAM_TYPE", is_inline ? GetGodotType( type ) : "int" );
      code_.SetValue("VALUE_DEFAULT", is_default_scalar ? field->value.constant : "" );

      // Function Signature
      code_ += "func add_{{FIELD_NAME}}( {{PARAM_NAME}} : {{INCLUDE}}{{PARAM_TYPE}} ) -> void:";
      code_.IncrementIdentLevel();

      // Scalar
      if( field->IsScalar() ){
        code_.SetValue("TYPE", add_element_func[type.base_type] );
        code_ += "fbb_.{{TYPE}}_default( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}}, {{VALUE_DEFAULT}} )";
      }
      // Struct
      else if( IsStruct( type ) ) {
        if( IsBuiltin( type ) ){
          code_ += "fbb_.add_{{PARAM_TYPE}}( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
        } else {
          code_ += "fbb_.add_bytes( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}}.bytes ) ";
        }
      }
      // Table
      else if( IsTable( type ) ) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      // Union
      else if( IsUnion( type ) ) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      // String
      else if( IsString( type ) ) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      // TODO Vector of
      else if( IsVector( type ) ) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
        // TODO - Scalar
        // TODO - Struct
        // TODO - Table
        // TODO - String
        // TODO - Vector
      }
      // TODO Vector of Union
      // TODO Fixed length Array
      // TODO Dictionary
      else {
        code_ += "# FIXME Unknown Type";
        code_ += "pass";
        GenFieldDebug( *field );
      }

      code_.DecrementIdentLevel();
      code_ += "";
    }

    // var finish(): -> void
    // ---------------------
    code_ += "func finish() -> int:";
    code_.IncrementIdentLevel();
    code_ += "var end = fbb_.end_table( start_ )";
    code_ += "var o = end";

    for (const auto &field : struct_def.fields.vec) {
      if( not field->deprecated and field->IsRequired() ){
        code_.SetValue("FIELD_NAME", Name(*field));
        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
        code_ += "fbb_.Required(o, {{STRUCT_NAME}}.vtable.{{OFFSET_NAME}});";
      }
    }
    code_ += "return o;";
    code_.DecrementIdentLevel();
    code_ += "";

    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenCreateFunc( const StructDef &struct_def ){
    // Generate a convenient CreateX function that uses the above builder
    // to create a table in one go.

    code_ += "static func Create{{STRUCT_NAME}}( _fbb : FlatBufferBuilder,";
    code_.IncrementIdentLevel();
    code_.IncrementIdentLevel();
    code_.SetValue("SEP", "," );
    bool add_sep = false;
    for (const auto &field : struct_def.fields.vec) {
      if( field->deprecated ) continue;
      const auto type = field->value.type;
      if( add_sep ) code_ += "{{SEP}}";
      code_.SetValue("PARAM_NAME", Name(*field) );
      code_.SetValue("DEFAULT_VALUE", "default" );
      // Scalar | Struct | Fixed length Array
      // These items are added inline in the table, and do not require creating an offset ahead of time.

      code_.SetValue("INCLUDE", "" );
      code_.SetValue("PARAM_TYPE", GetGodotType( type ) );
      if( IsScalar(type.base_type) ){ /* no changes */
      } else if( IsStruct( type ) ){
        code_.SetValue("INCLUDE", IsIncluded(type) ? GetInclude(type) : "" );
      } else {
        code_.SetValue("PARAM_TYPE", "int" );
      }
      code_ += "{{PARAM_NAME}} : {{INCLUDE}}{{PARAM_TYPE}}\\";
      //TODO add default value if possible.
      add_sep = true;
    }
    code_ += " ) -> int :";
    code_.DecrementIdentLevel();

//    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
//      for( auto it = struct_def.fields.vec.rbegin(); it not_eq struct_def.fields.vec.rend(); ++it ) {
//        const auto &field = **it;
//        if( not field.deprecated and ( not struct_def.sortbysize or size == SizeOf( field.value.type.base_type )) ) {
//          if( not IsStruct( field.value.type ) ) continue;
//          // FIXME this might also include fixed sized arrays
//          code_.SetValue( "FIELD_NAME", Name( field ) );
//          code_ += "# Create {{FIELD_NAME}}";
//          code_ += "var {{FIELD_NAME}}_offset";
//        }
//      }
//    }

    // Create* function body
    code_ += "var builder = {{STRUCT_NAME}}Builder.new( _fbb );";
    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
      for( auto it = struct_def.fields.vec.rbegin(); it not_eq struct_def.fields.vec.rend(); ++it ) {
        const auto &field = **it;
        if( not field.deprecated and ( not struct_def.sortbysize or size == SizeOf(field.value.type.base_type) ) ){
          code_.SetValue("FIELD_NAME", Name( field ) );
          code_ += "builder.add_{{FIELD_NAME}}( {{FIELD_NAME}} );";
        }
      }
    }
    code_ += "return builder.finish();";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenCreateFunc2( const StructDef &struct_def ){
    // Generate a convenient CreateX function that uses the above builder
    // to create a table in one go.

    code_ += "static func Create{{STRUCT_NAME}}2( _fbb : FlatBufferBuilder, object : Variant ) -> int:";
    code_.IncrementIdentLevel();

    // All the non-inline objects need to be added to the builder before adding our object
    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
      for( auto it = struct_def.fields.vec.rbegin(); it not_eq struct_def.fields.vec.rend(); ++it ) {
        const auto &field = **it;
        if( not field.deprecated and ( not struct_def.sortbysize or size == SizeOf(field.value.type.base_type) ) ){

          Type field_type = field.value.type;
          Type element_type;
          code_.SetValue("FIELD_NAME", Name( field ) );
          code_.SetValue("INCLUDE", "" );

          // Scalar | Struct | Fixed length Array
          // These items are added inline in the table, and do not require creating an offset ahead of time.
          if( IsScalar(field_type.base_type) or IsStruct( field_type ) ) continue;

          if( IsTable( field_type ) ){
            code_.SetValue("FIELD_TYPE", GetGodotType(field_type ) );
            code_.SetValue("INCLUDE", include_map[field_type.struct_def->file] );
          }
          if( IsSeries( field_type) ){
            code_.SetValue("FIELD_TYPE", "Vector" );
            element_type = field_type.VectorType();
            code_.SetValue("ELEMENT_TYPE", TypeName(element_type.base_type) );
            if( IsTable(element_type) ){
              code_.SetValue("INCLUDE", include_map[element_type.struct_def->file] );
              code_.SetValue("ELEMENT_TYPE", GetGodotType( element_type ) );
            }
          }

          code_ += "# {{FIELD_NAME}} : {{FIELD_TYPE}} \\";
          code_ += IsSeries(field_type ) ? "of {{ELEMENT_TYPE}}" : "";

          // Scalar | Struct | Fixed length Array
          // These items are excluded from this step already
          //Table
          if( IsTable( field_type ) ){
            code_ += "var {{FIELD_NAME}}_offset : int = {{INCLUDE}}Create{{FIELD_TYPE}}2( _fbb, object.{{FIELD_NAME}} );";
          }
          //TODO Union
          else if( IsUnion( field_type ) ) {
            code_ += "# TODO Union";
            code_ += "var {{FIELD_NAME}}_offset : int";
          }
          //String
          else if( IsString( field_type ) ) {
            code_ += "var {{FIELD_NAME}}_offset : int = _fbb.create_String( object.{{FIELD_NAME}} );";
          }
          // Vector of
          else if( IsVector( field_type ) &! IsUnion(element_type) ){
            //Scalar
            if( IsScalar( element_type.base_type ) ){
              code_.SetValue("CREATE_FUNC", vector_create_func[element_type.base_type] );
              code_ += "var {{FIELD_NAME}}_offset : int = _fbb.{{CREATE_FUNC}}( object.{{FIELD_NAME}} )";
            }
            //TODO - Struct
            else if( IsStruct( element_type ) ) {
              code_ += "# TODO Vector of Struct";
              code_ += "var {{FIELD_NAME}}_offset : int";
            }
            //Table
            else if( IsTable( element_type ) ) {
              code_ += "var {{FIELD_NAME}}_array : Array = object.{{FIELD_NAME}}";
              code_ += "var {{FIELD_NAME}}_offsets : PackedInt32Array";
              code_ += "{{FIELD_NAME}}_offsets.resize( {{FIELD_NAME}}_array.size() )";
              code_ += "for index in {{FIELD_NAME}}_array.size():";
              code_.IncrementIdentLevel();
              code_ += "var item = {{FIELD_NAME}}_array[index]";
              code_ += "{{FIELD_NAME}}_offsets[index] = {{INCLUDE}}Create{{ELEMENT_TYPE}}2( _fbb, item )";
              code_.DecrementIdentLevel();
              code_ += "var {{FIELD_NAME}}_offset : int = _fbb.create_vector_offset( {{FIELD_NAME}}_offsets )";
            }
            // String
            else if( IsString( element_type ) ){
              code_.SetValue("CREATE_FUNC", vector_create_func[element_type.base_type] );
              code_ += "var {{FIELD_NAME}}_offset : int = _fbb.{{CREATE_FUNC}}( object.{{FIELD_NAME}} )";
            }
            //TODO - Vector
            else if( IsVector( element_type ) ){
              code_ += "# TODO Vector of Vector";
            }
            else {
              code_ += "# TODO Vector of Unknown Type";
              GenFieldDebug( field );
            }
          }
          //TODO Vector of Union
          else{
            GenFieldDebug( field );
          }
          code_ += "";
        }
      }
    }

    // Create* function body
    code_ += "# build the {{STRUCT_NAME}}";
    code_ += "var builder = {{STRUCT_NAME}}Builder.new( _fbb )";
    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
      for( auto it = struct_def.fields.vec.rbegin(); it not_eq struct_def.fields.vec.rend(); ++it ) {
        const auto &field = **it;
        if( not field.deprecated and ( not struct_def.sortbysize or size == SizeOf(field.value.type.base_type) ) ){

          code_.SetValue("FIELD_NAME", Name( field ) );
          Type field_type = field.value.type;

          // Scalar | Struct | Fixed length Array
          // These items are added inline in the table, and do not require creating an offset ahead of time.
          if( IsScalar(field_type.base_type) or IsStruct( field_type ) ){
            code_ += "builder.add_{{FIELD_NAME}}( object.{{FIELD_NAME}} )";
          } else {
            code_ += "builder.add_{{FIELD_NAME}}( {{FIELD_NAME}}_offset )";
          }
        }
      }
    }
    code_ += "return builder.finish();";
    code_.DecrementIdentLevel();
    code_ += "";
  }

};

}  // namespace gdscript

static bool GenerateGDScript(const Parser &parser, const std::string &path,
                 const std::string &file_name) {
  gdscript::IDLOptionsGdscript opts(parser.opts);
  gdscript::GdscriptGenerator generator(parser, path, file_name, opts);
  return generator.generate();
}

static std::string GDScriptMakeRule(const Parser &parser, const std::string &path,
                               const std::string &file_name) {
  const auto file_base = StripPath(StripExtension(file_name));
  gdscript::GdscriptGenerator generator(parser, path, file_name, parser.opts);
  const auto included_files = parser.GetIncludedFilesRecursive(file_name);
  std::string make_rule =
      generator.GeneratedFileName(path, file_base, parser.opts) + ": ";
  for (const std::string &included_file : included_files) {
    make_rule += " " + included_file;
  }
  return make_rule;
}

namespace {

class GdscriptCodeGenerator : public CodeGenerator {
 public:
  Status GenerateCode(const Parser &parser, const std::string &path,
                      const std::string &filename) override {
    if(  not GenerateGDScript(parser, path, filename) ){ return Status::ERROR; }
    return Status::OK;
  }

  // Generate code from the provided `buffer` of given `length`. The buffer is a
  // serialised reflection.fbs.
  Status GenerateCode(const uint8_t *buffer, int64_t length, [[maybe_unused]]const CodeGenOptions &options) override {
    (void)buffer;
    (void)length;
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateMakeRule(const Parser &parser, const std::string &path,
                          const std::string &filename,
                          std::string &output) override {
    output = GDScriptMakeRule(parser, path, filename);
    return Status::OK;
  }

  Status GenerateGrpcCode([[maybe_unused]]const Parser &parser, [[maybe_unused]]const std::string &path,
                          [[maybe_unused]]const std::string &filename) override {
    //FIXME if ( not GenerateGdscriptGRPC(parser, path, filename)) { return Status::ERROR; }
    return Status::OK;
  }

  Status GenerateRootFile(const Parser &parser,
                          const std::string &path) override {
    (void)parser;
    (void)path;
    return Status::NOT_IMPLEMENTED;
  }

  [[nodiscard]] bool IsSchemaOnly() const override { return true; }

  [[nodiscard]] bool SupportsBfbsGeneration() const override { return false; }

  [[nodiscard]] bool SupportsRootFileGeneration() const override { return false; }

  [[nodiscard]] IDLOptions::Language Language() const override { return IDLOptions::kCpp; }

  [[nodiscard]] std::string LanguageName() const override { return "GDScript"; }
};

}  // namespace

std::unique_ptr<CodeGenerator> NewGDScriptCodeGenerator() {
  return std::make_unique<GdscriptCodeGenerator>();
}

}  // namespace flatbuffers
