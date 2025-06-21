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

namespace gdscript {



// The fields are:
// - enum
// - FlatBuffers schema type.
// - C++ type.
// - PBASUFFIX is the PackedByteArray.{decode|encode}_* functions
// - enum value (matches the reflected values)

//  BASE_TYPE ADD_FUNC CREATE_FUNC  PBA_SUFFIX  PBA_TO
// PackedByteArray types for encode_* and decode_* functions


//   ENUM      IDLTYPE,  CTYPE,          GDTYPE,  PBA_SUFFIX,   PBA_CONVERSION         PACKED_TYPE        ENUM_VALUE
#define GODOT_GEN_TYPES_SCALAR(TD) \
  TD(NONE,     "",       uint8_t,        int,     "",           "",                      Array,               0) \
  TD(UTYPE,    "",       uint8_t,        int,     "u8",         "",                      Array,               1) /* begin scalar/int */ \
  TD(BOOL,     "bool",   uint8_t,        int,     "u8",         "",                      Array,               2) \
  TD(CHAR,     "byte",   int8_t,         bool,    "s8",         "",                      Array,               3) \
  TD(UCHAR,    "ubyte",  uint8_t,        int,     "u8",         "",                      PackedByteArray,     4) \
  TD(SHORT,    "short",  int16_t,        int,     "s16",        "",                      Array,               5) \
  TD(USHORT,   "ushort", uint16_t,       int,     "u16",        "",                      Array,               6) \
  TD(INT,      "int",    int32_t,        int,     "s32",        "to_int32_array",        PackedInt32Array,    7) \
  TD(UINT,     "uint",   uint32_t,       int,     "u32",        "",                      Array,               8) \
  TD(LONG,     "long",   int64_t,        int,     "s64",        "to_int64_array",        PackedInt64Array,    9) \
  TD(ULONG,    "ulong",  uint64_t,       int,     "u64",        "",                      Array,              10) /* end int */ \
  TD(FLOAT,    "float",  float,          float,   "float",      "to_float32_array",      PackedFloat32Array, 11) /* begin float */ \
  TD(DOUBLE,   "double", double,         float,   "double",     "to_float64_array",      PackedFloat64Array, 12) /* end float/scalar */
#define GODOT_GEN_TYPES_POINTER(TD) \
  TD(STRING,   "string", Offset<void>,   String,  "",           "get_string_from_utf8",  PackedStringArray,  13) \
  TD(VECTOR,   "",       Offset<void>,   Array,   "",           "",                      Array,              14) \
  TD(VECTOR64, "",       Offset64<void>, Array,   "",           "",                      Array,              18) \
  TD(STRUCT,   "",       Offset<void>,   int,     "",           "",                      Array,              15) \
  TD(UNION,    "",       Offset<void>,   Variant, "",           "",                      Array,              16)
#define GODOT_GEN_TYPE_ARRAY(TD) \
  TD(ARRAY,    "",       int,            Array,   "",           "",                      Array,              17)

#define GODOT_GEN_TYPES(TD) \
GODOT_GEN_TYPES_SCALAR(TD) \
GODOT_GEN_TYPES_POINTER(TD) \
GODOT_GEN_TYPE_ARRAY(TD)

inline const char* gdPBASuffix(const BaseType t) {
  switch (t) {
#define GODOT_TD(ENUM, IDLTYPE, CTYPE, GDTYPE, PBASUFFIX, ...) \
case BASE_TYPE_##ENUM: return PBASUFFIX;
    GODOT_GEN_TYPES(GODOT_TD)
#undef GODOT_TD
  default: FLATBUFFERS_ASSERT(0);
  }
  return "";
}

inline const char* gdPBAConvert(const BaseType t) {
  switch (t) {
#define GODOT_TD(ENUM, IDLTYPE, CTYPE, GDTYPE, PBAS, PBAC, ...) \
case BASE_TYPE_##ENUM: return PBAC;
    GODOT_GEN_TYPES(GODOT_TD)
#undef GODOT_TD
  default: FLATBUFFERS_ASSERT(0);
  }
  return "";
}

inline const char* gdType(const BaseType t) {
  switch (t) {
#define GODOT_TD(ENUM, IDLTYPE, CTYPE, GDTYPE, ...) \
case BASE_TYPE_##ENUM: return #GDTYPE;
    GODOT_GEN_TYPES(GODOT_TD)
#undef GODOT_TD
  default: FLATBUFFERS_ASSERT(0);
  }
  return "";
}

inline const char* gdArrayType(const BaseType t) {
  switch (t) {
#define GODOT_TD(ENUM, IDLTYPE, CTYPE, GDTYPE, PBAS, PBAC, ARRAY, ...) \
case BASE_TYPE_##ENUM: return #ARRAY;
    GODOT_GEN_TYPES(GODOT_TD)
#undef GODOT_TD
  default: FLATBUFFERS_ASSERT(0);
  }
  return "";
}


// Extension of IDLOptions for gdscript-generator.
struct IDLOptionsGdscript : public IDLOptions {
  // All fields start with 'g_' prefix to distinguish from the base IDLOptions.

  IDLOptionsGdscript(const IDLOptions &opts)  // NOLINT(*-explicit-constructor)
      : IDLOptions(opts) {
    // TODO Possible future options to respect
    //  generate_object_based_api = false;
    //  object_prefix = "";
    //  object_suffix = "";
    //  include_prefix = "";
    //  keep_prefix = false;
    //  root_type = "";
    //  std::string filename_suffix;
    //  std::string filename_extension;
    //  std::string project_root;
  }
};


struct gdIncludeDef {
  std::string include_name;
  std::string include_path;
};

/*MARK: GDScriptGenerator
║  ___ ___  ___         _      _    ___                       _
║ / __|   \/ __| __ _ _(_)_ __| |_ / __|___ _ _  ___ _ _ __ _| |_ ___ _ _
║| (_ | |) \__ \/ _| '_| | '_ \  _| (_ / -_) ' \/ -_) '_/ _` |  _/ _ \ '_|
║ \___|___/|___/\__|_| |_| .__/\__|\___\___|_||_\___|_| \__,_|\__\___/_|
╙────────────────────────|_|──────────────────────────────────────────────*/

class GdscriptGenerator final : public BaseGenerator {
public:
  /*MARK: Constructor
  ║  ___             _               _
  ║ / __|___ _ _  __| |_ _ _ _  _ __| |_ ___ _ _
  ║| (__/ _ \ ' \(_-<  _| '_| || / _|  _/ _ \ '_|
  ║ \___\___/_||_/__/\__|_|  \_,_\__|\__\___/_|
  ╙───────────────────────────────────────────────*/
  GdscriptGenerator(const Parser &parser, const std::string &path,
                    const std::string &file_name, IDLOptionsGdscript opts)
      : BaseGenerator(parser, path, file_name, "", "::", "gd"),
        opts_(std::move(opts)) {
    code_.SetPadding("\t");

    // == Godot Types ==

    // atomic types
    // bool,
    // int,
    // float,
    // String

    static constexpr const char *godot_structs[] = {
      "Vector2",    "Vector2i",    "Rect2",   "Rect2i",      "Vector3",
      "Vector3i",   "Transform2D", "Vector4", "Vector4i",    "Plane",
      "Quaternion", "AABB",        "Basis",   "Transform3D", "Projection",
      "Color",      nullptr
    };
    for (auto kw = godot_structs; *kw; kw++) builtin_structs.insert(*kw);

    static constexpr const char *godot_stringlike[] = {
      "String", "StringName", "NodePath", nullptr
    };

    static constexpr const char *godot_composite[] = {
      "Rid", "Object", "Callable", "Signal", "Dictionary", nullptr
    };

    static constexpr const char *godot_arraylike[] = {
      "Array",               "packed_byte_array",    "packed_int32_array",
      "packed_int64_array",  "packed_float32_array", "packed_float64_array",
      "packed_string_array", "packed_vector2_array", "packed_vector3_array",
      "packed_color_array",  "packed_vector4_array", nullptr
    };


    static constexpr const char *gdscript_keywords[] = {
      "if",      "elif",     "else",  "for",    "while", "match",
      "break",   "continue", "pass",  "return", "class", "class_name",
      "extends", "is",       "in",    "as",     "self",  "signal",
      "func",    "static",   "const", "enum",   "var",   "breakpoint",
      "preload", "await",    "yield", "assert", "void",  "PI",
      "TAU",     "INF",      "NAN",   nullptr,
    };

    static constexpr const char *packed[] = {
      "Color", "Vector2", "Vector3", "Vector4", nullptr
    };

    static constexpr const char *my_keywords[] = { "bytes", "start", nullptr };

    for (auto kw = godot_structs; *kw; kw++) keywords.insert(*kw);
    for (auto kw = godot_stringlike; *kw; kw++) keywords.insert(*kw);
    for (auto kw = godot_composite; *kw; kw++) keywords.insert(*kw);
    for (auto kw = godot_arraylike; *kw; kw++) keywords.insert(*kw);
    for (auto kw = gdscript_keywords; *kw; kw++) keywords.insert(*kw);
    for (auto kw = my_keywords; *kw; kw++) keywords.insert(*kw);
    for (auto item = packed; *item; item++) packed_structs.insert(*item);

  }

  /*MARK: Generate
  ║  ___                       _
  ║ / __|___ _ _  ___ _ _ __ _| |_ ___
  ║| (_ / -_) ' \/ -_) '_/ _` |  _/ -_)
  ║ \___\___|_||_\___|_| \__,_|\__\___|
  ╙─────────────────────────────────────*/
  // Iterate through all definitions we haven't. Generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() override {

    MapIncludedTypes();

    code_.Clear();
    const auto file_path = GeneratedFileName(path_, file_name_, opts_);
    code_.SetValue("FILE_NAME", StripPath(file_path));

    code_ += "# " + std::string(FlatBuffersGeneratedWarning());
    code_ += "";

// TODO, rather than have this unsafe method access, we could use a gdscript based
//       getter which performs the conversion from Variant to PackedByteArray
    code_ += "# To maintain reference counted PackedByteArray the gdextension type for data";
    code_ += "# must be Variant, which triggers these two notices";
    code_ += "@warning_ignore_start('unsafe_method_access')";
    code_ += "@warning_ignore_start('unsafe_call_argument')";
    code_ += "";

    // Include Files
    include_map[parser_.root_struct_def_->file] = "";
    for (const auto &[include_path, include_file] : parser_.GetIncludedFiles()) {
      if (include_path == "godot.fbs") continue;

      // import path is the default generated script path for that schema
      auto import_path = GeneratedFileName("", StripExtension(include_path), opts_);
      auto schema_file = StripPath(include_path);
      auto schema_ident = StripExtension(schema_file);

      code_.SetValue("IMPORT_PATH", import_path);
      code_.SetValue("SCHEMA_FILE", schema_file);
      code_.SetValue("SCHEMA_IDENT", schema_ident + "_schema");

      // Included files are in the form:
      // const <schema_ident>_schema = preload("<import_path>")
      // const <ClassName> = <schema_ident>_schema.<ClassName>

      // add to the include map for later use
      include_map[include_file] = schema_ident;

      code_ += "const {{SCHEMA_IDENT}} = preload('{{IMPORT_PATH}}')";

      // Add the enums from the included files as const values
      // TODO Only add the items we use
      for (const auto &[enum_ident, enum_def] : parser_.enums_.dict) {
        if (const auto &enum_file = enum_def->file; enum_file != include_file) {
          continue;
        }

        code_.SetValue("ENUM_IDENT", EscapeKeyword(enum_def->name));
        code_ += "const {{ENUM_IDENT}} = {{SCHEMA_IDENT}}.{{ENUM_IDENT}}";
      }
      // Add the structs and tables from the included files as const values
      // TODO Only add the items we use
      for (const auto &[struct_ident, struct_def] : parser_.structs_.dict) {
        if (const auto &struct_file = struct_def->file;
            struct_file != include_file) {
          continue;
        }
        code_.SetValue("STRUCT_IDENT", EscapeKeyword(struct_def->name));
        code_ += "const {{STRUCT_IDENT}} = {{SCHEMA_IDENT}}.{{STRUCT_IDENT}}";
      }
      code_ += "";
    }

