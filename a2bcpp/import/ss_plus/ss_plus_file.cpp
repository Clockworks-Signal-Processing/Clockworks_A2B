/**
 * @file ss_plus_file.cpp
 * @brief SsPlusFile: a SigmaStudio+ export's C file read at run time.
 *
 * SigmaStudio+ writes the bus configuration as C (adi_a2b_busconfig.c): variables of
 * ADI's structures (adi_a2b_busconfig.h), initialised mostly with designated
 * initializers (`.nTDMMode = A2B_TDM8,`), positional ones for the peripheral
 * configuration units, and data arrays. SsPlusFile reads that subset of C and fills the
 * same structures in memory, as the compiler would have, so ImportSsPlus() takes them
 * unchanged:
 * - **Preprocessor:** `#if N`, `#ifdef`, `#ifndef`, `#elif`, `#else` and `#endif` (a name
 *   is defined if it is one of the header's constants); `#include`, `#define` and the
 *   rest are ignored.
 * - **Declarations:** `[static] [const] [ADI_A2B_MEM_...] Type name[N] = init;`, with
 *   tentative declarations (no initializer) first, as the exports have them. Variables of
 *   types the header doesn't define are skipped, with a warning.
 * - **Initializers:** braces, designators (`.field`, `[index]`, chained), positional
 *   values, numbers (with u/U/l/L suffixes), the header's constants, `NULL_PTR`,
 *   `&variable`, `&variable[index]`, a bare array name, casts, and `|`, `+`, `-`, `~`.
 * - **Anything not set is zero,** as in C.
 *
 * A field the header doesn't have (an export from a newer SigmaStudio+, or the fields
 * this repository's exports keep under `#if 0` because they don't compile) is skipped
 * with a warning: only ImportSsPlus() decides what matters. An unknown constant is a
 * warning too, taken as 0, in the parts ImportSsPlus() doesn't read (sub-node
 * descriptions, streams), and an error where it reads. A pointer to a variable of the
 * wrong type, too many initializers or a syntax error is an error, with its line.
 *
 * The field tables come from the header itself: tools/gen_ss_plus_tables.py lists each
 * structure's fields and the header's constants (ss_plus_tables.inc), and the templates
 * below take each field's offset, size and kind from the compiler.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ss_plus_file.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

namespace a2b {
namespace {

// ---------------------------------------------------------------------------------------
// Field tables
// ---------------------------------------------------------------------------------------

struct TypeInfo;

/** @brief What a field (or each element of an array field) holds. */
enum class Kind : uint8_t {
  kInt,      ///< An integer or an enum.
  kStruct,   ///< A structure.
  kPointer,  ///< A pointer.
};

/** @brief Gives the TypeInfo of a structure, or nullptr. */
using InfoFunction = const TypeInfo* (*)();

/** @brief One field of a structure, from the compiler. */
struct FieldInfo {
  const char* name;    ///< Its name.
  size_t offset;       ///< Its offset in the structure.
  bool isArray;        ///< It is an array.
  size_t count;        ///< Elements: the array's length, or 1.
  size_t stride;       ///< Bytes per element.
  Kind kind;           ///< What each element holds.
  bool isSigned;       ///< kInt: signed.
  InfoFunction type;   ///< kStruct: the structure; kPointer: the structure pointed to.
  size_t pointeeSize;  ///< kPointer to integers: their size (0 for a structure or void).
};

/** @brief One structure: its name, size and fields in declaration order. */
struct TypeInfo {
  const char* name;
  size_t size;
  const FieldInfo* fields;
  size_t fieldCount;
};

/** The TypeInfo of @p T: nullptr, except for the structures the table lists. */
template <typename T>
const TypeInfo* InfoOf() {
  return nullptr;
}

/** An array type's element and length; any other type is its own element, once. */
template <typename T>
struct ArrayOf {
  using Element = T;
  static constexpr size_t kCount = 1;
};
template <typename T, size_t N>
struct ArrayOf<T[N]> {
  using Element = T;
  static constexpr size_t kCount = N;
};

/** True for a signed integer, or an enum whose underlying type is signed. */
template <typename T>
constexpr bool IsSigned() {
  if constexpr (std::is_enum_v<T>) {
    return std::is_signed_v<std::underlying_type_t<T>>;
  } else {
    return std::is_signed_v<T>;
  }
}

/** Describes a field of type @p Member at @p offset. */
template <typename Member>
FieldInfo MakeField(const char* name, size_t offset) {
  using Element = typename ArrayOf<Member>::Element;
  FieldInfo field = {name,
                     offset,
                     std::is_array_v<Member>,
                     ArrayOf<Member>::kCount,
                     sizeof(Element),
                     Kind::kInt,
                     false,
                     nullptr,
                     0};
  if constexpr (std::is_pointer_v<Element>) {
    using Pointee = std::remove_cv_t<std::remove_pointer_t<Element>>;
    field.kind = Kind::kPointer;
    field.type = &InfoOf<Pointee>;
    if constexpr (!std::is_class_v<Pointee> && !std::is_void_v<Pointee>) {
      field.pointeeSize = sizeof(Pointee);
    }
  } else if constexpr (std::is_class_v<Element>) {
    field.kind = Kind::kStruct;
    field.type = &InfoOf<Element>;
  } else {
    static_assert(std::is_integral_v<Element> || std::is_enum_v<Element>,
                  "a field the loader can't fill");
    field.isSigned = IsSigned<Element>();
  }
  return field;
}

// Each structure's TypeInfo, declared, then defined, from the generated table; then the
// tables by name. (The X-macros are left out of the Doxygen docs.)
/// @cond A2B_SS_TABLES
#define A2B_SS_STRUCT(type) \
  template <>               \
  const TypeInfo* InfoOf<type>();
#define A2B_SS_FIELD(name)
#define A2B_SS_END()
#include "ss_plus_tables.inc"
#undef A2B_SS_STRUCT
#undef A2B_SS_FIELD
#undef A2B_SS_END

