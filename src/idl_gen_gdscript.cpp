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

// Make the string upper case
static inline std::string ToLower(std::string val) {
  std::locale loc;
  auto &facet = std::use_facet<std::ctype<char>>(loc);
  facet.tolower(&val[0], &val[0] + val.length());
  return val;
}

namespace gdscript {

enum GodotArrayType{
  GODOT_ARRAY_NONE,
  GODOT_ARRAY_BOOL,
  GODOT_ARRAY_BYTE,
  GODOT_ARRAY_SCALAR,
  GODOT_ARRAY_PACKED,
  GODOT_ARRAY_STRING,
  GODOT_ARRAY_STRUCT,
  GODOT_ARRAY_TABLE,
  GODOT_ARRAY_UNION,
};

const char *vector_create_func[] = {
  "",               // 0 NONE
  "create_Vector_uint8",      // 1 UTYPE
  "",      // 2 BOOL
  "create_Vector_int8",       // 3 CHAR
  "create_Vector_uint8",      // 4 UCHAR
  "create_Vector_int16",      // 5 SHORT
  "create_Vector_uint16",     // 6 USHORT
  "create_Vector_int32",      // 7 INT
  "create_Vector_uint32",     // 8 UINT
  "create_Vector_int64",      // 9 LONG
  "create_Vector_uint64",     //10 ULONG
  "create_Vector_float32",    //11 FLOAT
  "create_Vector_float64",    //12 DOUBLE
  "create_PackedStringArray",            //13 STRING
  "",      //14 VECTOR
  "",      //15 STRUCT
  "",      //16 UNION
  "",      //17 ARRAY
  "",      //18 VECTOR64
};

const char *decode_funcs[] = {
"",               // 0 NONE
"decode_u8",      // 1 UTYPE
"decode_u8",      // 2 BOOL
"decode_s8",      // 3 CHAR
"decode_u8",      // 4 UCHAR
"decode_s16",     // 5 SHORT
"decode_u16",     // 6 USHORT
"decode_s32",     // 7 INT
"decode_u32",     // 8 UINT
"decode_s64",     // 9 LONG
"decode_u64",     //10 ULONG
"decode_float",   //11 FLOAT
"decode_double",  //12 DOUBLE
"decode_String",  //13 STRING
"",               //14 VECTOR
"",               //15 STRUCT
"",               //16 UNION
"",               //17 ARRAY
"",               //18 VECTOR64
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
  "", //15 STRUCT
  "", //16 UNION
  "", //17 ARRAY
  "", //18 VECTOR64
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

    static const char *const builtin_types_[] = {
      "Vector3",
      "Vector3i",
      "Color",
      nullptr
    };
    for (auto kw = builtin_types_; *kw; kw++) builtin_types.insert(*kw);

    static const char *const keywords[] = {
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
      nullptr,
    };
    for (auto kw = keywords; *kw; kw++) keywords_.insert(*kw);
  }

  // Iterate through all definitions we haven't. Generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() override {
    code_.Clear();
    code_ += "# " + std::string(FlatBuffersGeneratedWarning() );
    code_.SetValue( "FILE_NAME", file_name_ );
    keywords_.insert( file_name_ );

    code_ += "class_name {{FILE_NAME}}";
    code_ += "";

    // convenience function to get the root table without having to pass its position
    code_.SetValue("ROOT_STRUCT", EscapeKeyword( parser_.root_struct_def_->name ) );
    code_ += "static func GetRoot( data : PackedByteArray ) -> {{ROOT_STRUCT}}:";
    code_.IncrementIdentLevel();
    code_ += "return Get{{ROOT_STRUCT}}( data.decode_u32(0), data )";
    code_.DecrementIdentLevel();
    code_ += "";


    // Generate code for all the enum declarations.
    for (const auto &enum_def : parser_.enums_.vec) {
      if (!enum_def->generated) {
        GenEnum(*enum_def);
      }
    }