    // Generate get_root convenience function to get the root table without
    // having to pass its position
    if (const auto &include_ident = include_map[parser_.root_struct_def_->file];
        include_ident.length() > 0) {
      code_.SetValue("INCLUDE_IDENT", include_ident + ".");
    } else {
      code_.SetValue("INCLUDE_IDENT", "");
    }
    code_.SetValue("ROOT_STRUCT", EscapeKeyword(parser_.root_struct_def_->name));
    code_ += "static func get_root( _bytes : PackedByteArray ) -> {{ROOT_STRUCT}}:";
    code_.IncrementIdentLevel();
    code_ += "return {{INCLUDE_IDENT}}get_{{ROOT_STRUCT}}( _bytes, _bytes.decode_u32(0) )";
    code_.DecrementIdentLevel();
    code_ += "";

    // Generate code for all the enum declarations.
    for (const auto &enum_def : parser_.enums_.vec) {
      if (!enum_def->generated) { GenEnum(*enum_def); }
    }

    // Generate code for all structs, then all tables.
    for (const auto &struct_def : parser_.structs_.vec) {
      if (struct_def->fixed && !struct_def->generated) {
        GenStructCreate(*struct_def);
        GenStruct(*struct_def);
      }
    }

    for (const auto &struct_def : parser_.structs_.vec) {
      if (!struct_def->fixed && !struct_def->generated) {
        GenTable(*struct_def);
        GenTableBuilder(*struct_def);
      }
    }

    // Generate the static get_<> functions for the structs and then tables
    for (const auto &struct_def : parser_.structs_.vec) {
      if (struct_def->fixed && !struct_def->generated) {
        GenStructGet(*struct_def);
      }
    }
    for (const auto &struct_def : parser_.structs_.vec) {
      if (!struct_def->fixed && !struct_def->generated) {
        GenStructGet( *struct_def );
        GenTableCreate(*struct_def);
      }
    }

    // Optional: UnPackObject API
    if ( opts_.generate_object_based_api ) {
      for (const auto &struct_def : parser_.structs_.vec) {
        if (!struct_def->fixed && !struct_def->generated) {
          const Value *godot_type = struct_def->attributes.Lookup("godot_type");
          if ( godot_type == nullptr ) {continue;}
          code_.SetValue("OBJECT_TYPE", godot_type->constant);
          code_.SetValue("OBJECT_NAME", ConvertCase(godot_type->constant, Case::kAllLower) );
          GenUnPackObject( *struct_def );
        }
      }
    }

    const auto final_code = code_.ToString();

    // Save the file
    return SaveFile(file_path.c_str(), final_code, false);
  }

  /*MARK: private :
  ║          _          _         _
  ║ _ __ _ _(_)_ ____ _| |_ ___  (_)
  ║| '_ \ '_| \ V / _` |  _/ -_)  _
  ║| .__/_| |_|\_/\__,_|\__\___| (_)
  ╙|_|──────────────────────────────*/
 private:
  CodeWriter code_;
  const IDLOptionsGdscript opts_;
  const CommentConfig comment_config{
    nullptr,
    "#",
    nullptr
  };

  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> builtin_structs;
  std::unordered_map<std::string, std::string> include_map;
  std::unordered_set<std::string> packed_structs;

  // List of all the includes
  std::vector<gdIncludeDef> include_defs;

  // maps the def->file to an include_def for fast lookup
  std::unordered_map<std::string, gdIncludeDef*> include_fast;

  // maps the flatbuffer type to an include
  std::unordered_map<std::string, gdIncludeDef *> type_include;

  template<class... args>
  static std::string strcat(const std::string &head, const args&... tail) {
    std::ostringstream os;
    os << head;
    (os << ... << tail);
    return os.str();
  }

  /*MARK: MapIncludeTypes
  ║ __  __           ___         _         _    _____
  ║|  \/  |__ _ _ __|_ _|_ _  __| |_  _ __| |__|_   _|  _ _ __  ___ ___
  ║| |\/| / _` | '_ \| || ' \/ _| | || / _` / -_)| || || | '_ \/ -_|_-<
  ║|_|  |_\__,_| .__/___|_||_\__|_|\_,_\__,_\___||_| \_, | .__/\___/__/
  ╙────────────|_|───────────────────────────────────|__/|_|────────────*/
  void MapIncludedTypes() {
    // resize the vector before putting things inside
    include_defs.resize( parser_.GetIncludedFiles().size() + 1);

    // add the current file as it wont be in the included list otherwise.
    std::string this_file = parser_.file_being_parsed_;
    gdIncludeDef *this_def = &include_defs.emplace_back( gdIncludeDef{
      strcat("_",StripExtension(StripPath(this_file)), "_schema"),
      strcat("'", StripExtension(StripPath(this_file)), "_generated.gd'")
    });
    include_fast.emplace(this_file, this_def);

    // add the included files
    for (auto &[schema_name, filename] : parser_.GetIncludedFiles()) {
      if ( include_fast.find(filename) == include_fast.end() ) {
        gdIncludeDef *new_def = &include_defs.emplace_back( gdIncludeDef{
         strcat("_", StripExtension(StripPath(filename)), "_schema"),
         strcat("'", StripExtension(schema_name), "_generated.gd'")});
        include_fast.emplace(filename, new_def );
      }
    }

    // Search through all the types in the parser
    for( const Type* type : parser_.types_.vec ){
      // get the definition
      Definition *def;
      if ( type->struct_def ) { def = type->struct_def;
      } else if ( type->enum_def ) {
        def = type->enum_def;
      } else { continue; }

      // skip the godot.fbs convenience.
      if ( StripPath(def->file) == "godot.fbs" ){
        continue;
      }

      // map the type to an include_def
      if ( type_include.find(def->name) == type_include.end() ) {
        // find the include_def
        auto inc = include_fast.find(def->file);
        if ( inc == include_fast.end() ) {
          printf("Error: Failed to match 'def->file' to an include\n");
          printf("\t%s : %s\n", def->name.c_str(), def->file.c_str());
          continue;
        }
        // Add the mapping.
        type_include.emplace(def->name, inc->second);
      }
    }
  }

  // returns a map like
  // [include_name, "const <include_name>  = preload( <include_path> )"]
  // with fields taken from gdInclude struct.
  std::unordered_map<std::string, std::string> CollectStructIncludes(const StructDef &struct_def) {
    // import files needed for external types
    std::unordered_map<std::string, std::string> struct_includes;

    // we need to include ourselves under some circumstances
    if ( opts_.generate_object_based_api ) {
      // The first element of the map is always the current file
      const gdIncludeDef *idef = type_include.begin()->second;
      struct_includes.emplace(
            idef->include_name,
            strcat("const ", idef->include_name, " = preload( ",
                   idef->include_path, " )"));
    }

    for (const FieldDef *field : struct_def.fields.vec) {
      const auto &type = field->value.type;
      Definition *def;
      if (type.struct_def) {
        def = type.struct_def;
      } else if (type.enum_def) {
        def = type.enum_def;
      } else {
        continue;
      }

      if ( StripPath(def->file) == "godot.fbs" ) {
        continue;
      }

      // find the include associated with the type
      auto itr = type_include.find(def->name);
      if (itr == type_include.end()) {
        struct_includes.emplace(
            def->name, strcat("# failed to find include for ", def->name,
                              " using: ", def->file));
        continue;
      }

      // only add if we havent seen it before.
      if (const gdIncludeDef *idef = itr->second;
          struct_includes.find(idef->include_name) == struct_includes.end()) {
        struct_includes.emplace(
            idef->include_name,
            strcat("const ", idef->include_name, " = preload( ",
                   idef->include_path, " )"));
          }
    }
    return struct_includes;
  }

  /*MARK: EscapeKeyword
  ║ ___                       _  __                           _
  ║| __|___ __ __ _ _ __  ___| |/ /___ _  ___ __ _____ _ _ __| |
  ║| _|(_-</ _/ _` | '_ \/ -_) ' </ -_) || \ V  V / _ \ '_/ _` |
  ║|___/__/\__\__,_| .__/\___|_|\_\___|\_, |\_/\_/\___/_| \__,_|
  ╙────────────────|_|─────────────────|__/─────────────────────*/
  std::string EscapeKeyword(const std::string &name) const {
    return keywords.find(name) == keywords.end() ? name : name + "_";
  }

  /*MARK: IsBuiltin
  ║ ___    ___      _ _ _   _
  ║|_ _|__| _ )_  _(_) | |_(_)_ _
  ║ | |(_-< _ \ || | | |  _| | ' \
  ║|___/__/___/\_,_|_|_|\__|_|_||_|
  ╙────────────────────────────────*/
  bool IsBuiltinStruct(const Type &type) {
    return type.struct_def != nullptr &&
           builtin_structs.find(type.struct_def->name) != builtin_structs.end();

    //FIXME use this instead?  if ( StripPath(def->file) == "godot.fbs" )
  }

  /*MARK: IsIncluded
  ║ ___    ___         _         _        _
  ║|_ _|__|_ _|_ _  __| |_  _ __| |___ __| |
  ║ | |(_-<| || ' \/ _| | || / _` / -_) _` |
  ║|___/__/___|_||_\__|_|\_,_\__,_\___\__,_|
  ╙─────────────────────────────────────────*/
  bool IsIncluded(const Type &type) const {
    Definition *def;
    if ( type.struct_def ) { def = type.struct_def;
    } else if ( type.enum_def ) {
      def = type.enum_def;
    } else { return false; }

    return def->file != parser_.root_struct_def_->file;
  }

  /*MARK: GetInclude
  ║  ___     _   ___         _         _
  ║ / __|___| |_|_ _|_ _  __| |_  _ __| |___
  ║| (_ / -_)  _|| || ' \/ _| | || / _` / -_)
  ║ \___\___|\__|___|_||_\__|_|\_,_\__,_\___|
  ╙──────────────────────────────────────────*/
  std::string GetInclude(const Type &type) {
    // get the definition
    Definition *def;
    if ( type.struct_def ) { def = type.struct_def;
    } else if ( type.enum_def ) {
      def = type.enum_def;
    } else { return ""; }

    // skip the godot.fbs convenience.
    if (StripPath(def->file) == "godot.fbs") { return ""; }

    const auto itr = type_include.find(def->name);
    if ( itr == type_include.end() ) {
      return "";
    }

    return itr->second->include_name + ".";
  }