#define A2B_SS_STRUCT(type)                 \
  template <>                               \
  const TypeInfo* InfoOf<type>() {          \
    using Self = type;                      \
    static const char* const kName = #type; \
    static const FieldInfo kFields[] = {
#define A2B_SS_FIELD(name) MakeField<decltype(Self::name)>(#name, offsetof(Self, name)),
#define A2B_SS_END()                                                    \
  }                                                                     \
  ;                                                                     \
  static const TypeInfo kInfo = {kName, sizeof(Self), kFields,          \
                                 sizeof(kFields) / sizeof(kFields[0])}; \
  return &kInfo;                                                        \
  }
#include "ss_plus_tables.inc"
#undef A2B_SS_STRUCT
#undef A2B_SS_FIELD
#undef A2B_SS_END

/** @brief A structure an export may declare a variable of, by name. */
struct NamedType {
  const char* name;
  InfoFunction info;
};

#define A2B_SS_STRUCT(type) {#type, &InfoOf<type>},
#define A2B_SS_FIELD(name)
#define A2B_SS_END()
const NamedType kStructTypes[] = {
#include "ss_plus_tables.inc"
};
#undef A2B_SS_STRUCT
#undef A2B_SS_FIELD
#undef A2B_SS_END

/** @brief An integer type an export may declare data of. */
struct IntType {
  const char* name;
  size_t size;
  bool isSigned;
};

// clang-format off: LLVM 18 lays this macro out differently from LLVM 19 and later.
#define A2B_SS_INT(type) {#type, sizeof(type), std::is_signed_v<type>}
// clang-format on
const IntType kIntTypes[] = {
    A2B_SS_INT(uint8),           A2B_SS_INT(uint16),
    A2B_SS_INT(uint32),          A2B_SS_INT(int8),
    A2B_SS_INT(int16),           A2B_SS_INT(int32),
    A2B_SS_INT(a2b_UInt8),       A2B_SS_INT(a2b_UInt16),
    A2B_SS_INT(a2b_UInt32),      A2B_SS_INT(a2b_Int8),
    A2B_SS_INT(a2b_Int16),       A2B_SS_INT(a2b_Int32),
    A2B_SS_INT(a2b_Char),        {"char", 1, std::is_signed_v<char>},
    {"unsigned char", 1, false}, {"signed char", 1, true},
};
#undef A2B_SS_INT

/** @brief A constant of the header (enumerator or #define). */
struct Symbol {
  const char* name;
  int64_t value;
};

#define A2B_SS_SYMBOL(name) {#name, static_cast<int64_t>(name)},
const Symbol kSymbols[] = {
#include "ss_plus_tables.inc"
};
#undef A2B_SS_SYMBOL
/// @endcond

/** Looks up a constant of the header. */
bool FindSymbol(const std::string& name, int64_t& value) {
  for (const Symbol& symbol : kSymbols) {
    if (name == symbol.name) {
      value = symbol.value;
      return true;
    }
  }
  return false;
}

/** Looks up a structure type by name. */
const TypeInfo* FindStruct(const std::string& name) {
  for (const NamedType& type : kStructTypes) {
    if (name == type.name) {
      return type.info();
    }
  }
  return nullptr;
}