    // Generate code for all structs, then all tables.
    for (const auto &struct_def : parser_.structs_.vec) {
      if (struct_def->fixed && !struct_def->generated) {
        GenStruct(*struct_def);
      }
    }

    for (const auto &struct_def : parser_.structs_.vec) {
      if (!struct_def->fixed && !struct_def->generated) {
        GenTable(*struct_def);
      }
    }

    const auto file_path = GeneratedFileName(path_, file_name_, opts_);
    const auto final_code = code_.ToString();

    // Save the file
    return SaveFile(file_path.c_str(), final_code, false);
  }

 private:
  CodeWriter code_;

  std::unordered_set<std::string> keywords_;
  std::unordered_set<std::string> builtin_types;

  const IDLOptionsGdscript opts_;

  std::string EscapeKeyword(const std::string &name) const {
    return keywords_.find(name) == keywords_.end() ? name : name + "_";
  }

  bool IsBuiltin( const Type &type ){
    if( type.struct_def ) {
      return builtin_types.find(type.struct_def->name) != builtin_types.end();
    }
    return false;
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
    else if( IsStruct( type ) || IsTable( type ) ){
      return EscapeKeyword( type.struct_def->name );
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

  void GenFieldDebug( const FieldDef &field){
    code_.SetValue("FIELD_NAME", Name(field));
    code_ += "# GenFieldDebug for: '{{FIELD_NAME}}'";
    code_ += "#FieldDef {";

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


    if( IsEnum(type) ){
      code_ += "#FieldDef.Value.EnumDef {";
      const auto & enum_def = *type.enum_def;
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
    code_ += "static func Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ):";
    code_.IncrementIdentLevel();
    code_ += "var new_{{STRUCT_NAME}} = {{STRUCT_NAME}}.new()";
    code_ += "new_{{STRUCT_NAME}}.start = _start";
    code_ += "new_{{STRUCT_NAME}}.bytes = _bytes";
    code_ += "return new_{{STRUCT_NAME}}";
    code_.DecrementIdentLevel();
    code_ += "";

    // Generate the class definition

    code_ += "class {{STRUCT_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();
    code_ += "# struct";

    // Generate the accessor functions, in the form:
    // func name() -> type :
    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue( "FIELD_NAME", Name(*field) );

      const auto &type = field->value.type;
      code_.SetValue( "OFFSET" , NumToString(field->value.offset) );
      code_.SetValue( "GODOT_TYPE", GetGodotType(type) );

      if( field->IsScalar() ){

        code_.SetValue("DECODE_FUNC", decode_funcs[ type.base_type] );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return bytes.{{DECODE_FUNC}}(start + {{OFFSET}})\\";
        code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
        code_.DecrementIdentLevel();
      }
      else if( IsStruct( type) ){
        code_.SetValue("STRUCT_NAME", Name( struct_def ) );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return {{FILE_NAME}}.Get{{STRUCT_NAME}}(start + {{OFFSET}}, bytes)";
        code_.DecrementIdentLevel();
      }
      else {
        code_ += " #TODO - Unhandled Type";
        GenFieldDebug( *field );
      }
      code_ += "";
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenStaticFactory(){
    code_ += "static func Get{{TABLE_NAME}}( _start : int, _bytes : PackedByteArray ) -> {{TABLE_NAME}}:";
    code_.IncrementIdentLevel();
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
    if (!struct_def.fields.vec.empty()) {
      // We need to add a trailing comma to all elements except the last one as
      // older versions of gcc complain about this.
      code_.SetValue("SEP", "");
      code_ += "enum {";
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
    code_ += "return get_field_offset( {{OFFSET_NAME}} )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenAccessFunc(const FieldDef &field) {
        const auto &type = field.value.type;
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_.SetValue("DECODE_FUNC", decode_funcs[ type.base_type] );

        if( field.IsScalar() ){
          code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return " + field.value.constant + "\\";
          code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
          code_ += "return bytes.{{DECODE_FUNC}}( start + foffset )\\";
          code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
          code_.DecrementIdentLevel();
        }
        else if( IsString( type ) ){
          code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return {{GODOT_TYPE}}()";
          code_ += "var field_start = get_field_start( foffset )";
          code_ += "return {{DECODE_FUNC}}( field_start )";
          code_.DecrementIdentLevel();
        }
        else if( IsStruct( type ) ){
          if(IsBuiltin(type)){
            code_.SetValue( "DECODE_FUNC", "decode_" + GetGodotType(type));
          }
          code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var field_offset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not field_offset: return {{GODOT_TYPE}}()";
          code_ += "return {{DECODE_FUNC}}( start + field_offset )";
          code_.DecrementIdentLevel();
        }
        else if(  IsTable( type ) ) {
          code_ += " #TODO - Handle table";
          GenFieldDebug( field );
        }
        else if( IsUnion(type) ){
          code_ += "func {{FIELD_NAME}}():";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return null";
          code_ += "var field_start = get_field_start( foffset )";

          // match the type
          code_ += "match( {{FIELD_NAME}}_type() ):";
          code_.IncrementIdentLevel();
          code_.SetValue("ENUM_TYPE", type.enum_def->name );
          for( const auto &val : type.enum_def->Vals() ){
            if( val->IsZero() )continue;
            code_.SetValue( "ENUM_VALUE", ToUpper( val->name ) );
            code_.SetValue( "UNION_TYPE",  val->name );
            code_.SetValue( "GODOT_TYPE", GetGodotType(val->union_type ) );
            code_ += "{{ENUM_TYPE}}.{{ENUM_VALUE}}:";
            code_.IncrementIdentLevel();
            code_ += "return {{FILE_NAME}}.Get{{UNION_TYPE}}( field_start, bytes )";
            code_.DecrementIdentLevel();
          }
          code_ += "_: pass";
          code_.DecrementIdentLevel();
          code_ += "return null";
          code_.DecrementIdentLevel();
        }
        else if(IsSeries( type ) ){
          GenSeriesAccessors(field);
        }
        else {
          code_ += " #TODO - Unhandled Type";
          GenFieldDebug( field );
        }
        code_ += "";
  }

  void GenPrettyPrinter( const StructDef &struct_def ){
    // Generate Pretty Printer
    code_ += "func pprint( printer : FlatBufferPrinter, heading = \"\"):";
    code_.IncrementIdentLevel();
    code_ += "printer.print(\"{{TABLE_NAME}} {\", heading)";
    code_ += "printer.indent()";
    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue( "FIELD_NAME", Name( *field ) );
      code_.SetValue( "FIELD_TYPE", GetGodotType( field->value.type ) );
      code_ += "# {{FIELD_NAME}}:{{FIELD_TYPE}} \\";
      code_ += field->IsRequired() ? "(required)" : "";
      code_ += "if {{FIELD_NAME}}_is_present():";
      code_.IncrementIdentLevel();
      code_ += "printer.print( self.{{FIELD_NAME}}(), \"{{FIELD_NAME}}\" )";
      code_.DecrementIdentLevel();
    }
    code_ += "printer.outdent()";
    code_ += "printer.print(\"}\")";
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
    GenStaticFactory();


    { // generate Flatbuffer derived class
      code_ += "class {{TABLE_NAME}} extends FlatBuffer:";
      code_.IncrementIdentLevel();
      code_ += "# table";

      GenVtableEnums(struct_def);

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

      GenPrettyPrinter(struct_def);

      code_.DecrementIdentLevel();
      code_ += "";
    }

    GenBuilders(struct_def);

    GenCreateFunc( struct_def );
  }

  void GenSeriesAccessors( const FieldDef &field ){
    // to get here we have checked the field.value.type.basetype == VECTOR || VECTOR64 || ARRAY
    /*
     * What kind of arrays do I have.
     * scalar arrays
     * strings
     * struct arrays
     * table arrays
     * union arrays
     * dictionaries
     */
    const auto type = field.value.type;
    const auto element = type.VectorType();

    std::string to_packed_func;
    GodotArrayType array_type = GODOT_ARRAY_NONE;

    switch( type.element ){
      case BASE_TYPE_BOOL: array_type = GODOT_ARRAY_BOOL; break; // I believe this is a special case?

      case BASE_TYPE_UCHAR: array_type = GODOT_ARRAY_BYTE; break; // Easiest case, we just need to slice.

        // These are relatively easy and have direct corresponding packed arrays in godot.
      case BASE_TYPE_INT:
        array_type = GODOT_ARRAY_PACKED;
        to_packed_func = "to_int32_array()"; break;
      case BASE_TYPE_LONG:
        array_type = GODOT_ARRAY_PACKED;
        to_packed_func = "to_int64_array()"; break;
      case BASE_TYPE_FLOAT:
        array_type = GODOT_ARRAY_PACKED;
        to_packed_func = "to_float32_array()"; break;
      case BASE_TYPE_DOUBLE:
        array_type = GODOT_ARRAY_PACKED;
        to_packed_func = "to_float64_array()"; break;

        // I'm going to have to do some decoding from packed byte array
      case BASE_TYPE_CHAR:
      case BASE_TYPE_SHORT:
      case BASE_TYPE_USHORT:
      case BASE_TYPE_UINT:
      case BASE_TYPE_ULONG:
        array_type = GODOT_ARRAY_SCALAR;
        break;

        // These three I believe are relatively easy too,
        // I just need to hand off to other functions once I get the offsets.
      case BASE_TYPE_STRING:
        array_type = GODOT_ARRAY_STRING;
        break;
      case BASE_TYPE_STRUCT: array_type = GODOT_ARRAY_STRUCT; break;
      case BASE_TYPE_UNION: array_type = GODOT_ARRAY_TABLE; break;

        // I don't know how to handle these right now.
      case BASE_TYPE_NONE:  // something would be broken
      case BASE_TYPE_UTYPE: // a vector of union types?
      case BASE_TYPE_ARRAY:  // a vector of arrays?
      case BASE_TYPE_VECTOR:  // a vector of vectors?
      case BASE_TYPE_VECTOR64: break; // again...
        // I mean it all makes sense that these would be allowed as they are just offsets to data.
        // It's just very recursive.
    }

    code_.SetValue( "ARRAY_TYPE", GetGodotType( type ) );
    code_.SetValue( "ELEMENT_TYPE", GetGodotType( element ) );
    code_.SetValue( "ELEMENT_SIZE", element_size[type.element] );

    code_.SetValue("DECODE_FUNC", decode_funcs[ type.element] );
    code_.SetValue("TO_PACKED_FUNC", to_packed_func );

    // Convenience Function to get the size of the array
    code_ += "# Accessors for {{FIELD_NAME}}";
    code_ += "func {{FIELD_NAME}}_size() -> int:";
    code_.IncrementIdentLevel();
    code_ += "return get_array_size( {{OFFSET_NAME}} )";
    code_.DecrementIdentLevel();
    code_ += "";

    if( array_type == GODOT_ARRAY_BYTE ) {
      code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return 0";
      code_ += "array_start += 4";
      code_ += "return bytes[array_start + index]";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> {{ARRAY_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return []";
      code_ += "var array_size = bytes.decode_u32( array_start )";
      code_ += "array_start += 4";
      code_ += "return bytes.slice( array_start, array_start + array_size )";
      code_.DecrementIdentLevel();
      code_ += "";
    } else if( array_type == GODOT_ARRAY_SCALAR ){
      code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return 0";
      code_ += "array_start += 4";
      code_ += "return bytes.{{DECODE_FUNC}}( array_start + index * {{ELEMENT_SIZE}})";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> {{ARRAY_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return []";
      code_ += "var array_size = bytes.decode_u32( array_start )";
      code_ += "array_start += 4";

      code_ += "var array = []";
      code_ += "array.resize( array_size )";
      code_ += "for i in array_size:";
      code_.IncrementIdentLevel();
      code_ += "array[i] = bytes.{{DECODE_FUNC}}( array_start + i * {{ELEMENT_SIZE}})";
      code_.DecrementIdentLevel();
      code_ += "return array";
      code_.DecrementIdentLevel();
      code_ += "";
    } else if( array_type == GODOT_ARRAY_PACKED ){
      code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return 0";
      code_ += "array_start += 4";
      code_ += "return bytes.{{DECODE_FUNC}}( array_start + index * {{ELEMENT_SIZE}})";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> {{ARRAY_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return []";
      code_ += "var array_size = bytes.decode_u32( array_start )";
      code_ += "array_start += 4";
      code_ += "var array_end = array_start + array_size * {{ELEMENT_SIZE}}";
      code_ += "return bytes.slice( array_start, array_end ).{{TO_PACKED_FUNC}}";
      code_.DecrementIdentLevel();
      code_ += "";
    } else if( array_type == GODOT_ARRAY_STRING ){
      code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return \"\"";
      code_ += "array_start += 4";
      code_ += "var string_start = array_start + index * {{ELEMENT_SIZE}}";
      code_ += "string_start += bytes.decode_u32( string_start )";
      code_ += "return {{DECODE_FUNC}}( string_start )";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> {{ARRAY_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var array_start = get_field_start( {{OFFSET_NAME}} )";
      code_ += "if not array_start: return []";
      code_ += "var array_size = bytes.decode_u32( array_start )";
      code_ += "array_start += 4";
      code_ += "var array : {{ARRAY_TYPE}}";
      code_ += "array.resize( array_size )";
      code_ += "for i in array_size:";
      code_.IncrementIdentLevel();
      code_ += "var idx = array_start + i * {{ELEMENT_SIZE}}";
      code_ += "var element_start = idx + bytes.decode_u32( idx )";
      code_ += "array[i] = {{DECODE_FUNC}}( element_start )";
      code_.DecrementIdentLevel();
      code_ += "return array";
      code_.DecrementIdentLevel();
      code_ += "";
    } else {
      code_ += "# TODO Unhandled array element type.";
      code_ += "func {{FIELD_NAME}}() -> {{ARRAY_TYPE}}: return []";
      GenFieldDebug( field );
    }
  }

  void GenBuilders(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));

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

      const bool is_inline = field->IsScalar() || IsStruct( field->value.type );
      const bool is_default_scalar = is_inline && !field->IsScalarOptional();
      std::string field_name = Name(*field);
      std::string struct_name = Name(struct_def);
      std::string offset = GenFieldOffsetName(*field);
      std::string default_ = is_default_scalar ? field->value.constant : "";


      // Generate accessor functions of the forms:
      // Scalars
      // func add_{{FIELD_NAME}}( {{FIELD_NAME}} : {{GODOT_TYPE}} ) -> void:
      //   fbb_.add_element_{{GODOT_TYPE}}_default( {{FIELD_OFFSET}}, {{FIELD_NAME}}, {{VALUE_DEFAULT}});

      // Non-Scalars
      // func add_{{FIELD_NAME}}( {{FIELD_NAME}}_offset : int ) -> void:
      //   fbb_.add_offset( {{FIELD_OFFSET}, {{FIELD_NAME}}}

      code_.SetValue("FIELD_NAME", field_name );
      code_.SetValue("FIELD_OFFSET", struct_name + "." + offset);
      code_.SetValue("GODOT_TYPE", GetGodotType(field->value.type));


      code_.SetValue("VALUE_DEFAULT", default_ );
      if( IsEnum(field->value.type) ){
        code_.SetValue("TYPE_NAME", "ubyte" ); // enums are interpreted as a ubyte
      } else if( IsStruct(field->value.type) ) {
        code_.SetValue("TYPE_NAME", field->value.type.struct_def->name );
      } else {
        code_.SetValue("TYPE_NAME", TypeName(field->value.type.base_type));
      }

      // Function Signature
      if (is_inline) {
        code_ += "func add_{{FIELD_NAME}}( {{FIELD_NAME}} : {{GODOT_TYPE}} ) -> void:";
      } else {
        code_ += "func add_{{FIELD_NAME}}( {{FIELD_NAME}}_offset : int ) -> void:";
      }
      code_.IncrementIdentLevel();
      // Function Body
      if( field->IsScalar() ){
        code_ +=
            "fbb_.add_element_{{TYPE_NAME}}_default( {{FIELD_OFFSET}}, {{FIELD_NAME}}, {{VALUE_DEFAULT}} )";
      } else if( IsStruct(field->value.type ) ) {
        code_ += "fbb_.add_{{TYPE_NAME}}( {{FIELD_OFFSET}}, {{FIELD_NAME}} )";
      } else {
        code_ += "fbb_.add_offset( {{FIELD_OFFSET}}, {{FIELD_NAME}}_offset )";
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
      if (!field->deprecated && field->IsRequired()) {
        code_.SetValue("FIELD_NAME", Name(*field));
        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
        code_ += "fbb_.Required(o, {{STRUCT_NAME}}.{{OFFSET_NAME}});";
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
      code_.SetValue("PARAM_NAME", Name(*field) );
      code_.SetValue("PARAM_VALUE", "default" );
      code_.SetValue("PARAM_TYPE", GetGodotType(field->value.type) );
      //FIXME add default value if possible.
      if( add_sep ) code_ += "{{SEP}}";
      code_ += "{{PARAM_NAME}} : {{PARAM_TYPE}}\\";
      add_sep = true;
    }
    code_ += " ) -> int :";
    code_.DecrementIdentLevel();
    code_ += "";

    // All the non-inline objects need to be added to the builder before adding our object
    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
      for( auto it = struct_def.fields.vec.rbegin(); it != struct_def.fields.vec.rend(); ++it ) {
        const auto &field = **it;
        if( ! field.deprecated && ( ! struct_def.sortbysize || size == SizeOf(field.value.type.base_type) ) ){
          if( field.IsScalar() || IsStruct( field.value.type ) ){
            // Scalars and Structs are added inline in the table, and do not require creating an offset ahead of time.
            continue;
          }
          code_.SetValue("PARAM_NAME", Name(field) );
          code_.SetValue("PARAM_TYPE", GetGodotType( field.value.type ) );
          if( IsTable(field.value.type) ){
            code_ += "# {{PARAM_NAME}} : {{PARAM_TYPE}}";
            code_ += "# TODO var I haven't yet figured out how to create tables here";
            continue;
          }
          if( IsSeries(field.value.type) ){
            code_.SetValue("CREATE_FUNC", vector_create_func[field.value.type.element] );
          }
          if( IsString(field.value.type) ){
            code_.SetValue("CREATE_FUNC", "create_String" );
          }
          code_ += "var {{PARAM_NAME}}_offset : int = _fbb.{{CREATE_FUNC}}( {{PARAM_NAME}} );";
        }
      }
    }
    code_ += "";
    // Create* function body
    code_ += "var builder = {{STRUCT_NAME}}Builder.new( _fbb );";
    for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2 ) {
      for( auto it = struct_def.fields.vec.rbegin(); it != struct_def.fields.vec.rend(); ++it ) {
        const auto &field = **it;
        if( ! field.deprecated && ( ! struct_def.sortbysize || size == SizeOf(field.value.type.base_type) ) ){

          code_.SetValue("FIELD_NAME", Name( field ) );
          if( field.IsScalar() || IsStruct( field.value.type) ){
            code_.SetValue("PARAM_NAME", Name( field ) );
          } else{
            code_.SetValue("PARAM_NAME", Name( field ) + "_offset" );
          }

          code_ += "builder.add_{{FIELD_NAME}}( {{PARAM_NAME}} );";
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
    if (!GenerateGDScript(parser, path, filename)) { return Status::ERROR; }
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
    //FIXME if (!GenerateGdscriptGRPC(parser, path, filename)) { return Status::ERROR; }
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
