#include "Core/json.h"

#include <cstdio>
#include <cstdlib>

namespace {
const Json kNull;

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool ParseDocument(Json& out) {
        SkipSpace();
        if (!ParseValue(out, 0))
            return false;
        SkipSpace();
        return pos_ == s_.size();
    }

private:
    void SkipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\t' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool Consume(const char* word) {
        const size_t n = std::char_traits<char>::length(word);
        if (s_.compare(pos_, n, word) != 0)
            return false;
        pos_ += n;
        return true;
    }

    bool ParseValue(Json& out, int depth) {
        if (depth > 64 || pos_ >= s_.size())
            return false;
        const char c = s_[pos_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!ParseString(str))
                return false;
            out = Json::MakeString(str);
            return true;
        }
        if (Consume("true")) { out = Json::MakeBool(true); return true; }
        if (Consume("false")) { out = Json::MakeBool(false); return true; }
        if (Consume("null")) { out = Json(); return true; }
        return ParseNumber(out);
    }

    bool ParseNumber(Json& out) {
        const char* begin = s_.c_str() + pos_;
        char* end = nullptr;
        const double n = std::strtod(begin, &end);
        if (end == begin)
            return false;
        pos_ += static_cast<size_t>(end - begin);
        out = Json::MakeNumber(n);
        return true;
    }

    bool ParseHex4(unsigned& value) {
        if (pos_ + 4 > s_.size())
            return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = s_[pos_++];
            value <<= 4;
            if (h >= '0' && h <= '9') value |= h - '0';
            else if (h >= 'a' && h <= 'f') value |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') value |= h - 'A' + 10;
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                return false;
            const char e = s_[pos_++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!ParseHex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low))
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: out += e; break;  // " \ /
            }
        }
        return false;
    }

    bool ParseArray(Json& out, int depth) {
        ++pos_;
        out = Json::MakeArray();
        SkipSpace();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipSpace();
            Json item;
            if (!ParseValue(item, depth + 1))
                return false;
            out.Push(std::move(item));
            SkipSpace();
            if (pos_ >= s_.size())
                return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }

    bool ParseObject(Json& out, int depth) {
        ++pos_;
        out = Json::MakeObject();
        SkipSpace();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '"')
                return false;
            std::string key;
            if (!ParseString(key))
                return false;
            SkipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ':')
                return false;
            ++pos_;
            SkipSpace();
            Json value;
            if (!ParseValue(value, depth + 1))
                return false;
            out.Set(key, std::move(value));
            SkipSpace();
            if (pos_ >= s_.size())
                return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
};

void DumpString(const std::string& s, std::string& out) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}
}  // namespace

Json Json::MakeObject() { Json j; j.type_ = Type::Object; return j; }
Json Json::MakeArray() { Json j; j.type_ = Type::Array; return j; }
Json Json::MakeString(const std::string& s) { Json j; j.type_ = Type::String; j.string_ = s; return j; }
Json Json::MakeNumber(double n) { Json j; j.type_ = Type::Number; j.number_ = n; return j; }
Json Json::MakeBool(bool b) { Json j; j.type_ = Type::Bool; j.bool_ = b; return j; }

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object)
        for (size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i] == key)
                return items_[i];
    return kNull;
}

const Json& Json::At(size_t index) const {
    return type_ == Type::Array && index < items_.size() ? items_[index] : kNull;
}

std::string Json::AsString(const std::string& fallback) const {
    return type_ == Type::String ? string_ : fallback;
}
double Json::AsNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
bool Json::AsBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }

Json& Json::Set(const std::string& key, Json value) {
    type_ = Type::Object;
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key) {
            items_[i] = std::move(value);
            return *this;
        }
    keys_.push_back(key);
    items_.push_back(std::move(value));
    return *this;
}

Json& Json::Push(Json value) {
    type_ = Type::Array;
    items_.push_back(std::move(value));
    return *this;
}

bool Json::Parse(const std::string& text, Json& out) {
    Json result;
    if (!Parser(text).ParseDocument(result))
        return false;
    out = std::move(result);
    return true;
}

std::string Json::Dump() const {
    std::string out;
    switch (type_) {
        case Type::Null: out = "null"; break;
        case Type::Bool: out = bool_ ? "true" : "false"; break;
        case Type::Number: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.15g", number_);
            out = buf;
            break;
        }
        case Type::String: DumpString(string_, out); break;
        case Type::Array:
            out = "[";
            for (size_t i = 0; i < items_.size(); ++i)
                out += (i ? "," : "") + items_[i].Dump();
            out += "]";
            break;
        case Type::Object:
            out = "{";
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i)
                    out += ",";
                DumpString(keys_[i], out);
                out += ":" + items_[i].Dump();
            }
            out += "}";
            break;
    }
    return out;
}