/** Looks up an integer type by name. */
const IntType* FindInt(const std::string& name) {
  for (const IntType& type : kIntTypes) {
    if (name == type.name) {
      return &type;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------------------
// Tokens and the preprocessor
// ---------------------------------------------------------------------------------------

/** @brief One token of the export. */
struct Token {
  /** @brief Its kind. */
  enum class Type : uint8_t {
    kName,    ///< An identifier.
    kNumber,  ///< An integer.
    kString,  ///< A string or character literal.
    kPunct,   ///< One punctuation character.
    kEnd,     ///< The end of the file.
  };
  Type type = Type::kEnd;
  std::string text;     ///< kName, kPunct: its text.
  uint64_t number = 0;  ///< kNumber: its value.
  int line = 0;         ///< Its line, from 1.
};

/** @brief One open #if: whether its lines are used. */
struct Conditional {
  bool parentActive;  ///< The lines around it are used.
  bool active;        ///< Its current branch is used.
  bool taken;         ///< A branch of it has been used.
};

/** @p text with the export's line number @p line in front, for a warning or an error. */
std::string AtLine(int line, const std::string& text) {
  return "line " + std::to_string(line) + ": " + text;
}

/** Splits an export into tokens, applying its #if blocks. */
class Lexer {
 public:
  /// Over @p len bytes of @p text; skipped #define lines go to @p warnings.
  Lexer(const char* text, size_t len, std::vector<std::string>& warnings)
      : m_Text(text), m_End(text + len), m_Warnings(warnings) {}

  bool Run(std::vector<Token>& tokens, std::string& error);  ///< Tokenises the text.

 private:
  bool Directive(std::string& error);  ///< Handles a preprocessor line.
  bool Evaluate(const std::string& expr, bool& value,
                std::string& error) const;  ///< Evaluates an #if.
  /// The current lines are used (not in a false #if branch).
  bool Active() const { return m_Conditionals.empty() || m_Conditionals.back().active; }
  /// A character that starts a name.
  static bool IsNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  }
  /// A character of a name.
  static bool IsNameChar(char c) { return IsNameStart(c) || (c >= '0' && c <= '9'); }

  const char* m_Text;                       ///< The next character to read.
  const char* m_End;                        ///< The end of the text.
  std::vector<std::string>& m_Warnings;     ///< Where warnings go.
  int m_Line = 1;                           ///< The current line.
  bool m_LineStart = true;                  ///< Only white space so far on this line.
  std::vector<Conditional> m_Conditionals;  ///< The open #if blocks.
};

/**
 * Tokenises the text.
 * @param tokens Gets the tokens of the lines in use, then a kEnd token.
 * @param error Gets the first error.
 * @return False on an error.
 */
bool Lexer::Run(std::vector<Token>& tokens, std::string& error) {
  const char* p = m_Text;
  while (p < m_End) {
    const char c = *p;
    if (c == '\n') {
      ++m_Line;
      m_LineStart = true;
      ++p;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
      ++p;
      continue;
    }
    if (c == '/' && p + 1 < m_End && p[1] == '/') {
      while (p < m_End && *p != '\n') {
        ++p;
      }
      continue;
    }
    if (c == '/' && p + 1 < m_End && p[1] == '*') {
      const int start = m_Line;
      p += 2;
      while (p + 1 < m_End && !(p[0] == '*' && p[1] == '/')) {
        m_Line += (*p == '\n') ? 1 : 0;
        ++p;
      }
      if (p + 1 >= m_End) {
        error = AtLine(start, "comment not closed");
        return false;
      }
      p += 2;
      continue;
    }
    if (c == '#' && m_LineStart) {
      m_Text = p;
      if (!Directive(error)) {
        return false;
      }
      p = m_Text;
      continue;
    }
    m_LineStart = false;
    Token token;
    token.line = m_Line;
    if (IsNameStart(c)) {
      const char* start = p;
      while (p < m_End && IsNameChar(*p)) {
        ++p;
      }
      token.type = Token::Type::kName;
      token.text.assign(start, p);
    } else if (c >= '0' && c <= '9') {
      char* end = nullptr;
      token.type = Token::Type::kNumber;
      token.number = std::strtoull(p, &end, 0);
      p = end;
      while (p < m_End && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L')) {
        ++p;
      }
      if (p < m_End && (IsNameChar(*p) || *p == '.')) {
        error = AtLine(m_Line, "malformed number");
        return false;
      }
    } else if (c == '"' || c == '\'') {
      const char quote = c;
      ++p;
      while (p < m_End && *p != quote && *p != '\n') {
        p += (*p == '\\' && p + 1 < m_End) ? 2 : 1;
      }
      if (p >= m_End || *p != quote) {
        error = AtLine(m_Line, "literal not closed");
        return false;
      }
      ++p;
      token.type = Token::Type::kString;
    } else {
      token.type = Token::Type::kPunct;
      token.text.assign(1, c);
      ++p;
    }
    if (Active()) {
      tokens.push_back(token);
    }
  }
  if (!m_Conditionals.empty()) {
    error = AtLine(m_Line, "#if without #endif");
    return false;
  }
  Token end;
  end.line = m_Line;
  tokens.push_back(end);
  return true;
}

/**
 * Handles the directive at m_Text (its '#'), and moves m_Text past its line.
 * @param error Gets an error.
 * @return False on an error.
 */
bool Lexer::Directive(std::string& error) {
  const int line = m_Line;
  std::string text;
  const char* p = m_Text + 1;
  while (p < m_End && *p != '\n') {
    if (*p == '\r') {  // CRLF line ends (a Windows checkout).
      ++p;
      continue;
    }
    if (*p == '\\' && p + 1 < m_End && (p[1] == '\n' || p[1] == '\r')) {  // A continued line.
      p += (p[1] == '\r' && p + 2 < m_End && p[2] == '\n') ? 3 : 2;
      ++m_Line;
      continue;
    }
    text += *p++;
  }
  m_Text = p;
  // Comments on the line: a // to the end, and /* */ that close on it.
  const size_t slashes = text.find("//");
  if (slashes != std::string::npos) {
    text.erase(slashes);
  }
  for (size_t open = text.find("/*"); open != std::string::npos; open = text.find("/*")) {
    const size_t close = text.find("*/", open + 2);
    text.erase(open, (close == std::string::npos) ? std::string::npos : close + 2 - open);
  }
  size_t start = text.find_first_not_of(" \t");
  const size_t nameEnd = text.find_first_of(" \t(", (start == std::string::npos) ? 0 : start);
  const std::string name = (start == std::string::npos) ? "" : text.substr(start, nameEnd - start);
  const std::string rest = (nameEnd == std::string::npos) ? "" : text.substr(nameEnd);

  const bool active = Active();
  if (name == "if" || name == "ifdef" || name == "ifndef") {
    bool value = false;
    if (active) {
      const std::string expr = (name == "if") ? rest : "defined(" + rest + ")";
      if (!Evaluate(expr, value, error)) {
        error = AtLine(line, error);
        return false;
      }
      value = (name == "ifndef") ? !value : value;
    }
    m_Conditionals.push_back({active, active && value, active && value});
    return true;
  }
  if (name == "elif" || name == "else") {
    if (m_Conditionals.empty()) {
      error = AtLine(line, "#" + name + " without #if");
      return false;
    }
    Conditional& open = m_Conditionals.back();
    bool value = (name == "else");
    if (open.parentActive && !open.taken && name == "elif" && !Evaluate(rest, value, error)) {
      error = AtLine(line, error);
      return false;
    }
    open.active = open.parentActive && !open.taken && value;
    open.taken = open.taken || open.active;
    return true;
  }
  if (name == "endif") {
    if (m_Conditionals.empty()) {
      error = AtLine(line, "#endif without #if");
      return false;
    }
    m_Conditionals.pop_back();
    return true;
  }
  if (active && name == "define") {
    m_Warnings.push_back(AtLine(line, "#define ignored"));
  }
  return true;  // #include, #pragma and the rest: nothing to do.
}

/**
 * Evaluates an #if expression: a number, a constant of the header (an unknown name is 0,
 * as in C), `defined(NAME)` or `defined NAME`, each optionally after `!`.
 * @param expr The expression.
 * @param value Gets its truth.
 * @param error Gets an error.
 * @return False if it is not of that form.
 */
bool Lexer::Evaluate(const std::string& expr, bool& value, std::string& error) const {
  const size_t first = expr.find_first_not_of(" \t");
  const std::string shown = (first == std::string::npos) ? "" : expr.substr(first);
  std::string text;
  for (char c : expr) {
    if (c != ' ' && c != '\t' && c != '\r') {
      text += c;
    }
  }
  bool negate = false;
  while (!text.empty() && text[0] == '!') {
    negate = !negate;
    text.erase(0, 1);
  }
  int64_t number = 0;
  if (text.compare(0, 7, "defined") == 0) {
    std::string name = text.substr(7);
    if (name.size() >= 2 && name.front() == '(' && name.back() == ')') {
      name = name.substr(1, name.size() - 2);
    }
    value = FindSymbol(name, number);
  } else if (!text.empty() && text[0] >= '0' && text[0] <= '9') {
    char* end = nullptr;
    value = std::strtoll(text.c_str(), &end, 0) != 0;
    if (*end != '\0' && std::strspn(end, "uUlL") != std::strlen(end)) {
      error = "can't evaluate #if " + shown;
      return false;
    }
  } else if (!text.empty() && IsNameStart(text[0])) {
    for (char c : text) {
      if (!IsNameChar(c)) {
        error = "can't evaluate #if " + shown;
        return false;
      }
    }
    value = FindSymbol(text, number) && number != 0;
  } else {
    error = "can't evaluate #if " + shown;
    return false;
  }
  value = value != negate;
  return true;
}

// ---------------------------------------------------------------------------------------
// Initializers and variables
// ---------------------------------------------------------------------------------------

/** @brief One designator: `.field` or `[index]`. */
struct Designator {
  bool isIndex = false;
  std::string field;  ///< `.field`
  int64_t index = 0;  ///< `[index]`
};

/** @brief An initializer, as written. */
struct Value {
  /** @brief Its form. */
  enum class Type : uint8_t {
    kInt,   ///< An integer.
    kName,  ///< A bare name: a constant, or an array (its first element).
    kRef,   ///< `&name` or `&name[index]`.
    kList,  ///< `{ ... }`.
  };
  Type type = Type::kInt;
  int64_t number = 0;                   ///< kInt.
  std::string name;                     ///< kName, kRef.
  int64_t index = 0;                    ///< kRef.
  std::vector<Value> items;             ///< kList.
  std::vector<Designator> designators;  ///< As an item of a list: its designators.
  int line = 0;
};

/** @brief A variable of the export. */
struct Variable {
  std::string typeName;
  const TypeInfo* info = nullptr;    ///< A structure, or nullptr.
  const IntType* intType = nullptr;  ///< An integer type, or nullptr.
  bool isArray = false;
  bool hasSize = false;  ///< The declaration gives the array's length.
  size_t count = 1;      ///< Elements.
  bool hasInit = false;
  Value init;
  int line = 0;
  std::vector<uint64_t> storage;  ///< Its bytes, 8-byte aligned.

  /// Bytes per element.
  size_t ElementSize() const { return (info != nullptr) ? info->size : intType->size; }
  /// Its first byte.
  uint8_t* Bytes() { return reinterpret_cast<uint8_t*>(storage.data()); }
};

}  // namespace

// ---------------------------------------------------------------------------------------
// The loader
// ---------------------------------------------------------------------------------------

/** @brief The loaded variables, and the parser that fills them. */
struct SsPlusFile::Impl {
  std::map<std::string, Variable> variables;  ///< The export's variables, by name.
  std::string error;                          ///< The first error, with its line.
  std::vector<std::string> warnings;          ///< What was skipped, with the lines.
  const ADI_A2B_BCD* bus = nullptr;           ///< sBusDescription, once filled.

  std::vector<Token> tokens;  ///< The export's tokens, then kEnd.
  size_t pos = 0;             ///< The next token to parse.

  std::set<std::string> used;  ///< Variables ImportSsPlus() can reach (FindUsed()).
  std::string current;         ///< The variable being filled.
  bool currentUsed = true;     ///< It is in #used.

  bool Run(const char* text, size_t len);                   ///< Loads the text.
  const Token& Peek(size_t ahead = 0) const;                ///< A token ahead.
  bool IsPunct(const char* punct, size_t ahead = 0) const;  ///< A punctuation ahead.
  bool Expect(const char* punct);                           ///< Takes a punctuation.
  bool Fail(int line, const std::string& text);             ///< Records an error.
  bool ParseDeclaration();                                  ///< One declaration.
  bool SkipStatement();                                     ///< Skips a declaration.
  bool ParseInitializer(Value& value);                      ///< An initializer.
  bool ParseExpression(Value& value);                       ///< Terms joined by operators.
  bool ParseTerm(Value& value);                             ///< One term.
  bool ToInt(Value& value);                                 ///< A constant to its value.

  bool Allocate();  ///< Gives each variable its storage.
  void FindUsed();  ///< The variables ImportSsPlus() can reach.
  void CollectRefs(const Value& value, std::vector<std::string>& names) const;  ///< Refs in it.
  bool FillVariable(const std::string& name, Variable& variable);  ///< Fills a variable.
  bool FillStruct(uint8_t* base, const TypeInfo& info,
                  const Value& value);  ///< Fills a structure.
  bool FillArray(uint8_t* base, const FieldInfo& element, size_t count,
                 const Value& value);  ///< Fills an array.
  bool FillDesignated(uint8_t* base, const FieldInfo& target, const Value& value,
                      size_t from);  ///< Fills a field through designators.
  bool FillElement(uint8_t* base, const FieldInfo& element,
                   const Value& value);  ///< Fills one element.
  bool FillInt(uint8_t* base, const FieldInfo& element,
               const Value& value);  ///< Stores an integer.
  bool FillPointer(uint8_t* base, const FieldInfo& element,
                   const Value& value);  ///< Stores a pointer.
};

/**
 * Makes an empty loader; Bus() is nullptr until a Load() or Parse() succeeds.
 */
SsPlusFile::SsPlusFile() : m_Impl(new Impl) {}

/** Frees the loaded export. */
SsPlusFile::~SsPlusFile() = default;

/**
 * Reads an export's C file and parses it (Parse()).
 * @param path The file, e.g. a SigmaStudio+ adi_a2b_busconfig.c.
 * @return Result::kOk; Result::kInvalidArgument if the file can't be read;
 *         Result::kInvalidData if it can't be parsed (Error() says where).
 */
Result SsPlusFile::Load(const char* path) {
  std::FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    m_Impl.reset(new Impl);
    m_Impl->error = std::string("can't read ") + path;
    return Result::kInvalidArgument;
  }
  std::string text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  std::fclose(file);
  return Parse(text.data(), text.size());
}