  /*MARK: Name
  ║ _  _
  ║| \| |__ _ _ __  ___
  ║| .` / _` | '  \/ -_)
  ║|_|\_\__,_|_|_|_\___|
  ╙─────────────────────*/
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
    } else if (opts_.cpp_object_api_field_case_style == IDLOptions::CaseStyle_Lower) {
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

  std::string Name(const EnumVal &ev) const {
    return EscapeKeyword(ev.name);
  }

  /*MARK: GetGodotType
  ║  ___     _    ___         _     _  _____
  ║ / __|___| |_ / __|___  __| |___| ||_   _|  _ _ __  ___
  ║| (_ / -_)  _| (_ / _ \/ _` / _ \  _|| || || | '_ \/ -_)
  ║ \___\___|\__|\___\___/\__,_\___/\__||_| \_, | .__/\___|
  ╙─────────────────────────────────────────|__/|_|────────*/
  std::string GetGodotType(const Type &type) {
    if (IsEnum(type)) { return EscapeKeyword(type.enum_def->name); }
    if (IsStruct(type) || IsTable(type)) {
      if (IsBuiltinStruct(type)) { return type.struct_def->name; }
      return EscapeKeyword(type.struct_def->name);
    }
    if (IsSeries(type)) { return gdArrayType(type.element); }
    return gdType( type.base_type );
  }

  /*MARK: GenComment
  ║  ___          ___                         _
  ║ / __|___ _ _ / __|___ _ __  _ __  ___ _ _| |_
  ║| (_ / -_) ' \ (__/ _ \ '  \| '  \/ -_) ' \  _|
  ║ \___\___|_||_\___\___/_|_|_|_|_|_\___|_||_\__|
  ╙───────────────────────────────────────────────*/
  void GenComment(const std::vector<std::string> &dc) {
    if (dc.empty()) return;
    std::string comment;
    flatbuffers::GenComment(dc, &comment, &comment_config, "");
    comment.pop_back(); // get rid of the newline.
    code_ += comment;
  }


  /*MARK: GenDebugDict
  ║  ___          ___      _              ___  _    _
  ║ / __|___ _ _ |   \ ___| |__ _  _ __ _|   \(_)__| |_
  ║| (_ / -_) ' \| |) / -_) '_ \ || / _` | |) | / _|  _|
  ║ \___\___|_||_|___/\___|_.__/\_,_\__, |___/|_\__|\__|
  ╙─────────────────────────────────|___/───────────────*/
  void GenDebugDict(const StructDef &struct_def) {
    // There are only two options on how we got here
    // Either we are a table, or we are a struct.
    // Generate Pretty Printer
    code_ += "func debug() -> Dictionary:";
    code_.IncrementIdentLevel();
    code_ += "var d : Dictionary = {}";
    code_ += "d['buffer_size'] = bytes.size()";
    code_ += "d['start'] = start";

    // IF we are a table, then we have a vtable.
    if (struct_def.fixed) {  // Then we are a struct
      code_ += "d[{{FIELD_NAME}}] = {{FIELD_NAME}}()";
    } else {
      code_ += "d['vtable_offset'] = bytes.decode_s32( start )";
      code_ += "d['vtable_start'] = d.start - d.vtable_offset";
      code_ += "d['vtable'] = Dictionary()";
      code_ += "d.vtable['vtable_bytes'] = bytes.decode_u16( d.vtable_start )";
      code_ += "d.vtable['table_size'] = bytes.decode_u16( d.vtable_start + 2 )";
      code_ += "";
      code_ += "for i : int in ((d.vtable.vtable_bytes / 2) - 2):";
      code_.IncrementIdentLevel();
      code_ += "var keys : Array = vtable.keys()";
      code_ += "var offsets : Array = vtable.values()";
      code_ += "d.vtable[keys[i]] = bytes.decode_u16( d.vtable_start + offsets[i] )";
      code_.DecrementIdentLevel();
      code_ += "";
    }

    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      Type field_type = field->value.type;
      Type element_type;
      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("FIELD_TYPE", GetGodotType(field_type));
      if (IsSeries(field_type)) {
        element_type = field_type.VectorType();
        code_.SetValue("ELEMENT_TYPE", TypeName(field_type.element));
      }

      code_.SetValue("OFFSET_NAME", "VT_" + ConvertCase(Name(*field), Case::kAllUpper));

      code_ += "# {{FIELD_NAME}}:{{FIELD_TYPE}} \\";
      code_ += field->IsRequired() ? "(required)" : "";

      // Field Types:
      code_.SetValue("DICT", Name(*field) + "_dict");
      code_ += "var {{DICT}} : Dictionary = {'type':'{{FIELD_TYPE}}'}";
      code_ += "{{DICT}}['offset'] = get_field_offset( vtable.{{OFFSET_NAME}} )";

      if (!struct_def.fixed) {
        // If we are a table, then all fields are optional.
        code_ += "if {{FIELD_NAME}}_is_present():";
        code_.IncrementIdentLevel();
      }

      // Scalar
      if (IsScalar(field_type.base_type)) {
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Struct
      else if (IsStruct(field_type)) {
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Table
      else if (IsTable(field_type)) {
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}().debug()";
      }
      // TODO * Union
      else if (IsUnion(field_type)) {
        code_ += "pass # TODO Union";
      }
      // String
      else if (IsString(field_type)) {
        code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
      }
      // Vector of
      else if (IsVector(field_type)) {
        code_ += "{{DICT}}['type'] = '{{FIELD_TYPE}} of {{ELEMENT_TYPE}}'";
        code_ += "{{DICT}}['start'] = get_field_start( vtable.{{OFFSET_NAME}} )";
        code_ += "{{DICT}}['size'] = bytes.decode_u32( get_field_start( vtable.{{OFFSET_NAME}} ) )";
        // Scalar
        if (IsScalar(field_type.element)) {
          code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
        }
        // Struct | Table
        else if (IsStruct(element_type) || IsTable(element_type)) {
          //FIXME, this fails when the array type is a Packed type due to the missing map functionn.
          code_ += "{{DICT}}['value'] = 'FIXME, Packed type arrays break what was here.'";
          // code_ += "{{DICT}}['value'] = {{FIELD_NAME}}().map(";
          // code_.IncrementIdentLevel();
          // code_ += "func( element : FlatBuffer ) -> Dictionary:";
          // code_.IncrementIdentLevel();
          // code_ += "\treturn element.debug() if element else null";
          // code_.DecrementIdentLevel();
          // code_.DecrementIdentLevel();
          // code_ += ")";
        }
        // String
        if (IsString(element_type)) {
          code_ += "{{DICT}}['value'] = {{FIELD_NAME}}()";
        }
        // TODO     - Vector
      }

      // TODO * Vector of Union
      // TODO * Fixed length Array

      if (!struct_def.fixed) {
        // Come back down after : if {{FIELD_NAME}}_is_present():
        code_.DecrementIdentLevel();
        code_ += "";
      }
      code_ += "d['{{FIELD_NAME}}'] = {{DICT}}";
    }
    code_ += "return d ";
    code_.DecrementIdentLevel();
    code_ += "";
  }


  /*MARK: GenDefinitionDebug
  ║  ___          ___       __ _      _ _   _          ___      _
  ║ / __|___ _ _ |   \ ___ / _(_)_ _ (_) |_(_)___ _ _ |   \ ___| |__ _  _ __ _
  ║| (_ / -_) ' \| |) / -_)  _| | ' \| |  _| / _ \ ' \| |) / -_) '_ \ || / _` |
  ║ \___\___|_||_|___/\___|_| |_|_||_|_|\__|_\___/_||_|___/\___|_.__/\_,_\__, |
  ╙──────────────────────────────────────────────────────────────────────|___/-*/
  void GenDefinitionDebug(const Definition *def) {
    code_ += "# Definition Base Class";

    // std::string name;
    code_ += "#  name = " + def->name;

    // std::string file;
    code_ += "#  file = " + def->file;

    // std::vector<std::string> doc_comment;
    code_ += "#  doc_comment = ?";  // + def->doc_comment;

    // SymbolTable<Value> attributes;
    code_ += "#  attributes = ??";  // + def->attributes;

    // bool generated;  // did we already output code for this definition?
    code_ += "#  generated = \\";
    code_ += def->generated ? "true" : "false";

    // Namespace *defined_namespace;  // Where it was defined.
    code_ += "#  defined_namespace = ";  // + def->defined_namespace;

    // // For use with Serialize()
    // uoffset_t serialized_location;
    code_ +=
        "#  serialized_location = " + NumToString(def->serialized_location);

    // int index;  // Inside the vector it is stored.
    code_ += "#  index = " + NumToString(def->index);

    // int refcount;
    code_ += "#  refcount = " + NumToString(def->refcount);

    // const std::string *declaration_file;
    if ( def->declaration_file ) {
      code_ += "#  declaration_file = " + *def->declaration_file;
    } else {
      code_ += "#  declaration_file = NULL";
    }

    code_ += "#  ---- ";
  }

  void GenEnumDebug(const EnumDef *enum_def) {
    GenDefinitionDebug( enum_def );
    code_ += "# EnumDev Debug";
    code_ += "# AllFlags(): ??";
    // code_ += enum_def->AllFlags();

    code_ += "# MinValue(): \\";
    const auto min = enum_def->MinValue();
    code_ += min->name + " = " + enum_def->ToString(*min);

    code_ += "# MaxValue(): \\";
    const auto max = enum_def->MaxValue();
    code_ += max->name + " = " + enum_def->ToString(*max);

    code_ += "# size(): \\";
    code_ += NumToString( enum_def->size() );

    //   const std::vector<EnumVal *> &Vals() const { return vals.vec; }

    code_ += "# is_union: \\";
    code_ += enum_def->is_union ? "true" : "false";

    // Type is a union which uses type aliases where at least one type is
    // available under two different names.
    code_ += "# uses_multiple_type_instances: \\";
    code_ += enum_def->uses_multiple_type_instances ? "true" : "false";

    code_ += "# underlying_type: \\";
    code_ += TypeName(enum_def->underlying_type.base_type);

  }


  void GenTypeDebug( const Type &type ) {
    //    BaseType base_type;
    code_ += "#  base_type: \\";
    code_ += TypeName(type.base_type);

    //    BaseType element;       // only set if t == BASE_TYPE_VECTOR or
    //                            // BASE_TYPE_VECTOR64
    code_ += "#  element: \\";
    code_ += TypeName(type.element);

    //    StructDef *struct_def;  // only set if t or element ==
    //    BASE_TYPE_STRUCT
    code_ += "#  struct_def: \\";
    code_ += type.struct_def ? "exists" : "<null>";
    //    EnumDef *enum_def;      // set if t == BASE_TYPE_UNION /
    //    BASE_TYPE_UTYPE,
    //                            // or for an integral type derived from an
    //                            enum.
    code_ += "#  enum_def: \\";
    code_ += type.enum_def ? "exists" : "<null>";

    //    uint16_t fixed_length;  // only set if t == BASE_TYPE_ARRAY
    code_ += "#  fixed_length: " + NumToString(type.fixed_length);
    if (type.base_type == BASE_TYPE_ARRAY) {
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

  }

  /*MARK: GenFieldDebug
  ║  ___          ___ _     _    _ ___      _
  ║ / __|___ _ _ | __(_)___| |__| |   \ ___| |__ _  _ __ _
  ║| (_ / -_) ' \| _|| / -_) / _` | |) / -_) '_ \ || / _` |
  ║ \___\___|_||_|_| |_\___|_\__,_|___/\___|_.__/\_,_\__, |
  ╙──────────────────────────────────────────────────|___/*/
  void GenFieldDebug(const FieldDef &field) {
    code_.SetValue("FIELD_NAME", Name(field));
    code_ += "# GenFieldDebug for: '{{FIELD_NAME}}'";
    code_ += "#FieldDef {";

    // FieldDef is derived from Definition
    GenDefinitionDebug(&field);

    //  bool deprecated;// Field is allowed to be present in old data, but can't
    //  be.
    //                  // written in new data nor accessed in new code.
    code_ += "#  deprecated = \\";
    code_ += field.deprecated ? "true" : "false";

    //    bool key;         // Field functions as a key for creating sorted
    //    vectors.
    code_ += "#  key = \\";
    code_ += field.key ? "true" : "false";

    //    bool shared;  // Field will be using string pooling (i.e.
    //    CreateSharedString)
    //                         // as default serialisation behaviour if field is
    //                         a string.
    code_ += "#  shared = \\";
    code_ += field.shared ? "true" : "false";

    //    bool native_inline;  // Field will be defined inline (instead of as a
    //    pointer)
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

    const auto &type = field.value.type;
    code_ += "#FieldDef.Value.Type {";
    GenTypeDebug( type );
    code_ += "#}";

    if (IsTable(type) && field.value.type.struct_def) {
      const StructDef *struct_def = type.struct_def;

      code_ += "#FieldDef.Value.Type.StructDef {";

      // StructDef is derived from Definition
      GenDefinitionDebug(struct_def);

      // SymbolTable<FieldDef> fields;
      code_ += "#  fields = ? (SymbolTable<FieldDef>)";

      // bool fixed;       // If it's struct, not a table.
      code_ += "#  fixed = \\";
      code_ += struct_def->fixed ? "true" : "false";

      // bool predecl;     // If it's used before it was defined.
      code_ += "#  predecl = \\";
      code_ += struct_def->predecl ? "true" : "false";

      // bool sortbysize;  // Whether fields come in the declaration or size
      // order.
      code_ += "#  sortbysize = \\";
      code_ += struct_def->sortbysize ? "true" : "false";

      // bool has_key;     // It has a key field.
      code_ += "#  has_key = \\";
      code_ += struct_def->has_key ? "true" : "false";

      // size_t minalign;  // What the whole object needs to be aligned to.
      code_ += "#  minalign = " + NumToString(struct_def->minalign);

      // size_t bytesize;  // Size if fixed.
      code_ += "#  bytesize = " + NumToString(struct_def->bytesize);

      // flatbuffers::unique_ptr<std::string> original_location;
      code_ += "#  original_location = \\";
      code_ += struct_def->original_location.get() ? *struct_def->original_location : "";

      // std::vector<voffset_t> reserved_ids;
      code_ += "#  reserved_ids = ? (std::vector<voffset_t>)";
    }

    else if (IsEnum(type)) {
      const auto &enum_def = *type.enum_def;
      code_ += "#FieldDef.Value.EnumDef {";

      // StructDef is derived from Definition
      GenDefinitionDebug(&enum_def);

      //      bool is_union;
      code_ += "#  is_union: \\";
      code_ += enum_def.is_union ? "true" : "false";
      // type is a union which uses type aliases where at least one type is
      // available under two different names.
      // bool uses_multiple_type_instances;
      code_ += "#  uses_multiple_type_instances: \\";
      code_ += enum_def.uses_multiple_type_instances ? "true" : "false";
      code_ += "#  MinValue() = " + NumToString(enum_def.MinValue());
      code_ += "#  MaxValue() = " + NumToString(enum_def.MaxValue());
      //      Type underlying_type;
      code_ += "#}";
    }
    else if ( IsSeries(type) ) {
      if (type.struct_def) {
        code_ += "#FieldDef.Value.Type.struct_def = {";
        GenDefinitionDebug(type.struct_def);
        code_ += "#}";
      }
    }
  }

   /*MARK: Gen Enum
   ║  ___            ___
   ║ / __|___ _ _   | __|_ _ _  _ _ __
   ║| (_ / -_) ' \  | _|| ' \ || | '  \
   ║ \___\___|_||_| |___|_||_\_,_|_|_|_|
   ╙─────────────────────────────────────*/
  // Generate an enum declaration
  void GenEnum(const EnumDef &enum_def) {
    code_.SetValue("ENUM_NAME", Name(enum_def));

    GenComment(enum_def.doc_comment);
    code_ += "enum " + Name(enum_def) + " {";
    code_.IncrementIdentLevel();

    code_.SetValue("SEP", ",");
    auto add_sep = false;
    for (const auto enum_val : enum_def.Vals()) {
      if (add_sep) code_ += "{{SEP}}";
      GenComment(enum_val->doc_comment);
      auto key = ConvertCase(Name(*enum_val), Case::kAllUpper);
      code_.SetValue("KEY", key);
      code_.SetValue("VALUE", enum_def.ToString(*enum_val));
      code_ += "{{KEY}} = {{VALUE}}\\";
      add_sep = true;
    }
    code_.DecrementIdentLevel();
    code_ += "";
    code_ += "}\n";
  }

  /*MARK: GenStructGet
  ║  ___          ___ _               _    ___     _
  ║ / __|___ _ _ / __| |_ _ _ _  _ __| |_ / __|___| |_
  ║| (_ / -_) ' \\__ \  _| '_| || / _|  _| (_ / -_)  _|
  ║ \___\___|_||_|___/\__|_|  \_,_\__|\__|\___\___|\__|
  ╙────────────────────────────────────────────────────*/
  void GenStructGet(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ += "static func get_{{STRUCT_NAME}}( _bytes : PackedByteArray, _start : int = 0 ) -> {{STRUCT_NAME}}:";
    code_.IncrementIdentLevel();
    code_ += "assert(not _bytes.is_empty())";
    code_ += "return {{STRUCT_NAME}}.new(_bytes, _start)";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenStructCreate
  ║  ___          ___ _               _    ___              _
  ║ / __|___ _ _ / __| |_ _ _ _  _ __| |_ / __|_ _ ___ __ _| |_ ___
  ║| (_ / -_) ' \\__ \  _| '_| || / _|  _| (__| '_/ -_) _` |  _/ -_)
  ║ \___\___|_||_|___/\__|_|  \_,_\__|\__|\___|_| \___\__,_|\__\___|
  ╙─────────────────────────────────────────────────────────────────*/
  void GenStructCreate(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ += "static func create_{{STRUCT_NAME}}(";
    code_.IncrementIdentLevel();
    code_.IncrementIdentLevel();
    code_.SetValue("SEP", ",");
    bool add_sep = false;
    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) continue;
      const auto type = field->value.type;
      if (add_sep) code_ += "{{SEP}}";
      code_.SetValue("PARAM_NAME", Name(*field));
      code_.SetValue("DEFAULT_VALUE", "default");
      code_.SetValue("INCLUDE", IsIncluded(type) ? GetInclude(type) : "");
      code_.SetValue("PARAM_TYPE", GetGodotType(type));
      code_ += "_{{PARAM_NAME}} : {{INCLUDE}}{{PARAM_TYPE}}\\";
      // TODO add default value if possible.
      add_sep = true;
    }
    code_ += " ) -> {{STRUCT_NAME}} :";
    code_.DecrementIdentLevel();

    // Allocate the memory required.
    code_ += "var val : {{STRUCT_NAME}} = {{STRUCT_NAME}}.new()";

    // for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) :
    // 1; size; size /= 2 ) {
    //   for( auto it = struct_def.fields.vec.rbegin(); it !=
    //   struct_def.fields.vec.rend(); ++it ) {
    //     const auto &field = **it;
    //     if( not field.deprecated and ( not struct_def.sortbysize or size
    //     == SizeOf( field.value.type.base_type )) ) {
    //       if( not IsStruct( field.value.type ) ) continue;
    //       // FIXME this might also include fixed sized arrays
    //       code_.SetValue( "FIELD_NAME", Name( field ) );
    //       code_ += "# Create {{FIELD_NAME}}";
    //       code_ += "var {{FIELD_NAME}}_offset";
    //     }
    //   }
    // }

    // Create* function body
    for (size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2) {
      for (auto it = struct_def.fields.vec.rbegin(); it != struct_def.fields.vec.rend(); ++it) {
        if (const auto &field = **it; !field.deprecated &&
            (!struct_def.sortbysize || size == SizeOf(field.value.type.base_type))) {
          code_.SetValue("FIELD_NAME", Name(field));
          code_ += "val.{{FIELD_NAME}} = _{{FIELD_NAME}};";
        }
      }
    }
    //FIXME assign all the values here.
    code_ += "return val";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenStructArray
  ║  ___          ___ _               _     _
  ║ / __|___ _ _ / __| |_ _ _ _  _ __| |_  /_\  _ _ _ _ __ _ _  _
  ║| (_ / -_) ' \\__ \  _| '_| || / _|  _|/ _ \| '_| '_/ _` | || |
  ║ \___\___|_||_|___/\__|_|  \_,_\__|\__/_/ \_\_| |_| \__,_|\_, |
  ╙──────────────────────────────────────────────────────────|__/-*/
  void GenStructArrayGet(const FieldDef &field [[maybe_unused]]) {
    // Already defined
    // STRUCT_NAME, NUM_BYTES, FIELD_NAME, OFFSET, GODOT_TYPE, INCLUDE
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX, FIXED_LENGTH

    const Type element = field.value.type.VectorType();
    code_ += "func get_{{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    if ( IsScalar( element.base_type  )) {
      code_.SetValue("PBA_CONVERT", gdPBAConvert(element.base_type));
      code_ += "# TODO Scalar Type";
      code_ += "return bytes.slice({{OFFSET}}, \\";
      code_ += "{{OFFSET}} + {{FIXED_LENGTH}} * {{ELEMENT_SIZE}}).{{PBA_CONVERT}}()";
    } else if ( IsBuiltinStruct( element ) ) {
      code_ += "# TODO Builtin Type";
      code_ += "return []";
    } else if ( IsStruct(element) ) {
      code_ += "# TODO Struct Type";
      code_ += "return []";
    } else {
      code_ += "# FIXME Unknown Type";
      code_ += "return []";
    }

    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenStructArraySet(const FieldDef &field [[maybe_unused]]) {
    // Already defined
    // STRUCT_NAME, NUM_BYTES, FIELD_NAME, OFFSET, GODOT_TYPE, INCLUDE
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX, FIXED_LENGTH
    const Type element = field.value.type.VectorType();
    code_ += "func set_{{FIELD_NAME}}( _v : {{GODOT_TYPE}} ):";
    code_.IncrementIdentLevel();
    if ( IsScalar( element.base_type  )) {
      code_ += "# TODO Scalar Type";
    } else if ( IsBuiltinStruct( element ) ) {
      code_ += "# TODO Builtin Type";
    } else if ( IsStruct(element) ) {
      code_ += "# TODO Struct Type";
    } else {
      code_ += "return null";
      code_ += "# FIXME Unknown Type";
    }
    code_ += "pass";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenStructArrayAt(const FieldDef &field [[maybe_unused]]) {
    // Already defined
    // STRUCT_NAME, NUM_BYTES, FIELD_NAME, OFFSET, GODOT_TYPE, INCLUDE
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX, FIXED_LENGTH
    const Type element = field.value.type.VectorType();
    code_ += "func at_{{FIELD_NAME}}( idx : int ) -> {{ELEMENT_INCLUDE}}{{ELEMENT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "assert( idx < {{FIXED_LENGTH}})";
    if ( IsScalar( element.base_type  )) {
      code_.SetValue("PBA_SUFFIX", gdPBASuffix(element.base_type));
      code_ += "return bytes.decode_{{PBA_SUFFIX}}( bytes, {{OFFSET}} + idx * {{ELEMENT_SIZE}})";
    } else if ( IsBuiltinStruct( element ) ) {
      code_ += "return bytes.decode_{{ELEMENT_TYPE}}( bytes, {{OFFSET}} + idx * {{ELEMENT_SIZE}})";
    } else if ( IsStruct(element) ) {
      code_ += "return {{ELEMENT_INCLUDE}}get_{{ELEMENT_TYPE}}( bytes, {{OFFSET}} + idx * {{ELEMENT_SIZE}})";
    } else {
      code_ += "return null";
      code_ += "# FIXME Unknown Type";
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenStructInit
  ║  ___          ___ _               _   ___      _ _
  ║ / __|___ _ _ / __| |_ _ _ _  _ __| |_|_ _|_ _ (_) |_
  ║| (_ / -_) ' \\__ \  _| '_| || / _|  _|| || ' \| |  _|
  ║ \___\___|_||_|___/\__|_|  \_,_\__|\__|___|_||_|_|\__|
  ╙──────────────────────────────────────────────────────*/
  // Init function to prevent a rather spicy footgun
  void GenStructInit(const StructDef &struct_def[[maybe_unused]]) {
    code_ += "func _init( bytes_ : PackedByteArray = [], start_ : int = 0) -> void:";
    code_.IncrementIdentLevel();
    code_ += "if bytes_.is_empty(): ";
    code_.IncrementIdentLevel();
    code_ += "bytes = PackedByteArray()";
    code_ += "bytes.resize( size )";
    code_.DecrementIdentLevel();
    code_ += "else:";
    code_.IncrementIdentLevel();
    code_ += "assert(start_ + size <= bytes_.size())";
    code_ += "bytes = bytes_; start = start_";
    code_.DecrementIdentLevel();
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenStructIncludes
  ║  ___          ___ _               _   ___         _         _
  ║ / __|___ _ _ / __| |_ _ _ _  _ __| |_|_ _|_ _  __| |_  _ __| |___ ___
  ║| (_ / -_) ' \\__ \  _| '_| || / _|  _|| || ' \/ _| | || / _` / -_|_-<
  ║ \___\___|_||_|___/\__|_|  \_,_\__|\__|___|_||_\__|_|\_,_\__,_\___/__/
  ╙──────────────────────────────────────────────────────────────────────*/
  void GenStructIncludes(const StructDef &struct_def) {
    auto struct_includes = CollectStructIncludes( struct_def );
    if (struct_includes.empty()) {
      return;
    }
    for ( auto &[type, include] : struct_includes ) {
      code_ += include;
    }
    code_ += "";
  }

  /*MARK: Gen Struct
  ║  ___            ___ _               _
  ║ / __|___ _ _   / __| |_ _ _ _  _ __| |_
  ║| (_ / -_) ' \  \__ \  _| '_| || / _|  _|
  ║ \___\___|_||_| |___/\__|_|  \_,_\__|\__|
  ╙──────────────────────────────────────────*/
  void GenStruct(const StructDef &struct_def) {
    // Generate class to access the structs fields
    // The generated classes are like a view into a PackedByteArray,
    // it decodes the data on access.
    GenComment(struct_def.doc_comment);

    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_.SetValue("NUM_BYTES", NumToString(struct_def.bytesize));

    // Generate the class definition
    code_ += "class {{STRUCT_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();

    // I need to preload all the dependencies for the class here
    GenStructIncludes( struct_def );

    // Add the fixed size
    code_ += "const size : int = {{NUM_BYTES}}";
    code_ += "";

    // Init function to prevent a rather spicy footgun
    GenStructInit(struct_def);

    for (const auto field : struct_def.fields.vec) {
      if (field->deprecated) {
        // Deprecated fields won't be accessible.
        // TODO generate deprecated field with flag?
        continue;
      }

      const auto &type = field->value.type;
      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("OFFSET", NumToString(field->value.offset));
      code_.SetValue("GODOT_TYPE", GetGodotType(type));
      code_.SetValue("INCLUDE", GetInclude(type));
      code_ += "# [================[ {{FIELD_NAME}} ]================]";
      if (field->IsScalar()) {
        code_.SetValue("PBASUFFIX", gdPBASuffix(type.base_type));
        code_ += "var {{FIELD_NAME}} : {{GODOT_TYPE}} :";
        code_.IncrementIdentLevel();
        code_ += "get(): return bytes.decode_{{PBASUFFIX}}(start + {{OFFSET}})\\";
        code_ += IsEnum(type) ? " as {{GODOT_TYPE}}" : "";
        code_ += "set(v): bytes.encode_{{PBASUFFIX}}(start + {{OFFSET}}, v)";
        code_.DecrementIdentLevel();
        code_ += "";
      } else if (IsStruct(type) && IsBuiltinStruct(type)) {
        code_ += "var {{FIELD_NAME}} : {{GODOT_TYPE}} :";
        code_.IncrementIdentLevel();
        code_ += "get(): return decode_{{GODOT_TYPE}}(start + {{OFFSET}})";
        code_ += "set(v): encode_{{GODOT_TYPE}}(start + {{OFFSET}}, v)";
        code_.DecrementIdentLevel();
        code_ += "";
      } else if (IsStruct(type)) {
        code_ += "var {{FIELD_NAME}} : {{GODOT_TYPE}} :";
        code_.IncrementIdentLevel();
        code_ += "get(): return {{INCLUDE}}get_{{GODOT_TYPE}}(bytes, start + {{OFFSET}})";
        code_ += "set(v): overwrite_bytes(v.bytes, v.start, start + {{OFFSET}}, v.size)";
        code_.DecrementIdentLevel();
        code_ += "";
      } else if (IsArray(type)){
        const Type element = type.VectorType();
        code_.SetValue("ARRAY_SIZE", NumToString(InlineSize(type)) );
        code_.SetValue("FIXED_LENGTH", NumToString( type.fixed_length ) );
        code_.SetValue("ELEMENT_INCLUDE", GetInclude(element) );
        code_.SetValue("ELEMENT_TYPE", GetGodotType(element));
        code_.SetValue("ELEMENT_SIZE", NumToString(InlineSize(element) ));

        code_ += "var {{FIELD_NAME}} : {{GODOT_TYPE}} :";
        code_.IncrementIdentLevel();
        code_ += "get = get_{{FIELD_NAME}}, set = set_{{FIELD_NAME}}";
        code_.DecrementIdentLevel();
        code_ += "";
        GenStructArrayGet( *field );
        GenStructArraySet( *field );
        GenStructArrayAt( *field );
      } else {
        code_ += "#TODO - Unhandled Type";
        code_ += "pass";
        GenFieldDebug(*field);
      }
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: Gen VTable
  ║  ___           __   _______     _    _
  ║ / __|___ _ _   \ \ / /_   _|_ _| |__| |___
  ║| (_ / -_) ' \   \ V /  | |/ _` | '_ \ / -_)
  ║ \___\___|_||_|   \_/   |_|\__,_|_.__/_\___|
  ╙─────────────────────────────────────────────*/
  void GenVtableEnums(const StructDef &struct_def) {
    // Generate field id constants.
    if (!struct_def.fields.vec.empty()) {
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
        code_.SetValue("OFFSET_NAME", "VT_" + ConvertCase(Name(*field), Case::kAllUpper));
        code_.SetValue("OFFSET_VALUE", NumToString(field->value.offset));
        if (sep) code_ += "{{SEP}}";
        code_ += "{{OFFSET_NAME}} = {{OFFSET_VALUE}}\\";
        sep = true;
      }
      code_.DecrementIdentLevel();
      code_ += "";  // end the line after the last element
      code_ += "}";
    }
    code_ += "";
  }

  /*MARK: GenFieldPresence
  ║  ___          ___ _     _    _ ___
  ║ / __|___ _ _ | __(_)___| |__| | _ \_ _ ___ ___ ___ _ _  __ ___
  ║| (_ / -_) ' \| _|| / -_) / _` |  _/ '_/ -_|_-</ -_) ' \/ _/ -_)
  ║ \___\___|_||_|_| |_\___|_\__,_|_| |_| \___/__/\___|_||_\__\___|
  ╙────────────────────────────────────────────────────────────────*/
  void GenPresenceFunc(const FieldDef &field) {
    // Generate presence funcs
    code_.SetValue("FIELD_NAME", Name(field));

    if (field.IsRequired()) {
      // Required fields are always accessible.
      code_ += "# {{FIELD_NAME}} is required\n";
    }
    code_.SetValue("OFFSET_NAME", "VT_" + ConvertCase(Name(field), Case::kAllUpper));
    code_ += "func {{FIELD_NAME}}_is_present() -> bool:";
    code_.IncrementIdentLevel();
    code_ += "return get_field_offset( vtable.{{OFFSET_NAME}} )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldVectorScalar
  ║  ___          ___ _     _    ___   __      _           ___          _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _/ __| __ __ _| |__ _ _ _
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_\__ \/ _/ _` | / _` | '_|
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_| |___/\__\__,_|_\__,_|_|
  ╙────────────────────────────────────────────────────────────────────────────────*/
  void GenFieldVectorScalarGet(const FieldDef &field) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    const auto &type = field.value.type;
    const Type element = type.VectorType();

    code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return []";
    code_ += "var array_size : int = bytes.decode_u32( array_start )";
    code_ += "array_start += 4";
    switch (element.base_type) {
      case BASE_TYPE_UTYPE:
      case BASE_TYPE_BOOL:
      case BASE_TYPE_UCHAR:
        code_ += "return bytes.slice( array_start, array_start + array_size )";
        code_.DecrementIdentLevel();
        code_ += "";
        break;
      case BASE_TYPE_CHAR:
      case BASE_TYPE_SHORT:
      case BASE_TYPE_USHORT:
      case BASE_TYPE_UINT:
      case BASE_TYPE_ULONG:
        code_ += "var array : Array";
        code_ += "if array.resize( array_size ) != OK: return []";
        code_ += "for i : int in array_size:";
        code_.IncrementIdentLevel();
        code_ += "array[i] = bytes.decode_{{PBASUFFIX}}( array_start + i * {{ELEMENT_SIZE}})";
        code_.DecrementIdentLevel();
        code_ += "# To return packed array types, the scalar elements have to be of an appropriate type.";
        code_ += "return array";
        code_.DecrementIdentLevel();
        code_ += "";
        break;
      case BASE_TYPE_INT:
      case BASE_TYPE_LONG:
      case BASE_TYPE_FLOAT:
      case BASE_TYPE_DOUBLE:
        code_.SetValue("PBA_CONVERT", gdPBAConvert(element.base_type));
        code_ += "var array_end : int = array_start + array_size * {{ELEMENT_SIZE}}";
        code_ += "return bytes.slice( array_start, array_end ).{{PBA_CONVERT}}()";
        code_.DecrementIdentLevel();
        code_ += "";
        break;
      default:
        // We shouldn't be here.
        if (opts_.gdscript_debug) {
          GenFieldDebug(field);
        }
    }
  }

  void GenFieldVectorScalarAt(const FieldDef &field) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    const auto &type = field.value.type;
    const Type element = type.VectorType();

    code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
    code_.IncrementIdentLevel();
    switch (element.base_type) {
      case BASE_TYPE_UTYPE:
      case BASE_TYPE_BOOL:
      case BASE_TYPE_UCHAR:
        code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
        code_ += "if not array_start: return 0";
        code_ += "array_start += 4";
        code_ += "return bytes[array_start + index]";
        code_.DecrementIdentLevel();
        code_ += "";
        break;
      case BASE_TYPE_CHAR:
      case BASE_TYPE_SHORT:
      case BASE_TYPE_USHORT:
      case BASE_TYPE_UINT:
      case BASE_TYPE_ULONG:
        code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
        code_ += "if not array_start: return 0";
        code_ += "array_start += 4";
        code_ += "return bytes.decode_{{PBASUFFIX}}( array_start + index * {{ELEMENT_SIZE}})";
        code_.DecrementIdentLevel();
        code_ += "";
        break;
      case BASE_TYPE_INT:
      case BASE_TYPE_LONG:
      case BASE_TYPE_FLOAT:
      case BASE_TYPE_DOUBLE:
        code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
        code_ += "if not array_start: return 0";
        code_ += "array_start += 4";
        code_ += "return bytes.decode_{{PBASUFFIX}}( array_start + index * {{ELEMENT_SIZE}})";
        code_.DecrementIdentLevel();
        code_ += "";
        return;
      default:
        // We shouldn't be here.
        if (opts_.gdscript_debug) {
          GenFieldDebug(field);
        }
    }
  }

  /*MARK: GenFieldVectorStruct
  ║  ___          ___ _     _    ___   __      _           ___ _               _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _/ __| |_ _ _ _  _ __| |_
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_\__ \  _| '_| || / _|  _|
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_| |___/\__|_|  \_,_\__|\__|
  ╙────────────────────────────────────────────────────────────────────────────────*/
  void GenFieldVectorStructGet(const FieldDef &field) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    const auto &type = field.value.type;
    const Type element = type.VectorType();
    const auto struct_def = element.struct_def;

    code_.SetValue("ELEMENT_SIZE", NumToString( struct_def->bytesize) );
    if (packed_structs.find(struct_def->name) != packed_structs.end()) {
      code_.SetValue("GODOT_TYPE", "Packed" + GetGodotType(element) + "Array");
    } else {
      code_.SetValue("GODOT_TYPE", "Array[" + GetGodotType(element) + "]");
    }
    code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();

    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return []";
    code_ += "var array_size : int = bytes.decode_u32( array_start )";
    code_ += "array_start += 4";
    code_ += "var array : {{GODOT_TYPE}}";
    code_ += "if array.resize( array_size ) != OK: return []";
    code_ += "for i : int in array_size:";
    code_.IncrementIdentLevel();

    if (IsBuiltinStruct(element)) {
      code_ += "array[i] = decode_{{ELEMENT_TYPE}}( array_start + i * {{ELEMENT_SIZE}})";
    } else {
      code_ +=
          "array[i] = {{ELEMENT_INCLUDE}}get_{{ELEMENT_TYPE}}"
          "(bytes, array_start + i * {{ELEMENT_SIZE}} )";
    }

    code_.DecrementIdentLevel();
    code_ += "return array";

    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenFieldVectorStructAt(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    const bool is_builtin_sruct = IsBuiltinStruct(field.value.type.VectorType());

    if( is_builtin_sruct ) {
      // TODO research whether default values for structs is viable?
      code_ += "func {{FIELD_NAME}}_at( idx : int ) -> {{ELEMENT_TYPE}}:";
    } else {
      code_ += "func {{FIELD_NAME}}_at( idx : int, into : {{ELEMENT_TYPE}} = null ) -> {{ELEMENT_TYPE}}:";
    }
    code_.IncrementIdentLevel();
    code_ += "var field_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "var array_size : int = bytes.decode_u32( field_start )";
    code_ += "var array_start : int = field_start + 4";
    code_ += "assert(field_start, 'Field is not present in buffer' )";
    code_ += "assert( idx < array_size, 'index is out of bounds')";
    code_ += "var relative_offset : int = array_start + idx * 4";
    code_ += "var offset : int = relative_offset + bytes.decode_u32( relative_offset )";
    if ( is_builtin_sruct ) {
      code_ += "return decode_{{ELEMENT_TYPE}}( offset )";
    }else {
      code_ += "if into:";
      code_.IncrementIdentLevel();
      code_ += "into.bytes = bytes";
      code_ += "into.start = relative_offset";
      code_ += "return into";
      code_.DecrementIdentLevel();
      code_ += "return _change_schema.get_ChangeFB( bytes, offset )";
      code_ += "return {{ELEMENT_INCLUDE}}get_{{ELEMENT_TYPE}}( bytes, offset )";;
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldVectorTable
  ║  ___          ___ _     _    ___   __      _          _____     _    _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ |_   _|_ _| |__| |___
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_|| |/ _` | '_ \ / -_)
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_|  |_|\__,_|_.__/_\___|
  ╙────────────────────────────────────────────────────────────────────────────*/
  void GenFieldVectorTableGet(const FieldDef &field) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    const auto &type = field.value.type;
    const Type element = type.VectorType();
    if ( IsIncluded(type) ) {
      // TODO Handle this case properly.

    }
    // func {{FIELD_NAME}}() -> Array|PackedArray
    code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return []";
    code_ += "var array_size : int = bytes.decode_u32( array_start )";
    code_ += "array_start += 4";
    code_ += "var array : Array";
    code_ += "if array.resize( array_size ) != OK: return []";
    code_ += "for i : int in array_size:";
    code_.IncrementIdentLevel();
    code_ += "var p : int = array_start + i * 4";
    if (IsBuiltinStruct(element)) {
      code_ += "array[i] = decode_{{ELEMENT_TYPE}}( p + bytes.decode_u32( p ) )";
    } else {
      code_ += "array[i] = {{ELEMENT_INCLUDE}}get_{{ELEMENT_TYPE}}( bytes, p + bytes.decode_u32( p ) )";
    }

    code_.DecrementIdentLevel();
    code_ += "return array";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldVectorString
  ║  ___          ___ _     _    ___   __      _           ___ _       _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _/ __| |_ _ _(_)_ _  __ _
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_\__ \  _| '_| | ' \/ _` |
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_| |___/\__|_| |_|_||_\__, |
  ╙──────────────────────────────────────────────────────────────────────────|___/-*/
  void GenFieldVectorStringGet(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    // func {{FIELD_NAME}}() -> Array|PackedArray
    code_ += "func {{FIELD_NAME}}() -> {{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return []";
    code_ += "var array_size : int = bytes.decode_u32( array_start )";
    code_ += "array_start += 4";
    code_ += "var array : {{GODOT_TYPE}}";
    code_ += "if array.resize( array_size ) != OK: return []";
    code_ += "for i : int in array_size:";
    code_.IncrementIdentLevel();
    code_ += "var idx : int = array_start + i * {{ELEMENT_SIZE}}";
    code_ += "var element_start : int = idx + bytes.decode_u32( idx )";
    code_ += "array[i] = decode_String( element_start )";
    code_.DecrementIdentLevel();
    code_ += "return array";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenFieldVectorStringAt(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    code_ += "func {{FIELD_NAME}}_at( index : int ) -> {{ELEMENT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return ''";
    code_ += "array_start += 4";
    code_ += "var string_start : int = array_start + index * {{ELEMENT_SIZE}}";
    code_ += "string_start += bytes.decode_u32( string_start )";
    code_ += "return decode_String( string_start )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldVectorUnion
  ║  ___          ___ _     _    ___   __      _           _   _      _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _| | | |_ _ (_)___ _ _
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_| |_| | ' \| / _ \ ' \
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_|  \___/|_||_|_\___/_||_|
  ╙──────────────────────────────────────────────────────────────────────────────*/
  void GenFieldVectorUnionGet(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    code_ += "# TODO GenFieldVectorUnionGet ";
    code_ += "# {{FIELD_NAME}} : {{GODOT_TYPE}} ";
    code_ += "";
    if (opts_.gdscript_debug) {
      GenFieldDebug(field);
    }
  }

  void GenFieldVectorUnionAt(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    code_ += "# TODO GenFieldVectorUnionAt ";
    code_ += "# {{FIELD_NAME}} : {{GODOT_TYPE}} ";
    code_ += "";
    if (opts_.gdscript_debug) {
      GenFieldDebug(field);
    }
  }

  /*MARK: GenFieldVectorSize
  ║  ___          ___ _     _    ___   __      _           ___ _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _/ __(_)______
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_\__ \ |_ / -_)
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_| |___/_/__\___|
  ╙─────────────────────────────────────────────────────────────────────*/
  void GenFieldVectorSize(const FieldDef &field [[maybe_unused]]) {
    // FIELD_NAME, GODOT_TYPE, INCLUDE were set in GenField
    // ELEMENT_INCLUDE, ELEMENT_TYPE, ELEMENT_SIZE, PBASUFFIX were set in GenFieldVector
    code_ += "func {{FIELD_NAME}}_size() -> int:";
    code_.IncrementIdentLevel();
    code_ += "var array_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not array_start: return 0";
    code_ += "return bytes.decode_u32( array_start )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldVector
  ║  ___          ___ _     _    ___   __      _
  ║ / __|___ _ _ | __(_)___| |__| \ \ / /__ __| |_ ___ _ _
  ║| (_ / -_) ' \| _|| / -_) / _` |\ V / -_) _|  _/ _ \ '_|
  ║ \___\___|_||_|_| |_\___|_\__,_| \_/\___\__|\__\___/_|
  ╙────────────────────────────────────────────────────────*/
  void GenFieldVector(const FieldDef &field) {
    // FIELD_NAME, OFFSET_NAME, GODOT_TYPE, INCLUDE were set in GenField
    const Type &type = field.value.type;

    // Vector of
    const Type element = type.VectorType();
    code_.SetValue("ELEMENT_INCLUDE", GetInclude(element) );
    code_.SetValue("ELEMENT_TYPE", GetGodotType(element));
    code_.SetValue("ELEMENT_SIZE", NumToString(SizeOf(element.base_type)));
    code_.SetValue("PBASUFFIX", gdPBASuffix(element.base_type));

    // The size is the same for all vector fields.
    GenFieldVectorSize(field);

    if (IsScalar(element.base_type)) {
      GenFieldVectorScalarGet( field );
      GenFieldVectorScalarAt( field );
    }
    else if (IsStruct(element)) {
      GenFieldVectorStructGet( field );
      GenFieldVectorStructAt( field );
    }
    else if (IsTable(element)) {
      GenFieldVectorTableGet( field );
      GenFieldVectorStructAt( field );
    }
    else if (IsString(element)) {
      GenFieldVectorStringGet( field );
      GenFieldVectorStringAt( field );
    }
    else if (IsUnion(element)) {
      GenFieldVectorUnionGet( field );
      GenFieldVectorUnionAt( field );
    }
    else {
      if (opts_.gdscript_debug) {
        GenFieldDebug(field);
      } else {
        code_.SetValue("TYPE_NAME", TypeName(type.base_type));
        code_ += "# NOTE: Unhandled field type in schema";
        code_ += "# {{FIELD_NAME}}:{{TYPE_NAME}}";
      }
    }
    // TODO Fixed length Array, is only appropriate for structs
    // TODO Dictionary
  }

  /*MARK: GenFieldScalar
  ║  ___          ___ _     _    _ ___          _
  ║ / __|___ _ _ | __(_)___| |__| / __| __ __ _| |__ _ _ _
  ║| (_ / -_) ' \| _|| / -_) / _` \__ \/ _/ _` | / _` | '_|
  ║ \___\___|_||_|_| |_\___|_\__,_|___/\__\__,_|_\__,_|_|
  ╙────────────────────────────────────────────────────────*/
  void GenFieldScalar( const FieldDef &field ) {
    const auto &type = field.value.type;
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    code_.SetValue("PBA_SUFFIX", gdPBASuffix(type.base_type));
    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var foffset : int = get_field_offset( vtable.{{OFFSET_NAME}} )";
    code_ += "if not foffset: return " + field.value.constant;
    code_ += "return bytes.decode_{{PBA_SUFFIX}}( start + foffset )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldStruct
  ║  ___          ___ _     _    _ ___ _               _
  ║ / __|___ _ _ | __(_)___| |__| / __| |_ _ _ _  _ __| |_
  ║| (_ / -_) ' \| _|| / -_) / _` \__ \  _| '_| || / _|  _|
  ║ \___\___|_||_|_| |_\___|_\__,_|___/\__|_|  \_,_\__|\__|
  ╙────────────────────────────────────────────────────────*/
  void GenFieldStruct( const FieldDef &field ) {
    const auto &type = field.value.type;
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    if (IsBuiltinStruct(type)) {
      code_ += "return get_{{GODOT_TYPE}}( vtable.{{OFFSET_NAME}} )";
    } else {
      code_.SetValue("INCLUDE", GetInclude(type));
      code_ += "var field_offset : int = get_field_offset( vtable.{{OFFSET_NAME}} )";
      code_ += "if not field_offset: return null";
      code_ += "return {{INCLUDE}}get_{{GODOT_TYPE}}( bytes, start + field_offset )";
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldTable
  ║  ___          ___ _     _    _ _____     _    _
  ║ / __|___ _ _ | __(_)___| |__| |_   _|_ _| |__| |___
  ║| (_ / -_) ' \| _|| / -_) / _` | | |/ _` | '_ \ / -_)
  ║ \___\___|_||_|_| |_\___|_\__,_| |_|\__,_|_.__/_\___|
  ╙─────────────────────────────────────────────────────*/
  void GenFieldTable( const FieldDef &field ) {
    const auto &type = field.value.type;
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    code_.SetValue("INCLUDE", GetInclude(type));
    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var field_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not field_start: return null";
    if (IsBuiltinStruct(type)) {
      code_ += "return decode_{{GODOT_TYPE}}( field_start )";
    } else {
      code_ += "return {{INCLUDE}}get_{{GODOT_TYPE}}( bytes, field_start )";
    }
    code_.DecrementIdentLevel();
    code_ += "";
  }

  void GenFieldEnum( const FieldDef &field ) {
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    const auto &type = field.value.type;
    code_.SetValue("PBA_SUFFIX", gdPBASuffix(type.base_type));
    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var foffset : int = get_field_offset( vtable.{{OFFSET_NAME}} )";
    code_ += "if not foffset: return " + field.value.constant + " as {{GODOT_TYPE}}";
    code_ += "return bytes.decode_{{PBA_SUFFIX}}( start + foffset ) as {{GODOT_TYPE}}";
    code_.DecrementIdentLevel();
    code_ += "";
  }


  /*MARK: GenFieldUnion
  ║  ___          ___ _     _    _ _   _      _
  ║ / __|___ _ _ | __(_)___| |__| | | | |_ _ (_)___ _ _
  ║| (_ / -_) ' \| _|| / -_) / _` | |_| | ' \| / _ \ ' \
  ║ \___\___|_||_|_| |_\___|_\__,_|\___/|_||_|_\___/_||_|
  ╙──────────────────────────────────────────────────────*/
  void GenFieldUnion( const FieldDef &field ) {
    const auto &type = field.value.type;
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    // Unions are made of two parts, one of them is a scalar Enum
    if (field.IsScalar()) {
      GenFieldEnum( field );
      return;
    }

    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_.SetValue("INCLUDE", GetInclude(type));
    code_ += "var field_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not field_start: return null";
    // match the type
    code_ += "match( {{FIELD_NAME}}_type() ):";
    code_.IncrementIdentLevel();
    code_.SetValue("ENUM_TYPE", type.enum_def->name);
    for (const auto &val : type.enum_def->Vals()) {
      if (val->IsZero()) continue;
      code_.SetValue("ENUM_VALUE", ConvertCase(val->name, Case::kAllUpper));
      code_.SetValue("GODOT_TYPE", GetGodotType(val->union_type));
      code_ += "{{ENUM_TYPE}}.{{ENUM_VALUE}}:";
      code_.IncrementIdentLevel();
      if (IsBuiltinStruct(type)) {
        code_ += "return decode_{{GODOT_TYPE}}( field_start )";
      } else {
        code_ += "return {{INCLUDE}}get_{{GODOT_TYPE}}( bytes, field_start )";
      }
      code_.DecrementIdentLevel();
    }
    code_ += "_: pass";
    code_.DecrementIdentLevel();
    code_ += "return null";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenFieldString
  ║  ___          ___ _     _    _ ___ _       _
  ║ / __|___ _ _ | __(_)___| |__| / __| |_ _ _(_)_ _  __ _
  ║| (_ / -_) ' \| _|| / -_) / _` \__ \  _| '_| | ' \/ _` |
  ║ \___\___|_||_|_| |_\___|_\__,_|___/\__|_| |_|_||_\__, |
  ╙──────────────────────────────────────────────────|___/-*/
  void GenFieldString( const FieldDef &field [[maybe_unused]]) {
    // Assumes that FIELD_NAME, GODOT_TYPE, INCLUDE are set
    code_ += "func {{FIELD_NAME}}() -> {{INCLUDE}}{{GODOT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var field_start : int = get_field_start( vtable.{{OFFSET_NAME}} )";
    code_ += "if not field_start: return ''";
    code_ += "return decode_String( field_start )";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: GenField
  ║  ___          ___ _     _    _
  ║ / __|___ _ _ | __(_)___| |__| |
  ║| (_ / -_) ' \| _|| / -_) / _` |
  ║ \___\___|_||_|_| |_\___|_\__,_|
  ╙────────────────────────────────*/
  void GenField(const FieldDef &field) {
    // FIELD_NAME is set by GenTable
    const auto &type = field.value.type;
    code_.SetValue("OFFSET_NAME", "VT_" + ConvertCase(Name(field), Case::kAllUpper));
    code_.SetValue("GODOT_TYPE", GetGodotType(type));
    code_.SetValue("INCLUDE", IsIncluded(type) ? GetInclude(type) : "");

    if (IsUnion(type)) { GenFieldUnion( field ); }
    else if (IsEnum(type)) { GenFieldEnum( field ); }
    else if (field.IsScalar()) {GenFieldScalar( field );}
    else if (IsStruct(type)) { GenFieldStruct( field ); }
    else if (IsTable(type)) { GenFieldTable( field ); }
    else if (IsSeries(type)) { GenFieldVector( field ); }
    else if (IsString(type)) { GenFieldString( field ); }
    else {
      if (opts_.gdscript_debug) {
        GenFieldDebug(field);
      } else {
        code_.SetValue("TYPE_NAME", TypeName(type.base_type));
        code_ += "# NOTE: Unhandled field type in schema";
        code_ += "# {{FIELD_NAME}}:{{TYPE_NAME}}";
      }
    }
  }

  // Init function to prevent a rather spicy footgun
  void GenTableInit(const StructDef &struct_def[[maybe_unused]]) {
    code_ += "func _init( bytes_ : PackedByteArray = [], start_ : int = 0) -> void:";
    code_.IncrementIdentLevel();
    code_ += "bytes = bytes_; start = start_";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: Gen Table
  ║  ___            _____     _    _
  ║ / __|___ _ _   |_   _|_ _| |__| |___
  ║| (_ / -_) ' \    | |/ _` | '_ \ / -_)
  ║ \___\___|_||_|   |_|\__,_|_.__/_\___|
  ╙───────────────────────────────────────*/
  // Generate an accessor struct
  void GenTable(const StructDef &struct_def) {
    // Generate classes to access the table fields
    // The generated classes are a view into a PackedByteArray,
    // will decode the data on access.
    GenComment(struct_def.doc_comment);

    code_.SetValue("TABLE_NAME", Name(struct_def));

    // generate Flatbuffer derived class
    code_ += "class {{TABLE_NAME}} extends FlatBuffer:";
    code_.IncrementIdentLevel();

    GenStructIncludes(struct_def);
    GenVtableEnums(struct_def);
    GenTableInit(struct_def);

    // Generate Presence Function
    // {{FIELD_NAME}}_is_present() -> bool
    code_ += "# Presence Functions";
    for (const FieldDef *field : struct_def.fields.vec) {
      code_.SetValue("FIELD_NAME", Name(*field));
      if (field->deprecated) {
        code_ += "# field:'{{FIELD_NAME}}' is deprecated\n";
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue("OFFSET_NAME",
                     "VT_" + ConvertCase(Name(*field), Case::kAllUpper));
      GenPresenceFunc(*field);
    }

    // Generate the accessors.
    for (const FieldDef *field : struct_def.fields.vec) {
      code_.SetValue("FIELD_NAME", Name(*field));
      if (field->deprecated) {
        if (opts_.gdscript_debug) {
          code_ += "# field:'{{FIELD_NAME}}' is deprecated";
        }
        continue;
      }
      code_ += "# [================[ {{FIELD_NAME}} ]================]";
      GenComment(field->doc_comment);
      GenField(*field);
    }

    const Value *godot_type = struct_def.attributes.Lookup("godot_type");
    if( opts_.generate_object_based_api && godot_type != nullptr ){
      code_.SetValue("OBJECT_TYPE", godot_type->constant);
      code_.SetValue("OBJECT_NAME", ConvertCase(godot_type->constant, Case::kAllLower) );
      GenPackUnPack( struct_def );
    }
    if (opts_.gdscript_debug) { GenDebugDict(struct_def); }

    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: Gen Builder
  ║  ___            ___      _ _    _
  ║ / __|___ _ _   | _ )_  _(_) |__| |___ _ _
  ║| (_ / -_) ' \  | _ \ || | | / _` / -_) '_|
  ║ \___\___|_||_| |___/\_,_|_|_\__,_\___|_|
  ╙────────────────────────────────────────────*/
  void GenTableBuilder(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));

    // Generate a builder struct:
    code_ += "class {{STRUCT_NAME}}Builder extends RefCounted:";
    code_.IncrementIdentLevel();
    code_ += "var fbb_: FlatBufferBuilder";
    code_ += "var start_ : int";
    code_ += "";

    // Add init function
    code_ += "func _init( _fbb : FlatBufferBuilder ) -> void:";
    code_.IncrementIdentLevel();
    code_ += "fbb_ = _fbb";
    code_ += "start_ = _fbb.start_table()";
    code_.DecrementIdentLevel();
    code_ += "";

    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) continue;

      // Generate add functions of the forms:
      // func add_{{FIELD_NAME}}( {{FIELD_NAME}} : {{GODOT_TYPE}} ) -> void:
      //   fbb_.add_element_{{GODOT_TYPE}}_default( {{FIELD_OFFSET}}, {{FIELD_NAME}}, {{VALUE_DEFAULT}});

      // func add_{{FIELD_NAME}}( {{FIELD_NAME}}_offset : int ) -> void:
      //   fbb_.add_offset( {{FIELD_OFFSET}, {{FIELD_NAME}} )

      const auto type = field->value.type;
      const bool is_inline = field->IsScalar() || IsStruct(type) || IsArray(type);
      const bool is_default_scalar = is_inline && !field->IsScalarOptional();

      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("INCLUDE", "");
      code_.SetValue("FIELD_OFFSET", "VT_" + ConvertCase(Name(*field), Case::kAllUpper));
      code_.SetValue("PARAM_NAME", Name(*field) + (is_inline ? "" : "_offset"));
      code_.SetValue("INCLUDE", IsStruct(type) ? include_map[type.struct_def->file] : "");
      code_.SetValue("PARAM_TYPE", is_inline ? GetGodotType(type) : "int");
      code_.SetValue("VALUE_DEFAULT", is_default_scalar ? field->value.constant : "");

      // Function Signature
      code_ += "func add_{{FIELD_NAME}}( {{PARAM_NAME}} : {{INCLUDE}}{{PARAM_TYPE}} ) -> void:";
      code_.IncrementIdentLevel();

      // Seems the ordering here is important, because a scalar value can also be a union or enum type.
      // So I have to start with the enum first, then union, then scalar.
      if (IsUnion(type)) {
        // Unions are made of two fields, One to store the offset to the object,
        // and the other to identify which object is stored.
        if ( IsScalar(type.base_type) ) { // The identifier
          code_ += "fbb_.add_element_ubyte( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
        } else {
          code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
        }
      }
      else if (IsEnum(type)) {
        code_.SetValue("TYPE_NAME", TypeName(type.base_type) );
        code_ += "fbb_.add_element_{{TYPE_NAME}}( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      else if (field->IsScalar()) {
        code_.SetValue("TYPE_NAME", TypeName(type.base_type));
        code_ += "fbb_.add_element_{{TYPE_NAME}}_default( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}}, {{VALUE_DEFAULT}} )";
      }
      else if (IsStruct(type)) {
        if (IsBuiltinStruct(type)) {
          code_ += "fbb_.add_{{PARAM_TYPE}}( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
        } else {
          code_ += "fbb_.add_bytes( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}}.bytes ) ";
        }
      }
      else if (IsTable(type)) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      else if (IsString(type)) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      else if (IsVector(type)) {
        code_ += "fbb_.add_offset( {{STRUCT_NAME}}.vtable.{{FIELD_OFFSET}}, {{PARAM_NAME}} )";
      }
      // TODO Vector of Union
      // TODO Fixed length Array
      // TODO Dictionary
      else {
        code_ += "# FIXME Unknown Type";
        code_ += "pass";
        if ( opts_.gdscript_debug )GenFieldDebug(*field);
      }

      code_.DecrementIdentLevel();
      code_ += "";
    }

    // var finish(): -> void
    // ---------------------
    code_ += "func finish() -> int:";
    code_.IncrementIdentLevel();
    code_ += "var end : int = fbb_.end_table( start_ )";
    code_ += "var o : int = end";

    for (const auto &field : struct_def.fields.vec) {
      if (!field->deprecated && field->IsRequired()) {
        code_.SetValue("FIELD_NAME", Name(*field));
        code_.SetValue("OFFSET_NAME", "VT_" + ConvertCase(Name(*field), Case::kAllUpper));
        code_ += "fbb_.Required(o, {{STRUCT_NAME}}.vtable.{{OFFSET_NAME}});";
      }
    }
    code_ += "return o;";
    code_.DecrementIdentLevel();
    code_ += "";

    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: Gen Create
  ║  ___             ___              _
  ║ / __|___ _ _    / __|_ _ ___ __ _| |_ ___
  ║| (_ / -_) ' \  | (__| '_/ -_) _` |  _/ -_)
  ║ \___\___|_||_|  \___|_| \___\__,_|\__\___|
  ╙────────────────────────────────────────────*/
  void GenTableCreate(const StructDef &struct_def) {
    // Generate a convenient create_<name> function that uses the above builder
    // to create a table in one go.
    code_.SetValue("TABLE_NAME", Name(struct_def));

    code_ += "static func create_{{TABLE_NAME}}( _fbb : FlatBufferBuilder,";
    code_.IncrementIdentLevel();
    code_.IncrementIdentLevel();
    code_.SetValue("SEP", ",");
    bool add_sep = false;
    for (const auto &field : struct_def.fields.vec) {
      if (field->deprecated) continue;
      const auto type = field->value.type;
      if (add_sep) code_ += "{{SEP}}";
      code_.SetValue("PARAM_NAME", Name(*field));
      code_.SetValue("DEFAULT_VALUE", "default");
      // Scalar | Struct | Fixed length Array
      // These items are added inline in the table, and do not require creating
      // an offset ahead of time.

      code_.SetValue("INCLUDE", "");
      code_.SetValue("PARAM_TYPE", GetGodotType(type));
      if (IsScalar(type.base_type)) { /* no changes */
      } else if (IsStruct(type)) {
        code_.SetValue("INCLUDE", IsIncluded(type) ? GetInclude(type) : "");
      } else {
        code_.SetValue("PARAM_TYPE", "int");
      }
      code_ += "{{PARAM_NAME}} : {{INCLUDE}}{{PARAM_TYPE}}\\";
      // TODO add default value if possible.
      add_sep = true;
    }
    code_ += " ) -> int :";
    code_.DecrementIdentLevel();

    // for( size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) :
    // 1; size; size /= 2 ) {
    //   for( auto it = struct_def.fields.vec.rbegin(); it !=
    //   struct_def.fields.vec.rend(); ++it ) {
    //     const auto &field = **it;
    //     if( not field.deprecated and ( not struct_def.sortbysize or size
    //     == SizeOf( field.value.type.base_type )) ) {
    //       if( not IsStruct( field.value.type ) ) continue;
    //       // FIXME this might also include fixed sized arrays
    //       code_.SetValue( "FIELD_NAME", Name( field ) );
    //       code_ += "# Create {{FIELD_NAME}}";
    //       code_ += "var {{FIELD_NAME}}_offset";
    //     }
    //   }
    // }

    // Create* function body
    code_ += "var builder : {{TABLE_NAME}}Builder = {{TABLE_NAME}}Builder.new( _fbb );";
    for (size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2) {
      for (auto it = struct_def.fields.vec.rbegin(); it != struct_def.fields.vec.rend(); ++it) {
        if (const auto &field = **it; !field.deprecated &&
            (!struct_def.sortbysize || size == SizeOf(field.value.type.base_type))) {
          code_.SetValue("FIELD_NAME", Name(field));
          code_ += "builder.add_{{FIELD_NAME}}( {{FIELD_NAME}} );";
        }
      }
    }
    code_ += "return builder.finish();";
    code_.DecrementIdentLevel();
    code_ += "";
  }

  /*MARK: Pack / UnPack

  ║ ___         _       __  _   _      ___         _
  ║| _ \__ _ __| |__   / / | | | |_ _ | _ \__ _ __| |__
  ║|  _/ _` / _| / /  / /  | |_| | ' \|  _/ _` / _| / /
  ║|_| \__,_\__|_\_\ /_/    \___/|_||_|_| \__,_\__|_\_\
  ╙─────────────────────────────────────────────────────*/
  void GenPackUnPack(const StructDef &struct_def) {
    // defined values: TABLE_NAME, OBJECT_TYPE, OBJECT_NAME
    code_ += "# Pack/UnPack Object based API";

    // Function Declaration
    code_ += "static func pack(_fbb : FlatBufferBuilder, object : {{OBJECT_TYPE}} ) -> int:";
    code_.IncrementIdentLevel();
    // create offset items, and store add functions for later.
    std::vector<const FieldDef*> ordered_fields;

    // We loop down through sizes, from largest power of two to zero, or we do this once.
    for (size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1; size; size /= 2) {
      // Loop through the fields from start to finish
      for (auto it = struct_def.fields.vec.rbegin(); it != struct_def.fields.vec.rend(); ++it) {
        const FieldDef *field = *it;
        const Type *field_type = &field->value.type;
        if( field->deprecated ) continue; // skip deprecated fields
        // if we are sorting by size, skip fields that dont match the size,
        // they will get done next size decrease.
        if( struct_def.sortbysize && size != SizeOf(field->value.type.base_type) ){ continue; }
        ordered_fields.push_back( field );
        // nothing to do for scalar and struct types here.
        if (IsScalar(field_type->base_type) || IsStruct(*field_type)){ continue; }
        Type element_type;
        code_.SetValue("FIELD_NAME", Name(*field));
        code_.SetValue("INCLUDE", "");
        code_.SetValue("FIELD_TYPE", GetGodotType(*field_type));
        // tables and arrays, aka offset fields
        code_ += "var {{FIELD_NAME}}_ofs : int = _fbb.create_{{FIELD_TYPE}}( object.{{FIELD_NAME}} )";
        // TODO if this is not a builtin type then Pack will need to be called on it.
      }
    }
    // Add scalar and inline objects
    code_ += "var builder : {{TABLE_NAME}}Builder = {{TABLE_NAME}}Builder.new( _fbb )";
    for ( const auto field : ordered_fields ) {
      const Type *field_type = &field->value.type;
      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("INCLUDE", "");
      code_.SetValue("FIELD_TYPE", GetGodotType(*field_type));
      code_.SetValue("DEFAULT_VALUE", field->value.constant);

      if (IsScalar(field_type->base_type) || IsStruct(*field_type)) {
        // scalar and structs aka inline fields
        code_ += "builder.add_{{FIELD_NAME}}( object.{{FIELD_NAME}} )";
      } else { // tables and arrays, aka offset fields
        code_ += "builder.add_{{FIELD_NAME}}( {{FIELD_NAME}}_ofs )";
      }
    }
    code_ += "return builder.finish();";
    code_.DecrementIdentLevel();
    code_ += "";


    code_ += "static func unpack( _bytes : PackedByteArray, _start : int ) -> {{OBJECT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "var {{OBJECT_NAME}} : {{OBJECT_TYPE}} = {{OBJECT_TYPE}}.new()";
    code_ += "unpack_to(_bytes, _start, {{OBJECT_NAME}} )";
    code_ += "return {{OBJECT_NAME}}";
    code_.DecrementIdentLevel();
    code_ += "";


    code_ += "static func unpack_to( _bytes : PackedByteArray, _start : int, object : {{OBJECT_TYPE}} ) -> void:";
    code_.IncrementIdentLevel();
    code_ += "var fbt : {{TABLE_NAME}} = {{TABLE_NAME}}.new(_bytes, _start)";
    for ( const auto field : ordered_fields ) {
      const Type *field_type = &field->value.type;
      code_.SetValue("FIELD_NAME", Name(*field));
      code_.SetValue("INCLUDE", "");
      code_.SetValue("FIELD_TYPE", GetGodotType(*field_type));
      code_.SetValue("DEFAULT_VALUE", field->value.constant);


      code_ += "object.{{FIELD_NAME}} = fbt.{{FIELD_NAME}}()";
    }
    code_.DecrementIdentLevel();
    code_ += "";

  }

  // Generate The Object Based interface using the same function names as C++
  void GenUnPackObject(const StructDef &struct_def) {
    (void)struct_def;
    // defined values: TABLE_NAME, OBJECT_TYPE, OBJECT_NAME
    code_ += "# UnPack Object based API";
    // Function Declaration
    code_ += "static func unpack_{{TABLE_NAME}}(_bytes : PackedByteArray, _start : int, ) -> {{OBJECT_TYPE}}:";
    code_.IncrementIdentLevel();
    code_ += "return {{TABLE_NAME}}.unpack(_bytes, _start)";
    code_.DecrementIdentLevel();
    code_ += "";
  }
};

}  // namespace gdscript

/*MARK: RemainingBoilerPlate
║ ___                _      _           ___      _ _         ___ _      _
║| _ \___ _ __  __ _(_)_ _ (_)_ _  __ _| _ ) ___(_) |___ _ _| _ \ |__ _| |_ ___
║|   / -_) '  \/ _` | | ' \| | ' \/ _` | _ \/ _ \ | / -_) '_|  _/ / _` |  _/ -_)
║|_|_\___|_|_|_\__,_|_|_||_|_|_||_\__, |___/\___/_|_\___|_| |_| |_\__,_|\__\___|
╙─────────────────────────────────|___/─────────────────────────────────────────*/

static bool GenerateGDScript(
    const Parser &parser,
    const std::string &path,
    const std::string &file_name) {
  const gdscript::IDLOptionsGdscript opts(parser.opts);
  gdscript::GdscriptGenerator generator(parser, path, file_name, opts);
  return generator.generate();
}

namespace {

class GdscriptCodeGenerator final : public CodeGenerator {
 public:
  Status GenerateCode(
      const Parser &parser,
      const std::string &path,
      const std::string &filename) override {
    if (!GenerateGDScript(parser, path, filename)) { return Status::ERROR; }
    return Status::OK;
  }

  // Generate code from the provided `buffer` of given `length`. The buffer is a
  // serialised reflection.fbs.
  Status GenerateCode(
      [[maybe_unused]] const uint8_t *buffer,
      [[maybe_unused]] const int64_t length,
      [[maybe_unused]] const CodeGenOptions &options) override {
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateMakeRule(
      [[maybe_unused]] const Parser &parser,
      [[maybe_unused]] const std::string &path,
      [[maybe_unused]] const std::string &filename,
      [[maybe_unused]] std::string &output) override {
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateGrpcCode(
      [[maybe_unused]] const Parser &parser,
      [[maybe_unused]] const std::string &path,
      [[maybe_unused]] const std::string &filename) override {
    return Status::NOT_IMPLEMENTED;
  }

  Status GenerateRootFile(
      [[maybe_unused]] const Parser &parser,
      [[maybe_unused]] const std::string &path) override {
    return Status::NOT_IMPLEMENTED;
  }

  [[nodiscard]] bool IsSchemaOnly() const override {
    return true;
  }

  [[nodiscard]] bool SupportsBfbsGeneration() const override {
    return false;
  }

  [[nodiscard]] bool SupportsRootFileGeneration() const override {
    return false;
  }

  [[nodiscard]] IDLOptions::Language Language() const override {
    return IDLOptions::kGDScript;
  }

  [[nodiscard]] std::string LanguageName() const override {
    return "GDScript";
  }
};

}  // namespace

std::unique_ptr<CodeGenerator> NewGDScriptCodeGenerator() {
  return std::make_unique<GdscriptCodeGenerator>();
}

}  // namespace flatbuffers
