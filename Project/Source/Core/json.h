#pragma once

#include <string>
#include <vector>

// Small JSON value: enough to read Claude Code's stream-json output and to write
// conversation logs. Strings are UTF-8.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static Json MakeObject();
    static Json MakeArray();
    static Json MakeString(const std::string& s);
    static Json MakeNumber(double n);
    static Json MakeBool(bool b);

    Type type() const { return type_; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsString() const { return type_ == Type::String; }
    bool IsNull() const { return type_ == Type::Null; }

    // Missing keys / wrong types give a null value, so lookups can be chained.
    const Json& operator[](const std::string& key) const;
    const Json& At(size_t index) const;
    size_t Size() const { return type_ == Type::Array ? items_.size() : keys_.size(); }

    std::string AsString(const std::string& fallback = std::string()) const;
    double AsNumber(double fallback = 0) const;
    bool AsBool(bool fallback = false) const;

    Json& Set(const std::string& key, Json value);  // object
    Json& Push(Json value);                          // array

    static bool Parse(const std::string& text, Json& out);
    std::string Dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> items_;        // array items, or object values
    std::vector<std::string> keys_;  // object keys, parallel to items_
};