/**
 * Parses an export's text and fills its variables. What was loaded before is freed, so
 * a BoardConfig imported from it must not be in use.
 * @param text The C text.
 * @param len Its length.
 * @return Result::kOk, with Bus() its sBusDescription; Result::kInvalidData if the text
 *         can't be read as an export (Error() says where). Warnings() lists what was
 *         skipped either way.
 */
Result SsPlusFile::Parse(const char* text, size_t len) {
  m_Impl.reset(new Impl);
  return m_Impl->Run(text, len) ? Result::kOk : Result::kInvalidData;
}

/**
 * The export's sBusDescription, to give to ImportSsPlus().
 * @return It, or nullptr if nothing is loaded.
 */
const ADI_A2B_BCD* SsPlusFile::Bus() const { return m_Impl->bus; }

/**
 * Why the last Load() or Parse() failed, with the line.
 * @return The message, empty after a success.
 */
const std::string& SsPlusFile::Error() const { return m_Impl->error; }

/**
 * What the last Load() or Parse() skipped: fields the header doesn't have, variables of
 * other types, `#define` lines. Each has its line.
 * @return The messages.
 */
const std::vector<std::string>& SsPlusFile::Warnings() const { return m_Impl->warnings; }

/**
 * Tokenises, parses the declarations, then fills the variables.
 * @param text The C text.
 * @param len Its length.
 * @return False on an error (in error).
 */
