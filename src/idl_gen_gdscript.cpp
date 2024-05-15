// independent of idl_parser, since this code is not needed for most clients

#include "idl_gen_gdscript.h"

#include <memory>
#include <string>
#include <unordered_set>

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

namespace gdscript {
const std::string prefix = "FB_";
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
"decode_string",  //13 STRING
"",               //14 VECTOR
"",               //15 STRUCT
"",               //16 UNION
"",               //17 ARRAY
"",               //18 VECTOR64
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
        opts_(opts)
  {
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
      // Builtin Classes
      "Color",
      // My used keywords
      "bytes",
      "start",
      nullptr,
    };
    for (auto kw = keywords; *kw; kw++) keywords_.insert(*kw);
    code_.SetPadding("\t");
  }

  // Iterate through all definitions we haven't. Generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() {
    code_.Clear();
    code_.SetValue( "PREFIX", "FB_"); //FIXME tie this to some option

    code_ += "# " + std::string(FlatBuffersGeneratedWarning());
    code_.SetValue( "FILE_NAME", file_name_ );
    //FIXME do I make the root_table the base class object?
    code_ += "class_name {{FILE_NAME}}\n";

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

  const IDLOptionsGdscript opts_;

  std::string EscapeKeyword(const std::string &name) const {
    return keywords_.find(name) == keywords_.end() ? name : name + "_";
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
      return type.enum_def->name;
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
    else if( IsStruct( type ) ){
      return prefix + type.struct_def->name;
    }
    else if( IsTable( type ) ){
      return prefix + type.struct_def->name;
    }
    else{
      return "TODO";
    }
  }

  bool HasNativeArray( const Type &type ){
    switch( type.base_type ){
      case BASE_TYPE_UCHAR:
      case BASE_TYPE_INT:
      case BASE_TYPE_LONG:
      case BASE_TYPE_FLOAT:
      case BASE_TYPE_DOUBLE:
        return true;
      case BASE_TYPE_NONE:
      case BASE_TYPE_UTYPE:
      case BASE_TYPE_BOOL:
      case BASE_TYPE_CHAR:
      case BASE_TYPE_SHORT:
      case BASE_TYPE_USHORT:
      case BASE_TYPE_UINT:
      case BASE_TYPE_ULONG:
      case BASE_TYPE_STRING:
      case BASE_TYPE_VECTOR:
      case BASE_TYPE_VECTOR64:
      case BASE_TYPE_STRUCT:
      case BASE_TYPE_UNION:
      case BASE_TYPE_ARRAY:
      default:
        return false;
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
//                         // as default serialization behavior if field is a string.
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
//      // Type is a union which uses type aliases where at least one type is
//      // available under two different names.
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
    // Generate the class definition
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ += "class {{PREFIX}}{{STRUCT_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();

    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    code_ += "static func Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ):";
    code_.IncrementIdentLevel();
    code_ += "var new_{{STRUCT_NAME}} = {{PREFIX}}{{STRUCT_NAME}}.new()";
    code_ += "new_{{STRUCT_NAME}}.start = _start";
    code_ += "new_{{STRUCT_NAME}}.bytes = _bytes";
    code_ += "return new_{{STRUCT_NAME}}";
    code_.DecrementIdentLevel();
    code_ += "";

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

      if( field->IsScalar() ){
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_.SetValue("DECODE_FUNC", decode_funcs[ type.base_type] );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return bytes.{{DECODE_FUNC}}(start + {{OFFSET}})\\";
        code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
        code_.DecrementIdentLevel();
      }
      else if( IsStruct( type) ){
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_.SetValue("STRUCT_NAME", type.struct_def->name );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return {{GODOT_TYPE}}.Get{{STRUCT_NAME}}(start + {{OFFSET}}, bytes)";
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

  void GenSeriesAccessors( const FieldDef &field ){
    // Convenience Function to get the size of the array
    code_ += "func {{FIELD_NAME}}_count() -> int:";
    code_.IncrementIdentLevel();
    code_ += "return get_array_count( {{OFFSET_NAME}} )";
    code_.DecrementIdentLevel();
    code_ += "";


    const auto type = field.value.type.VectorType();
    code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
    code_.SetValue("DECODE_FUNC", decode_funcs[ type.base_type] );
    if( IsScalar(type.base_type ) ){
      code_ +="func {{FIELD_NAME}}_get( index : int ) -> {{GODOT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return 0\\";
      code_ += IsEnum( type ) ? " as {{GODOT_TYPE}}" : "";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "var element_start = get_array_element_start( array_start, index )";
      code_ += "return bytes.{{DECODE_FUNC}}(element_start ) \\";
      code_ += IsEnum( type ) ? "as {{GODOT_TYPE}}" : "";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> FlatBufferArray:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return null";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "return get_array( array_start, func(loc): bytes.{{DECODE_FUNC}}(loc) )";
      code_.DecrementIdentLevel();
      code_ += "";
    }
    else if( IsString( type ) ){
      code_ +="func {{FIELD_NAME}}_get( index : int ) -> {{GODOT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return \"\"";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "var element_start = get_array_element_start( array_start, index )";
      code_ += "return {{DECODE_FUNC}}(element_start )";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> FlatBufferArray:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return null";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "return get_array( array_start, {{DECODE_FUNC}} )";
      code_.DecrementIdentLevel();
      code_ += "";
    }
    else if( IsStruct(type) || IsTable( type ) ){
      code_.SetValue("STRUCT_NAME", type.struct_def->name );

      code_ +="func {{FIELD_NAME}}_get( index : int ) -> {{GODOT_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return null";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "var element_start = get_array_element_start( array_start, index )";
      code_ += "return {{GODOT_TYPE}}.Get{{STRUCT_NAME}}( element_start, bytes )";
      code_.DecrementIdentLevel();
      code_ += "";

      code_ += "func {{FIELD_NAME}}() -> FlatBufferArray:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return null";
      code_ += "var array_start = get_field_start( foffset )";
      code_ += "return get_array( array_start, {{GODOT_TYPE}}.Get{{STRUCT_NAME}} )";
      code_.DecrementIdentLevel();
      code_ += "";
    }
    else {
      code_ += "# TODO Unhandled array element type.";
      GenFieldDebug( field );
    }

    if( HasNativeArray( type ) ){
      if( type.base_type == BASE_TYPE_UCHAR ) {
        code_.SetValue("ARRAY_TYPE", "PackedByteArray");
        code_.SetValue("ARRAY_CONV", "");
      }
      else if( type.base_type == BASE_TYPE_INT ) {
        code_.SetValue("ARRAY_TYPE", "PackedInt32Array");
        code_.SetValue("ARRAY_CONV", ".to_int32_array()");
      }
      else if( type.base_type == BASE_TYPE_LONG) {
        code_.SetValue("ARRAY_TYPE", "PackedInt64Array");
        code_.SetValue("ARRAY_CONV", ".to_int64_array()");
      }
      else if( type.base_type == BASE_TYPE_FLOAT) {
        code_.SetValue("ARRAY_TYPE", "PackedFloat32Array");
        code_.SetValue("ARRAY_CONV", ".to_float32_array()");
      }
      else if( type.base_type == BASE_TYPE_DOUBLE) {
        code_.SetValue("ARRAY_TYPE", "PackedFloat64Array");
        code_.SetValue("ARRAY_CONV", ".to_float64_array()");
      }
      {
        code_ += "func {{FIELD_NAME}}_native() -> {{ARRAY_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
        code_ += "if not foffset: return []";
        code_ += "var array_start = get_field_start( foffset )";
        code_ += "return bytes.slice( array_start + 4, bytes.decode_u32( array_start ) ){{ARRAY_CONV}}";
        code_.DecrementIdentLevel();
        code_ += "";
      }
    }
  }

  void GenTableFieldAccess(const FieldDef &field) {
    code_.SetValue("FIELD_NAME", Name(field));
    code_.SetValue("OFFSET_NAME", GenFieldOffsetName(field));
    code_.SetValue("RETURN_TYPE", "BEANS");

    const auto &type = field.value.type;

    if( field.IsScalar() ){
      if (IsEnum(type) ){
        const auto &enum_def = *type.enum_def;
        code_.SetValue("RETURN_TYPE", Name( enum_def ) );
      } else {
      }
      code_ += "# {{FIELD_NAME}}: {{RETURN_TYPE}}";
      code_ += "func {{FIELD_NAME}}() -> {{RETURN_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return {{DEFAULT}} as {{RETURN_TYPE}}";
      code_ += "return bytes.{{DECODE_FUNC}}( start + foffset ) as {{RETURN_TYPE}}";
      code_.DecrementIdentLevel();
    }
    else if( IsString(type) ){
      code_ += "# {{FIELD_NAME}}: {{RETURN_TYPE}}";
      code_ += "func {{FIELD_NAME}}() -> {{RETURN_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return {{DEFAULT}}";
      code_ += "return  {{DECODE_FUNC}}( get_field_start( foffset ) )";
      code_.DecrementIdentLevel();
    }
    else if( IsStruct(type) || IsTable(type) ){
      code_.SetValue("RETURN_TYPE", type.struct_def->name );
      code_ += "# {{FIELD_NAME}}: {{RETURN_TYPE}}";
      code_ += "func {{FIELD_NAME}}() -> {{PREFIX}}{{RETURN_TYPE}}:";
      code_.IncrementIdentLevel();
      code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
      code_ += "if not foffset: return null";
      code_ += "return {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}}( get_field_start(foffset), bytes )";
      code_.DecrementIdentLevel();
    }
    else if( IsSeries(type) ) {
      GenFieldDebug(field);
      code_.SetValue("DEFAULT", "0");

      // Convenience Function to get the size of the array
      code_ += "func {{FIELD_NAME}}_count() -> int:";
      code_.IncrementIdentLevel();
      code_ += "return get_array_count( {{OFFSET_NAME}} )";
      code_.DecrementIdentLevel();
      code_ += "";

      if (IsVector(type)) {
        if (IsVectorOfTable(type) || IsVectorOfStruct(type) ) {
          if (type.struct_def) {
            code_.SetValue("RETURN_TYPE", type.struct_def->name);
          }
          code_ += "# {{FIELD_NAME}}: [{{RETURN_TYPE}}]";
          code_ += "func {{FIELD_NAME}}_get( index : int ) -> {{PREFIX}}{{RETURN_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return null";
          code_ += "var array_start = get_field_start( foffset )";
          code_ += "var element_start = get_array_element_start( array_start, index )";
          code_ += "return {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}}( element_start, bytes )";
          code_.DecrementIdentLevel();
          code_ += "";

          code_ += "func {{FIELD_NAME}}() -> FlatBufferArray:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return null";
          code_ += "var array_start = get_field_start( foffset )";
          code_ += "return get_array( array_start, {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}} )";
          code_.DecrementIdentLevel();
          code_ += "";
        }
        else {
          code_ +="func {{FIELD_NAME}}_get( index : int ) -> {{RETURN_TYPE}}:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return {{DEFAULT}}";
          code_ += "var array_start = get_field_start( foffset )";
          code_ += "var element_start = get_array_element_start( array_start, index )";
          if( type.element == BASE_TYPE_STRING ){
            code_ += "return {{DECODE_FUNC}}( element_start )";
          } else {
            code_ += "return bytes.{{DECODE_FUNC}}( element_start )";
          }
          code_.DecrementIdentLevel();
          code_ += "";
          code_ += "func {{FIELD_NAME}}() -> FlatBufferArray:";
          code_.IncrementIdentLevel();
          code_ += "var foffset = get_field_offset( {{OFFSET_NAME}} )";
          code_ += "if not foffset: return null";
          code_ += "var array_start = get_field_start( foffset )";
          if( type.element == BASE_TYPE_STRING ){
            code_ += "return get_array( array_start, {{DECODE_FUNC}} )";
          } else {
            code_ += "return get_array( array_start, bytes.{{DECODE_FUNC}} )";
          }
          code_.DecrementIdentLevel();
          code_ += "";
        }
        code_ += "# {{FIELD_NAME}}: Vector[{{RETURN_TYPE}}]";
      }
      else if (IsArray(type)) {
        code_.SetValue("FIXED_LENGTH", NumToString(type.fixed_length));
        code_ += "# has fixed_length: {{FIXED_LENGTH}}";
        code_ += "# TODO Deal with Arrays";
      }
      else{
        code_ += "# This is some unknown bullshit is what this is.";
        GenFieldDebug(field);
      }
    } else {
      code_ += "# TODO - Implement this type";
      GenFieldDebug(field);
    }
    code_ += "";
  }

  // Generate an accessor struct
  void GenTable(const StructDef &struct_def) {
    // Generate class to access the table fields
    // The generated classes are a view into a PackedByteArray,
    // will decode the data on access.

    GenComment(struct_def.doc_comment);

    //Generate Class definition
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ += "class {{PREFIX}}{{STRUCT_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();
    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    code_ += "static func Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ) -> {{PREFIX}}{{STRUCT_NAME}}:";
    code_.IncrementIdentLevel();
    code_ += "var new_{{STRUCT_NAME}} = {{PREFIX}}{{STRUCT_NAME}}.new()";
    code_ += "new_{{STRUCT_NAME}}.start = _start";
    code_ += "new_{{STRUCT_NAME}}.bytes = _bytes";
    code_ += "return new_{{STRUCT_NAME}}";
    code_ += "";
    code_.DecrementIdentLevel();

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

    // Generate presence funcs
    for (const auto &field : struct_def.fields.vec) {
      code_.SetValue( "FIELD_NAME", Name( *field ) );
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        code_ += "# {{FIELD_NAME}} is deprecated\n";
        continue;
      }
      if( field->IsRequired() ){
        // Required fields are always accessible.
        code_ += "# {{FIELD_NAME}} is required\n";
      }
      code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
      code_ += "func {{FIELD_NAME}}_is_present() -> bool:";
      code_.IncrementIdentLevel();
      code_ += "return get_field_offset( {{OFFSET_NAME}} )";
      code_.DecrementIdentLevel();
      code_ += "";
    }

    // Generate the accessors.
    for (const auto field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue( "FIELD_NAME", Name(*field) );
      code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));

      const auto &type = field->value.type;

      if( field->IsScalar() ){
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_.SetValue("DECODE_FUNC", decode_funcs[ type.base_type] );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return bytes.{{DECODE_FUNC}}(start + {{OFFSET_NAME}}) \\";
        code_ += IsEnum( type ) ? "as {{GODOT_TYPE}}" : "";
        code_.DecrementIdentLevel();
      }
      else if( IsString( type ) ){
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return decode_string(start + {{OFFSET_NAME}})";
        code_.DecrementIdentLevel();
      }
      else if( IsStruct( type ) || IsTable( type ) ){
        code_.SetValue( "GODOT_TYPE", GetGodotType(type) );
        code_.SetValue("STRUCT_NAME", type.struct_def->name );
        code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
        code_.IncrementIdentLevel();
        code_ += "return {{GODOT_TYPE}}.Get{{STRUCT_NAME}}(start + {{OFFSET_NAME}}, bytes)";
        code_.DecrementIdentLevel();
      }
      else if(IsSeries( type) ){
        GenSeriesAccessors(*field);
      }
      else {
        code_ += " #TODO - Unhandled Type";
        GenFieldDebug( *field );
      }
      code_ += "";
    }

    // Generate Pretty Printer
//    code_ += "func _to_string() -> String:";
//    code_.IncrementIdentLevel();
//    code_ += "var value_ : String = \"\"";
//
//    code_.SetValue("SEP", "");
//    for (const auto &field : struct_def.fields.vec) {
//      if (field->deprecated) {
//        // Deprecated fields won't be accessible.
//        continue;
//      }
//      code_.SetValue( "FIELD_NAME", Name( *field ) );
//      code_ += "if {{FIELD_NAME}}_is_present():";
//      code_.IncrementIdentLevel();
//      code_ += "value_ += \"{{FIELD_NAME}}: %s{{SEP}}\" % {{FIELD_NAME}}()";
//      code_.DecrementIdentLevel();
//      code_.SetValue("SEP", ", ");
//    }
//    code_ += "return value_";
//    code_.DecrementIdentLevel();
//    code_ += "";

    // Decrement after the class definition
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
  const auto filebase = StripPath(StripExtension(file_name));
  gdscript::GdscriptGenerator generator(parser, path, file_name, parser.opts);
  const auto included_files = parser.GetIncludedFilesRecursive(file_name);
  std::string make_rule =
      generator.GeneratedFileName(path, filebase, parser.opts) + ": ";
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

  bool IsSchemaOnly() const override { return true; }

  bool SupportsBfbsGeneration() const override { return false; }

  bool SupportsRootFileGeneration() const override { return false; }

  IDLOptions::Language Language() const override { return IDLOptions::kCpp; }

  std::string LanguageName() const override { return "GDScript"; }
};

}  // namespace

std::unique_ptr<CodeGenerator> NewGDScriptCodeGenerator() {
  return std::unique_ptr<GdscriptCodeGenerator>(new GdscriptCodeGenerator());
}

}  // namespace flatbuffers
