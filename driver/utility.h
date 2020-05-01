// vim: set sts=4 ts=8 sw=4 tw=99 et:
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

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <date/date.h>
#include <proto/poll.pb.h>

namespace stone {

typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> FileTime;

Date Today();
Date NextDay(const Date& d);
bool ParseYyyyMmDd(std::string_view text, Date* d);
bool ParseMonthDayYear(std::string_view text, Date* d);
bool ConvertDate(const Date& d, struct tm* tm);

void SortPolls(google::protobuf::RepeatedPtrField<Poll>* polls);

std::string GetExecutableDir();
bool Run(const std::vector<std::string>& argv, const std::string& input = {},
         std::string* output = nullptr);

bool FileExists(std::string_view path);
bool ReadFile(std::string_view path, std::string* data);
bool SaveFile(const std::string& data, std::string_view path);
bool GetFileModTime(std::string_view path, FileTime* time);
bool DaysBetween(const Date& first, const Date& second, int* diff);
int DaysBetween(const Date& first, const Date& second);

int64_t GetUtcTime();
int64_t UtcToLocal(int64_t value);

Date operator +(const Date& d, int days);
Date operator -(const Date& d, int days);

static inline std::ostream& operator <<(std::ostream& os, const Date& date) {
    return os << date.year() << "-" << date.month() << "-" << date.day();
}

static inline bool operator <=(const Date& a, const Date& b) {
    if (a.year() < b.year()) return true;
    if (a.year() > b.year()) return false;
    if (a.month() < b.month()) return true;
    if (a.month() > b.month()) return false;
    if (a.day() > b.day()) return false;
    return true;
}

static inline bool operator <(const Date& a, const Date& b) {
    if (a.year() < b.year()) return true;
    if (a.year() > b.year()) return false;
    if (a.month() < b.month()) return true;
    if (a.month() > b.month()) return false;
    return a.day() < b.day();
}

static inline bool operator >(const Date& a, const Date& b) {
    return b < a;
}

static inline bool operator ==(const Date& a, const Date& b) {
    return a.year() == b.year() && a.month() == b.month() && a.day() == b.day();
}

static inline bool operator !=(const Date& a, const Date& b) {
    return !(a == b);
}

static inline bool operator <(const Poll& a, const Poll& b) {
    return a.end() < b.end();
}

static inline bool operator >(const Poll& a, const Poll& b) {
    return a.end() > b.end();
}

template <typename T>
static inline bool ParseInt(std::string_view text, T* v) {
    errno = 0;
    char* end;
    T value = strtoll(text.data(), &end, 10);
    if (errno != 0 || text.data() == end || *end != '\0') {
        return false;
    }
    if (value > std::numeric_limits<T>::max() || value < std::numeric_limits<T>::min())
        return false;
    *v = static_cast<T>(value);
    return true;
}

static inline bool ParseFloat(std::string_view text, double* d) {
    errno = 0;
    char* end;
    *d = strtod(text.data(), &end);
    if (errno != 0 || text.data() == end || *end != '\0') {
        return false;
    }
    return true;
}

static inline double RoundMargin(double margin)
{
    if (round(margin * 10.0) == 0.0)
        return 0.0;
    return margin;
}

extern std::string gTimezoneName;

} // namespace stone
