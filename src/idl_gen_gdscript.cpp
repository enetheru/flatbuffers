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
      nullptr,
    };
    for (auto kw = keywords; *kw; kw++) keywords_.insert(*kw);
  }

  // Iterate through all definitions we haven't. Generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() {
    code_.Clear();
    code_ += "# " + std::string(FlatBuffersGeneratedWarning()) + "\n\n";


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

  bool GetDecodeReturnValues( BaseType base_type ){
    switch( base_type ){
      case BASE_TYPE_BOOL:
        code_.SetValue("RETURN_TYPE", "bool" );
        code_.SetValue("DECODE_FUNC", "decode_bool" );
        break;
      case BASE_TYPE_CHAR:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_char" );
        break;
      case BASE_TYPE_UCHAR:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_uchar" );
        break;
      case BASE_TYPE_SHORT:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_short" );
        break;
      case BASE_TYPE_USHORT:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_ushort" );
        break;
      case BASE_TYPE_INT:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_int" );
        break;
      case BASE_TYPE_UINT:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_uint" );
        break;
      case BASE_TYPE_LONG:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_long" );
        break;
      case BASE_TYPE_ULONG:
        code_.SetValue("RETURN_TYPE", "int" );
        code_.SetValue("DECODE_FUNC", "decode_ulong" );
        break;
      case BASE_TYPE_FLOAT:
        code_.SetValue("RETURN_TYPE", "float" );
        code_.SetValue("DECODE_FUNC", "decode_float" );
        break;
      case BASE_TYPE_DOUBLE:
        code_.SetValue("RETURN_TYPE", "float" );
        code_.SetValue("DECODE_FUNC", "decode_double" );
        break;
      case BASE_TYPE_STRING:
        code_.SetValue("DEFAULT", "\"\"" );
        code_.SetValue("RETURN_TYPE", "String" );
        code_.SetValue("DECODE_FUNC", "decode_string" );
        break;

        // Unhandled
      case BASE_TYPE_NONE:
      case BASE_TYPE_UTYPE:
      case BASE_TYPE_VECTOR:
      case BASE_TYPE_VECTOR64:
      case BASE_TYPE_STRUCT:
      case BASE_TYPE_UNION:
      case BASE_TYPE_ARRAY:
      default:
        return false;
    }
    return true;
  }

  void GenComment(const std::vector<std::string> &dc, const char *prefix = "#") {
    std::string text;
    ::flatbuffers::GenComment(dc, &text, nullptr, prefix);
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

  void GenFieldArrayBasic(){
    code_ +=
        "\tfunc {{FIELD_NAME}}_get( index : int ) -> {{RETURN_TYPE}}:\n"
        "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
        "\t\tif foffset: return {{DECODE_FUNC}}( get_array_field_offset(foffset, index) )\n"
        "\t\treturn {{DEFAULT}}\n";

    code_ +=
        "\tfunc {{FIELD_NAME}}() -> FlatBuffer_Array:\n"
        "\t\treturn get_array( {{OFFSET_NAME}}, {{DECODE_FUNC}} )\n";
  }

  // Generate an enum declaration,
  // an enum string lookup table,
  // and an enum array of values

  void GenEnum(const EnumDef &enum_def) {
    code_.SetValue("ENUM_NAME", Name(enum_def));

    GenComment(enum_def.doc_comment);
    code_ += "enum " + Name(enum_def) + "\\";
    code_ += " {";

    code_.SetValue("SEP", ",");
    auto add_sep = false;
    for (const auto ev : enum_def.Vals()) {
      if (add_sep) code_ += "{{SEP}}";
      GenComment(ev->doc_comment );
      code_.SetValue("KEY",  ToUpper(Name(*ev)) );
      code_.SetValue("VALUE", enum_def.ToString(*ev) );
      code_ += "\t{{KEY}} = {{VALUE}}\\";
      add_sep = true;
    }
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
    // FIXME tie the {{PREFIX}} to something, perhaps namespace
    code_.SetValue( "PREFIX", "FB_");
    code_ +=
        "class {{PREFIX}}{{STRUCT_NAME}} extends GD_FlatBuffer:";

    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    code_ +=
        "\tstatic func Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ):\n"
        "\t\tvar new_{{STRUCT_NAME}} = {{PREFIX}}{{STRUCT_NAME}}.new()\n"
        "\t\tnew_{{STRUCT_NAME}}.start = _start\n"
        "\t\tnew_{{STRUCT_NAME}}.bytes = _bytes\n"
        "\t\treturn new_{{STRUCT_NAME}}\n";

    // Generate the accessor functions, in the form:
    // func name() -> type :
    for (const auto &field : struct_def.fields.vec) {
      GenFieldDebug(*field);
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }

      const auto &type = field->value.type;
      code_.SetValue( "OFFSET" , NumToString(field->value.offset) );
      if( field->IsScalar() ){
        if( IsEnum( type ) ){
          GetDecodeReturnValues( type.enum_def->underlying_type.base_type );
        } else{
          GetDecodeReturnValues( type.base_type );
        }
        code_ +=
            "\tfunc {{FIELD_NAME}}() -> {{RETURN_TYPE}}:\n"
            "\t\treturn {{DECODE_FUNC}}(start + {{OFFSET}})";
      } else {
        code_ += "#TODO - Implement this type";
      }
      code_ += "";
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
        GetDecodeReturnValues(enum_def.underlying_type.base_type);
        code_.SetValue("RETURN_TYPE", Name( enum_def) );
        code_ +=
            "\tfunc {{FIELD_NAME}}() -> {{RETURN_TYPE}}:\n"
            "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
            "\t\tif foffset: return {{DECODE_FUNC}}(foffset) as {{RETURN_TYPE}}\n"
            "\t\treturn 0 as {{RETURN_TYPE}}";
      }
      else {
        GetDecodeReturnValues(type.base_type );
        code_ +=
            "\tfunc {{FIELD_NAME}}() -> {{RETURN_TYPE}}:\n"
            "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
            "\t\tif foffset: return {{DECODE_FUNC}}(foffset)\n"
            "\t\treturn 0";
      }
    }
    else if( IsString(type) ){
      GetDecodeReturnValues( type.base_type );
      code_ +=
          "\tfunc {{FIELD_NAME}}() -> {{RETURN_TYPE}}:\n"
          "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
          "\t\tif foffset: return {{DECODE_FUNC}}( foffset )\n"
          "\t\treturn \"\"";
    }
    else if( IsStruct(type) ){
      code_.SetValue("RETURN_TYPE", type.struct_def->name );
      code_ +=
          "\tfunc {{FIELD_NAME}}() -> {{PREFIX}}{{RETURN_TYPE}}:\n"
          "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
          "\t\tif foffset: return {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}}( foffset, bytes )\n"
          "\t\treturn null";
    }
    else if( IsTable(type) ){
      code_.SetValue("RETURN_TYPE", type.struct_def->name );
      code_ +=
          "\tfunc {{FIELD_NAME}}() -> {{PREFIX}}{{RETURN_TYPE}}:\n"
          "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
          "\t\tif foffset: return {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}}( foffset, bytes )\n"
          "\t\treturn null";
    }
    else if( IsSeries(type) ) {
      if (IsVector(type)) {
        if (IsVectorOfTable(type) || IsVectorOfStruct(type)) {
          if (type.struct_def) {
            code_.SetValue("RETURN_TYPE", type.struct_def->name);
          }
        } else {
          code_.SetValue("RETURN_TYPE", TypeName(type.element));
        }
        code_ += "\t# Vector[{{RETURN_TYPE}}]";
      }
      if (IsArray(type)) {
        code_.SetValue("FIXED_LENGTH", NumToString(type.fixed_length));
        code_ += "\t# has fixed_length: {{FIXED_LENGTH}}";
      }

      // Convenience Function to get the size of the array
      code_ +=
          "\tfunc {{FIELD_NAME}}_count() -> int:\n"
          "\t\treturn get_array_count( {{OFFSET_NAME}} )\n";

      code_.SetValue("DEFAULT", "0");
      if( GetDecodeReturnValues(type.element) ){
        GenFieldArrayBasic();
      } else {
        code_ +=
            "\tfunc {{FIELD_NAME}}_get( index : int ) -> {{PREFIX}}{{RETURN_TYPE}}:\n"
            "\t\tvar foffset = get_field_offset( {{OFFSET_NAME}} )\n"
            "\t\tif foffset: return {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}}( get_array_field_offset(foffset, index), bytes )\n"
            "\t\treturn null\n";

        code_ +=
            "\tfunc {{FIELD_NAME}}() -> FlatBuffer_Array:\n"
            "\t\treturn get_array( {{OFFSET_NAME}}, {{PREFIX}}{{RETURN_TYPE}}.Get{{RETURN_TYPE}} )\n";
      }
    } else {
      code_ += "#TODO - Implement this type";
    }
    code_ += "";
  }

  // Generate an accessor struct
  void GenTable(const StructDef &struct_def) {
    // Generate class to access the table fields
    // The generated classes are like a view into a PackedByteArray,
    // it decodes the data on access.

    GenComment(struct_def.doc_comment);

    //Generate Class definition
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_.SetValue( "PREFIX", "FB_"); //FIXME tie this to some option
    code_ +=
        "class {{PREFIX}}{{STRUCT_NAME}} extends GD_FlatBuffer:";
    // GDScript likes to have empty constructors and cant do overloading.
    // So generate the static factory func in place of a constructor.
    code_ +=
        "\tstatic func Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ):\n"
        "\t\tvar new_{{STRUCT_NAME}} = {{PREFIX}}{{STRUCT_NAME}}.new()\n"
        "\t\tnew_{{STRUCT_NAME}}.start = _start\n"
        "\t\tnew_{{STRUCT_NAME}}.bytes = _bytes\n"
        "\t\treturn new_{{STRUCT_NAME}}\n";


    // Generate field id constants.
    if (!struct_def.fields.vec.empty()) {
      // We need to add a trailing comma to all elements except the last one as
      // older versions of gcc complain about this.
      code_.SetValue("SEP", "");
      code_ +=
          "\tenum {";
      for (const auto &field : struct_def.fields.vec) {
        if (field->deprecated) {
          // Deprecated fields won't be accessible.
          continue;
        }

        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
        code_.SetValue("OFFSET_VALUE", NumToString(field->value.offset));
        code_ += "{{SEP}}\t\t{{OFFSET_NAME}} = {{OFFSET_VALUE}}\\";
        code_.SetValue("SEP", ",\n");
      }
      code_ += "";
      code_ += "\t}\n";
    }


    // Generate the accessors.
    for (const auto &field : struct_def.fields.vec) {
      GenFieldDebug(*field);
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      GenTableFieldAccess(*field);
    }
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