bool SsPlusFile::Impl::Run(const char* text, size_t len) {
  Lexer lexer(text, len, warnings);
  if (!lexer.Run(tokens, error)) {
    return false;
  }
  while (Peek().type != Token::Type::kEnd) {
    if (!ParseDeclaration()) {
      return false;
    }
  }
  if (!Allocate()) {
    return false;
  }
  FindUsed();
  for (auto& [name, variable] : variables) {
    current = name;
    currentUsed = used.count(name) > 0;
    if (!FillVariable(name, variable)) {
      return false;
    }
  }
  auto found = variables.find("sBusDescription");
  if (found == variables.end() || found->second.info != InfoOf<ADI_A2B_BCD>() ||
      found->second.isArray) {
    return Fail(tokens.back().line, "no ADI_A2B_BCD sBusDescription: not a SigmaStudio+ export");
  }
  bus = reinterpret_cast<const ADI_A2B_BCD*>(found->second.Bytes());
  return true;
}

/** The token @p ahead tokens on (the end token past the end). */
const Token& SsPlusFile::Impl::Peek(size_t ahead) const {
  const size_t index = pos + ahead;
  return (index < tokens.size()) ? tokens[index] : tokens.back();
}

/** True if the token @p ahead tokens on is the punctuation @p punct. */
bool SsPlusFile::Impl::IsPunct(const char* punct, size_t ahead) const {
  const Token& token = Peek(ahead);
  return token.type == Token::Type::kPunct && token.text == punct;
}

/** Takes the punctuation @p punct, or fails. */
bool SsPlusFile::Impl::Expect(const char* punct) {
  if (!IsPunct(punct)) {
    return Fail(Peek().line, std::string("expected '") + punct + "'");
  }
  ++pos;
  return true;
}

/** Records an error at @p line; returns false. */
bool SsPlusFile::Impl::Fail(int line, const std::string& text) {
  if (error.empty()) {
    error = AtLine(line, text);
  }
  return false;
}

/**
 * Parses one top-level declaration: a variable, with or without an initializer. Type
 * definitions, function declarations and pointer variables are skipped.
 * @return False on an error.
 */
bool SsPlusFile::Impl::ParseDeclaration() {
  if (IsPunct(";")) {
    ++pos;
    return true;
  }
  const int line = Peek().line;
  std::vector<std::string> words;
  while (Peek().type == Token::Type::kName) {
    words.push_back(Peek().text);
    ++pos;
  }
  if (words.empty()) {
    return Fail(line, "expected a declaration");
  }
  if (words[0] == "typedef" || words[0] == "extern" || IsPunct("(") || IsPunct("*")) {
    return SkipStatement();
  }
  std::string typeName;
  for (size_t i = 0; i + 1 < words.size(); ++i) {
    const std::string& word = words[i];
    if (word == "static" || word == "const" || word == "volatile" || word == "struct" ||
        word.compare(0, 12, "ADI_A2B_MEM_") == 0) {
      continue;
    }
    typeName += (typeName.empty() ? "" : " ") + word;
  }
  if (typeName.empty()) {
    return Fail(line, "declaration of " + words.back() + " without a type");
  }
  for (;;) {  // Each declarator of the declaration.
    const std::string& name = words.back();
    Variable declared;
    declared.typeName = typeName;
    declared.line = line;
    if (IsPunct("[")) {
      ++pos;
      declared.isArray = true;
      if (!IsPunct("]")) {
        Value size;
        if (!ParseExpression(size) || !ToInt(size)) {
          return false;
        }
        if (size.number <= 0) {
          return Fail(line, "bad array length for " + name);
        }
        declared.hasSize = true;
        declared.count = static_cast<size_t>(size.number);
      }
      if (!Expect("]")) {
        return false;
      }
      if (IsPunct("[")) {
        return Fail(line, name + ": arrays of arrays are not supported");
      }
    }
    if (IsPunct("=")) {
      ++pos;
      declared.hasInit = true;
      if (!ParseInitializer(declared.init)) {
        return false;
      }
    }
    auto found = variables.find(name);
    if (found == variables.end()) {
      variables[name] = std::move(declared);
    } else {  // A tentative declaration, then the definition.
      Variable& existing = found->second;
      if (existing.typeName != declared.typeName || existing.isArray != declared.isArray ||
          (existing.hasSize && declared.hasSize && existing.count != declared.count)) {
        return Fail(line, name + " declared differently at line " + std::to_string(existing.line));
      }
      if (existing.hasInit && declared.hasInit) {
        return Fail(line, name + " defined twice");
      }
      if (declared.hasSize) {
        existing.hasSize = true;
        existing.count = declared.count;
      }
      if (declared.hasInit) {
        existing.hasInit = true;
        existing.init = std::move(declared.init);
        existing.line = line;
      }
    }
    if (IsPunct(",")) {  // Another declarator of the same type.
      ++pos;
      if (Peek().type != Token::Type::kName) {
        return Fail(Peek().line, "expected a name");
      }
      words.back() = Peek().text;
      ++pos;
      continue;
    }
    return Expect(";");
  }
}

/**
 * Skips a declaration the loader doesn't read: up to its ';' outside brackets, or to the
 * closing brace of a function body.
 * @return True.
 */
