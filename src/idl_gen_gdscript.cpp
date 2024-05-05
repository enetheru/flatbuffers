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
        //TODO GenStruct(*struct_def);
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

  void GenComment(const std::vector<std::string> &dc, const char *prefix = "#") {
    std::string text;
    ::flatbuffers::GenComment(dc, &text, nullptr, prefix);
    code_ += text + "\\";
  }

  std::string GenFieldOffsetName(const FieldDef &field) {
    std::string uname = Name(field);
    std::transform(uname.begin(), uname.end(), uname.begin(), CharToUpper);
    return "VT_" + uname;
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

  // Generate an accessor struct
  void GenTable(const StructDef &struct_def) {
    // Generate an accessor struct, with methods of the form:
    // func name() -> type :
    GenComment(struct_def.doc_comment);

    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ +=
        "class FB_{{STRUCT_NAME}} extends GD_FlatBuffer:\n"
        "\tfunc Get{{STRUCT_NAME}}( _start : int, _bytes : PackedByteArray ):\n"
        "\t\tvar new_{{STRUCT_NAME}} = FB_{{STRUCT_NAME}}.new()\n"
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
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }

      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("OFFSET_NAME", GenFieldOffsetName(*field));
      const auto &type = field->value.type;
      bool basic_type = true;
      switch( type.base_type ){
          case BASE_TYPE_NONE:basic_type = false;break;
          case BASE_TYPE_UTYPE:basic_type = false;break;
          case BASE_TYPE_BOOL:
              code_.SetValue("DECODE_FUNC", "u8" );
              code_.SetValue("RETURN_TYPE", "bool" );
              break;
          case BASE_TYPE_CHAR:
              code_.SetValue("DECODE_FUNC", "s8");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_UCHAR:
              code_.SetValue("DECODE_FUNC", "u8");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_SHORT:
              code_.SetValue("DECODE_FUNC", "s16");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_USHORT:
              code_.SetValue("DECODE_FUNC", "u16");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_INT:
              code_.SetValue("DECODE_FUNC", "s32");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_UINT:
              code_.SetValue("DECODE_FUNC", "u32");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_LONG:
              code_.SetValue("DECODE_FUNC", "s64");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_ULONG:
              code_.SetValue("DECODE_FUNC", "u64");
              code_.SetValue("RETURN_TYPE", "int" );
              break;
          case BASE_TYPE_FLOAT:
              code_.SetValue("DECODE_FUNC", "float");
              code_.SetValue("RETURN_TYPE", "float" );
              break;
          case BASE_TYPE_DOUBLE:
              code_.SetValue("DECODE_FUNC", "double");
              code_.SetValue("RETURN_TYPE", "float" );
              break;
          case BASE_TYPE_STRING:
              //FIXME This is a different type and needs additional consideration
              basic_type = false;
              break;
          case BASE_TYPE_VECTOR:basic_type = false;break;
          case BASE_TYPE_VECTOR64:basic_type = false;break;
          case BASE_TYPE_STRUCT:basic_type = false;break;
          case BASE_TYPE_UNION:basic_type = false;break;
          case BASE_TYPE_ARRAY:basic_type = false;break;
      }
      if( basic_type ){
          code_ +=
              "\tfunc {{FIELD_NAME}}() -> {{RETURN_TYPE}}:\n"
              "\t\tvar offset = get_field_offset( {{OFFSET_NAME}} )\n"
              "\t\tif offset: return bytes.decode_{{DECODE_FUNC}}(offset)\n"
              "\t\treturn 0 # TODO\n";
      } else {
          code_ +=
          "\tfunc {{FIELD_NAME}}():\n"
          "\t\tvar offset = get_field_offset( {{OFFSET_NAME}} )\n";
      }

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
