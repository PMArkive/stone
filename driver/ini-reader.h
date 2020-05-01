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
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace stone {

typedef std::unordered_map<std::string, std::string> IniSection;
typedef std::unordered_map<std::string, IniSection> IniFile;

typedef std::vector<std::pair<std::string, IniSection>> OrderedIniFile;

bool ParseIni(std::string_view path, IniFile* out);
bool ParseIni(std::string_view path, OrderedIniFile* out);

} // namespace stone