bool SsPlusFile::Impl::SkipStatement() {
  int depth = 0;
  bool body = false;  // A function body: it ends at its closing brace.
  while (Peek().type != Token::Type::kEnd) {
    const bool closeBrace = IsPunct("}");
    if (IsPunct("{")) {
      body = body || (depth == 0 && pos > 0 && tokens[pos - 1].text == ")");
      ++depth;
    } else if (IsPunct("(")) {
      ++depth;
    } else if (IsPunct(")") || closeBrace) {
      --depth;
    } else if (depth == 0 && IsPunct(";")) {
      ++pos;
      return true;
    }
    ++pos;
    if (closeBrace && depth == 0 && body) {
      return true;
    }
  }
  return true;
}

/**
 * Parses an initializer: a braced list (items with optional designators) or an
 * expression.
 * @param value Gets it.
 * @return False on an error.
 */
bool SsPlusFile::Impl::ParseInitializer(Value& value) {
  value.line = Peek().line;
  if (!IsPunct("{")) {
    return ParseExpression(value);
  }
  ++pos;
  value.type = Value::Type::kList;
  while (!IsPunct("}")) {
    Value item;
    while (IsPunct(".") || IsPunct("[")) {
      Designator designator;
      if (IsPunct(".")) {
        ++pos;
        if (Peek().type != Token::Type::kName) {
          return Fail(Peek().line, "expected a field name after '.'");
        }
        designator.field = Peek().text;
        ++pos;
      } else {
        ++pos;
        Value index;
        if (!ParseExpression(index) || !ToInt(index) || !Expect("]")) {
          return false;
        }
        designator.isIndex = true;
        designator.index = index.number;
      }
      item.designators.push_back(designator);
      if (IsPunct("=")) {
        ++pos;
        break;
      }
    }
    std::vector<Designator> designators = std::move(item.designators);
    if (!ParseInitializer(item)) {
      return false;
    }
    item.designators = std::move(designators);
    value.items.push_back(std::move(item));
    if (IsPunct(",")) {
      ++pos;
    } else if (!IsPunct("}")) {
      return Fail(Peek().line, "expected ',' or '}'");
    }
  }
  ++pos;
  return true;
}

/**
 * Parses terms joined by `|`, `+` and `-` (integers only when there is more than one).
 * @param value Gets the value.
 * @return False on an error.
 */
bool SsPlusFile::Impl::ParseExpression(Value& value) {
  if (!ParseTerm(value)) {
    return false;
  }
  while (IsPunct("|") || IsPunct("+") || IsPunct("-")) {
    const char op = Peek().text[0];
    ++pos;
    Value right;
    if (!ToInt(value) || !ParseTerm(right) || !ToInt(right)) {
      return false;
    }
    value.number = (op == '|')   ? (value.number | right.number)
                   : (op == '+') ? value.number + right.number
                                 : value.number - right.number;
  }
  return true;
}

/**
 * Parses one term: a number, a name, `&name[index]`, a cast, a parenthesised
 * expression, or `-` or `~` before a term.
 * @param value Gets the value.
 * @return False on an error.
 */
bool SsPlusFile::Impl::ParseTerm(Value& value) {
  const Token& token = Peek();
  value.line = token.line;
  if (token.type == Token::Type::kNumber) {
    value.type = Value::Type::kInt;
    value.number = static_cast<int64_t>(token.number);
    ++pos;
    return true;
  }
  if (token.type == Token::Type::kName) {
    value.type = Value::Type::kName;
    value.name = token.text;
    ++pos;
    return true;
  }
  if (IsPunct("&")) {
    ++pos;
    if (Peek().type != Token::Type::kName) {
      return Fail(Peek().line, "expected a variable after '&'");
    }
    value.type = Value::Type::kRef;
    value.name = Peek().text;
    ++pos;
    if (IsPunct("[")) {
      ++pos;
      Value index;
      if (!ParseExpression(index) || !ToInt(index) || !Expect("]")) {
        return false;
      }
      value.index = index.number;
    }
    return true;
  }
  if (IsPunct("-") || IsPunct("~")) {
    const char op = token.text[0];
    ++pos;
    if (!ParseTerm(value) || !ToInt(value)) {
      return false;
    }
    value.number = (op == '-') ? -value.number : ~value.number;
    return true;
  }
  if (IsPunct("(")) {
    ++pos;
    // A cast: type names (and '*'), then ')'.
    size_t ahead = 0;
    while (Peek(ahead).type == Token::Type::kName || (ahead > 0 && IsPunct("*", ahead))) {
      ++ahead;
    }
    if (ahead > 0 && IsPunct(")", ahead) && Peek(0).type == Token::Type::kName &&
        (FindInt(Peek(0).text) != nullptr || FindStruct(Peek(0).text) != nullptr ||
         Peek(0).text == "void" || Peek(0).text == "unsigned" || Peek(0).text == "const")) {
      pos += ahead + 1;
      return ParseTerm(value);
    }
    return ParseExpression(value) && Expect(")");
  }
  if (token.type == Token::Type::kString) {
    return Fail(token.line, "strings are not supported");
  }
  return Fail(token.line, "unexpected '" + token.text + "'");
}

/**
 * Makes @p value an integer: a constant's name becomes its value.
 * @return False if it is not an integer.
 */
bool SsPlusFile::Impl::ToInt(Value& value) {
  if (value.type == Value::Type::kInt) {
    return true;
  }
  if (value.type == Value::Type::kName) {
    if (FindSymbol(value.name, value.number)) {
      value.type = Value::Type::kInt;
      return true;
    }
    return Fail(value.line, "unknown constant " + value.name);
  }
  return Fail(value.line, "expected an integer");
}

/**
 * Finds each variable's type and length and gives it zeroed storage, before any is
 * filled, so that pointers between them can be resolved.
 * @return False on an error.
 */
