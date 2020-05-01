// vim: set ts=8 sts=4 sw=4 tw=99 et:
//
// Copyright (C) 2016-2020 David Anderson
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "ini-reader.h"

#include <optional>

#include "logging.h"
#include "utility.h"

namespace stone {

class IniParser
{
  public:
    IniParser()
    {}

    bool Parse(const std::string& text);

  protected:
    virtual void FinishSection(std::string section) = 0;
    virtual void AddKeyValue(std::string key, std::string value) = 0;

  private:
    bool ParseSection(const std::string& text);
    void SkipComment();

    char peek() const {
        if (pos_ >= end_)
            return '\0';
        return *pos_;
    }
    char next() {
        if (pos_ >= end_)
            return '\0';
        if (*pos_ == '\n')
            line_++;
        return *pos_++;
    }
    void SkipSpaces();
    bool ParseKeyValue(IniSection* section);

  private:
    const char* pos_;
    const char* end_;
    unsigned line_ = 1;
};

bool
IniParser::Parse(const std::string& text)
{
    pos_ = text.c_str();
    end_ = pos_ + text.size();

    while (pos_ < end_) {
        char c = next();
        switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                continue;
            case '[':
                if (!ParseSection(text))
                    return false;
                break;
            case ';':
                SkipComment();
                break;
            default:
                Err() << "Unexpected character, on line " << line_;
                return false;
        }
    }
    return true;
}

bool
IniParser::ParseSection(const std::string& text)
{
    SkipSpaces();

    int num_brackets = 1;
    const char* name_start = pos_;
    while (pos_ < end_ && num_brackets > 0) {
        char c = next();
        switch (c) {
            case '[':
                num_brackets++;
                continue;
            case '\0':
                Err() << "Unexpected end of file";
                return false;
            case '\r':
            case '\n':
                Err() << "Unexpected end of line, line " << line_;
                return false;
            case ']':
                num_brackets--;
                break;
        }
    }

    std::string section_name(name_start, pos_ - name_start - 1);
    IniSection section;

    SkipSpaces();
    if (peek() == '\r')
        next();
    if (next() != '\n') {
        Err() << "Unexpected characters after section header, line " << line_;
        return false;
    }

    while (pos_ < end_) {
        SkipSpaces();
        if (peek() == '\0')
            break;
        if (peek() == '\r' || peek() == '\n') {
            next();
            continue;
        }
        if (peek() == ';') {
            SkipComment();
            continue;
        }
        if (peek() == '[')
            break;
        if (!ParseKeyValue(&section))
            return false;
    }

    FinishSection(std::move(section_name));
    return true;
}

bool
IniParser::ParseKeyValue(IniSection* section)
{
    const char* key_start = pos_;
    while (pos_ < end_) {
        if (peek() == '\0' || peek() == '\r' || peek() == '\n') {
            Err() << "Unexpected end of file or line, line " << line_;
            return false;
        }
        if (next() == '=')
            break;
    }

    const char* key_end = pos_;
    std::string key(key_start, key_end - key_start - 1);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
        key.pop_back();

    if (key.empty()) {
        Err() << "Empty key found, line " << line_;
        return false;
    }

    SkipSpaces();

    const char* val_start = pos_;
    while (pos_ < end_) {
        char c = next();
        if (c == '\r' || c == '\n')
            break;
    }

    const char* val_end = pos_;
    std::string val(val_start, val_end - val_start - 1);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
        val.pop_back();

    AddKeyValue(std::move(key), std::move(val));
    return true;
}

void
IniParser::SkipComment()
{
    while (pos_ < end_) {
        char c = next();
        if (c == '\r')
            return;
        if (c == '\n')
            return;
    }
}

void
IniParser::SkipSpaces()
{
    while (pos_ < end_) {
        switch (peek()) {
            case ' ':
            case '\t':
                next();
                break;
            default:
                return;
        }
    }
}

class UnorderedIniParser final : public IniParser
{
  public:
    explicit UnorderedIniParser(IniFile* out)
     : out_(out)
    {}

    void FinishSection(std::string section) override {
        out_->emplace(std::move(section), std::move(section_));
    }
    void AddKeyValue(std::string key, std::string value) override {
        section_.emplace(std::move(key), std::move(value));
    }

  private:
    IniFile* out_;
    IniSection section_;
};

class OrderedIniParser final : public IniParser
{
  public:
    explicit OrderedIniParser(OrderedIniFile* out)
     : out_(out)
    {}

    void FinishSection(std::string section) override {
        out_->emplace_back(std::move(section), std::move(section_));
    }
    void AddKeyValue(std::string key, std::string value) override {
        section_.emplace(std::move(key), std::move(value));
    }

  private:
    OrderedIniFile* out_;
    IniSection section_;
};

bool
ParseIni(std::string_view path, IniFile* out)
{
    std::string contents;
    if (!ReadFile(path, &contents))
        return false;

    UnorderedIniParser parser(out);
    if (!parser.Parse(contents)) {
        Err() << "Failed to parse ini file: " << path;
        return false;
    }
    return true;
}

bool
ParseIni(std::string_view path, OrderedIniFile* out)
{
    std::string contents;
    if (!ReadFile(path, &contents))
        return false;

    OrderedIniParser parser(out);
    if (!parser.Parse(contents)) {
        Err() << "Failed to parse ini file: " << path;
        return false;
    }
    return true;
}

} // namespace stone