bool SsPlusFile::Impl::Allocate() {
  for (auto& [name, variable] : variables) {
    variable.info = FindStruct(variable.typeName);
    variable.intType = (variable.info == nullptr) ? FindInt(variable.typeName) : nullptr;
    if (variable.info == nullptr && variable.intType == nullptr) {
      warnings.push_back(AtLine(variable.line, name + " (type " + variable.typeName +
                                                   ") skipped: not a type of the header"));
      continue;
    }
    if (variable.isArray && !variable.hasSize) {
      if (!variable.hasInit || variable.init.type != Value::Type::kList) {
        return Fail(variable.line, name + ": array without a length");
      }
      size_t count = 0;
      size_t next = 0;
      for (const Value& item : variable.init.items) {
        if (!item.designators.empty() && item.designators[0].isIndex) {
          next = static_cast<size_t>(item.designators[0].index);
        }
        count = std::max(count, ++next);
      }
      variable.count = std::max<size_t>(count, 1);
    }
    variable.storage.assign((variable.ElementSize() * variable.count + 7) / 8, 0);
  }
  return true;
}

/**
 * Finds the variables ImportSsPlus() can reach from sBusDescription: the chains, their
 * main nodes, the target's peripherals with their config units and data. Sub-node
 * descriptions (apSlaveConfig) and streams (sStreamSettings and its stream pointers) are
 * not followed: the import doesn't read them, so a constant it doesn't know there is only
 * a warning.
 */
void SsPlusFile::Impl::FindUsed() {
  used.clear();
  std::vector<std::string> todo = {"sBusDescription"};
  while (!todo.empty()) {
    const std::string name = todo.back();
    todo.pop_back();
    auto found = variables.find(name);
    if (found == variables.end() || !used.insert(name).second) {
      continue;
    }
    if (found->second.hasInit) {
      CollectRefs(found->second.init, todo);
    }
  }
}

/**
 * Adds the variables @p value points to (`&name`, `&name[i]`, a bare name that is a
 * variable) to @p names, skipping the items for sub-nodes and streams.
 */
void SsPlusFile::Impl::CollectRefs(const Value& value, std::vector<std::string>& names) const {
  static const char* const kSkipped[] = {"apSlaveConfig", "sStreamSettings", "pSrcStreams",
                                         "pSnkStreams", "pPassThruStreams"};
  if (value.type == Value::Type::kRef ||
      (value.type == Value::Type::kName && variables.count(value.name) > 0)) {
    names.push_back(value.name);
    return;
  }
  if (value.type != Value::Type::kList) {
    return;
  }
  for (const Value& item : value.items) {
    bool skip = false;
    for (const Designator& designator : item.designators) {
      for (const char* skipped : kSkipped) {
        skip = skip || (!designator.isIndex && designator.field == skipped);
      }
    }
    if (!skip) {
      CollectRefs(item, names);
    }
  }
}

/**
 * Fills one variable from its initializer (nothing for a variable without one, or of
 * a type that was skipped).
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillVariable(const std::string& name, Variable& variable) {
  if (!variable.hasInit || (variable.info == nullptr && variable.intType == nullptr)) {
    return true;
  }
  FieldInfo element = {name.c_str(), 0,     false,   1, variable.ElementSize(),
                       Kind::kInt,   false, nullptr, 0};
  if (variable.info != nullptr) {
    element.kind = Kind::kStruct;
    element.type = nullptr;
  } else {
    element.isSigned = variable.intType->isSigned;
  }
  uint8_t* base = variable.Bytes();
  if (variable.info != nullptr) {
    // FillElement() takes the structure from element.type; give it this one directly.
    if (variable.isArray) {
      if (variable.init.type != Value::Type::kList) {
        return Fail(variable.init.line, name + ": an array needs { }");
      }
      size_t index = 0;
      for (const Value& item : variable.init.items) {
        if (!item.designators.empty()) {
          if (!item.designators[0].isIndex) {
            return Fail(item.line, name + ": expected [index]");
          }
          index = static_cast<size_t>(item.designators[0].index);
        }
        if (index >= variable.count) {
          return Fail(item.line, name + ": too many initializers");
        }
        if (item.designators.size() > 1) {
          return Fail(item.line, name + ": nested designators in an array of structures");
        }
        if (!FillStruct(base + index * variable.info->size, *variable.info, item)) {
          return false;
        }
        ++index;
      }
      return true;
    }
    return FillStruct(base, *variable.info, variable.init);
  }
  if (variable.isArray) {
    return FillArray(base, element, variable.count, variable.init);
  }
  return FillElement(base, element, variable.init);
}

/**
 * Fills a structure from a braced list: designated items by name, the others in field
 * order after the last one.
 * @param base The structure.
 * @param info Its type.
 * @param value The list.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillStruct(uint8_t* base, const TypeInfo& info, const Value& value) {
  if (value.type != Value::Type::kList) {
    return Fail(value.line, std::string(info.name) + " needs { }");
  }
  size_t next = 0;
  bool lost = false;  // After an unknown field, positional items can't be placed.
  for (const Value& item : value.items) {
    if (!item.designators.empty()) {
      const Designator& first = item.designators[0];
      if (first.isIndex) {
        return Fail(item.line, std::string(info.name) + " is not an array");
      }
      next = info.fieldCount;
      for (size_t i = 0; i < info.fieldCount; ++i) {
        if (first.field == info.fields[i].name) {
          next = i;
        }
      }
      if (next == info.fieldCount) {
        warnings.push_back(AtLine(
            item.line, std::string(info.name) + " has no field " + first.field + ": skipped"));
        lost = true;
        continue;
      }
      lost = false;
      if (!FillDesignated(base, info.fields[next], item, 1)) {
        return false;
      }
    } else {
      if (lost || next >= info.fieldCount) {
        return Fail(item.line, std::string("too many initializers for ") + info.name);
      }
      if (!FillDesignated(base, info.fields[next], item, 0)) {
        return false;
      }
    }
    ++next;
  }
  return true;
}

/**
 * Fills one field from an item whose designators from @p from on reach inside it.
 * @param base The structure holding the field.
 * @param target The field.
 * @param value The item.
 * @param from The first designator still to apply.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillDesignated(uint8_t* base, const FieldInfo& target, const Value& value,
                                      size_t from) {
  uint8_t* at = base + target.offset;
  if (from >= value.designators.size()) {
    if (target.isArray) {
      return FillArray(at, target, target.count, value);
    }
    return FillElement(at, target, value);
  }
  const Designator& next = value.designators[from];
  if (target.isArray) {
    if (!next.isIndex) {
      return Fail(value.line, std::string(target.name) + " is an array");
    }
    if (next.index < 0 || static_cast<size_t>(next.index) >= target.count) {
      return Fail(value.line, std::string(target.name) + ": index out of range");
    }
    FieldInfo element = target;
    element.offset = 0;
    element.isArray = false;
    return FillDesignated(at + static_cast<size_t>(next.index) * target.stride, element, value,
                          from + 1);
  }
  if (target.kind != Kind::kStruct || next.isIndex) {
    return Fail(value.line, std::string(target.name) + ": designator doesn't fit");
  }
  const TypeInfo& info = *target.type();
  for (size_t i = 0; i < info.fieldCount; ++i) {
    if (next.field == info.fields[i].name) {
      return FillDesignated(at, info.fields[i], value, from + 1);
    }
  }
  warnings.push_back(
      AtLine(value.line, std::string(info.name) + " has no field " + next.field + ": skipped"));
  return true;
}

/**
 * Fills the elements of an array from a braced list.
 * @param base The array.
 * @param element What each element holds (its offset is ignored).
 * @param count Its length.
 * @param value The list.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillArray(uint8_t* base, const FieldInfo& element, size_t count,
                                 const Value& value) {
  if (value.type != Value::Type::kList) {
    return Fail(value.line, std::string(element.name) + " is an array: it needs { }");
  }
  FieldInfo single = element;
  single.offset = 0;
  single.isArray = false;
  size_t index = 0;
  for (const Value& item : value.items) {
    size_t from = 0;
    if (!item.designators.empty()) {
      if (!item.designators[0].isIndex) {
        return Fail(item.line, std::string(element.name) + " is an array");
      }
      index = static_cast<size_t>(item.designators[0].index);
      from = 1;
    }
    if (index >= count) {
      return Fail(item.line, std::string("too many initializers for ") + element.name);
    }
    if (!FillDesignated(base + index * element.stride, single, item, from)) {
      return false;
    }
    ++index;
  }
  return true;
}

/**
 * Fills one element: an integer, a pointer or a structure. A braced list of one value
 * is taken for a scalar, as in C.
 * @param base The element.
 * @param element What it holds.
 * @param value Its initializer.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillElement(uint8_t* base, const FieldInfo& element, const Value& value) {
  if (element.kind == Kind::kStruct) {
    return FillStruct(base, *element.type(), value);
  }
  if (value.type == Value::Type::kList) {
    if (value.items.size() != 1 || !value.items[0].designators.empty()) {
      return Fail(value.line, std::string(element.name) + " takes one value");
    }
    return FillElement(base, element, value.items[0]);
  }
  return (element.kind == Kind::kInt) ? FillInt(base, element, value)
                                      : FillPointer(base, element, value);
}

/**
 * Stores an integer in an element of its size; a value that fits neither its signed nor
 * its unsigned range is an error.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillInt(uint8_t* base, const FieldInfo& element, const Value& value) {
  Value number = value;
  int64_t known = 0;
  if (!currentUsed && value.type == Value::Type::kName && !FindSymbol(value.name, known)) {
    // A constant from a newer SigmaStudio+ in a sub-node description or a stream, which
    // ImportSsPlus() doesn't read.
    warnings.push_back(AtLine(value.line, "unknown constant " + value.name + " in " + current +
                                              ", which the import doesn't use: taken as 0"));
    number.type = Value::Type::kInt;
    number.number = 0;
  }
  if (!ToInt(number)) {
    return false;
  }
  const size_t bits = element.stride * 8;
  if (bits < 64) {
    const int64_t low = -(int64_t{1} << (bits - 1));
    const int64_t high = (int64_t{1} << bits) - 1;
    if (number.number < low || number.number > high) {
      return Fail(value.line, std::string(element.name) + ": value out of range");
    }
  }
  const uint64_t raw = static_cast<uint64_t>(number.number);
  switch (element.stride) {
    case 1: {
      const uint8_t v = static_cast<uint8_t>(raw);
      std::memcpy(base, &v, 1);
      return true;
    }
    case 2: {
      const uint16_t v = static_cast<uint16_t>(raw);
      std::memcpy(base, &v, 2);
      return true;
    }
    case 4: {
      const uint32_t v = static_cast<uint32_t>(raw);
      std::memcpy(base, &v, 4);
      return true;
    }
    case 8:
      std::memcpy(base, &raw, 8);
      return true;
    default:
      return Fail(value.line, std::string(element.name) + ": unsupported size");
  }
}

/**
 * Stores a pointer: null for NULL_PTR, NULL or 0, else the address of a variable's
 * element, whose type must be the one the pointer points to.
 * @return False on an error.
 */
bool SsPlusFile::Impl::FillPointer(uint8_t* base, const FieldInfo& element, const Value& value) {
  void* pointer = nullptr;
  if (value.type == Value::Type::kInt && value.number == 0) {
    std::memcpy(base, &pointer, sizeof(pointer));
    return true;
  }
  if (value.type == Value::Type::kName && (value.name == "NULL_PTR" || value.name == "NULL")) {
    std::memcpy(base, &pointer, sizeof(pointer));
    return true;
  }
  if (value.type != Value::Type::kRef && value.type != Value::Type::kName) {
    return Fail(value.line, std::string(element.name) + " is a pointer");
  }
  auto found = variables.find(value.name);
  if (found == variables.end()) {
    return Fail(value.line, "unknown variable " + value.name);
  }
  Variable& target = found->second;
  if (value.type == Value::Type::kName && !target.isArray) {
    return Fail(value.line, value.name + " is not an array: use &" + value.name);
  }
  const TypeInfo* pointee = element.type();
  const bool fits =
      (pointee != nullptr)
          ? (target.info == pointee)
          : (element.pointeeSize == 0 ||
             (target.intType != nullptr && target.intType->size == element.pointeeSize));
  if (!fits) {
    return Fail(value.line, std::string(element.name) + " can't point to " + value.name + " (" +
                                target.typeName + ")");
  }
  if (value.index < 0 || static_cast<size_t>(value.index) >= target.count) {
    return Fail(value.line, value.name + ": index out of range");
  }
  pointer = target.Bytes() + static_cast<size_t>(value.index) * target.ElementSize();
  std::memcpy(base, &pointer, sizeof(pointer));
  return true;
}

}  // namespace a2b
