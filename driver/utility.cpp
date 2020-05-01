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
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <utility>

#include <amtl/am-string.h>
#include <amtl/am-time.h>
#include "logging.h"
#include "utility.h"

namespace stone {

std::string gTimezoneName;

void
SortPolls(google::protobuf::RepeatedPtrField<Poll>* polls)
{
    std::sort(polls->begin(), polls->end(), std::greater<Poll>());
}

static Date
FromYmd(const date::year_month_day& ymd)
{
    Date date;
    date.set_year((int)ymd.year());
    date.set_month((unsigned)ymd.month());
    date.set_day((unsigned)ymd.day());
    return date;
}

Date
Today()
{
    time_t time_sec = time(nullptr);
    struct tm tm;
    if (!localtime_r(&time_sec, &tm)) {
        PErr() << "localtime_r";
        abort();
    }
    Date date;
    date.set_year(1900 + tm.tm_year);
    date.set_month(tm.tm_mon + 1);
    date.set_day(tm.tm_mday);
    return date;
}

Date
operator +(const Date& d, int days)
{
    auto now = date::year_month_day(date::year(d.year()), date::month(d.month()),
                                    date::day(d.day()));
    auto next = date::year_month_day(date::sys_days{now} + date::days{days});
    return FromYmd(next);
}

Date
operator -(const Date& d, int days)
{
    auto now = date::year_month_day(date::year(d.year()), date::month(d.month()),
                                    date::day(d.day()));
    auto next = date::year_month_day(date::sys_days{now} - date::days{days});
    return FromYmd(next);
}

Date
NextDay(const Date& d)
{
    return d + 1;
}

bool
ParseYyyyMmDd(std::string_view text, Date* date)
{
    int year, month, day;
    if (sscanf(text.data(), "%d-%d-%d", &year, &month, &day) != 3)
        return false;
    date->set_year(year);
    date->set_month(month);
    date->set_day(day);
    return true;
}

bool
ParseMonthDayYear(std::string_view text, Date* d)
{
    int year, month, day;
    if (sscanf(text.data(), "%d/%d/%d", &month, &day, &year) != 3)
        return false;
    d->set_year(year);
    d->set_month(month);
    d->set_day(day);
    return true;
}

bool
ConvertDate(const Date& d, struct tm* tm)
{
    auto str = ke::StringPrintf("%d-%d-%d", (int)d.year(), (int)d.month(), (int)d.day());
    if (!strptime(str.c_str(), "%Y-%m-%d", tm))
        return false;
    return true;
}

std::string
GetExecutableDir()
{
    std::unique_ptr<char, decltype(&free)> ptr(nullptr, ::free);
    ptr.reset(realpath("/proc/self/exe", nullptr));
    if (!ptr) {
        Err() << "realpath /proc/self/exe failed: " << strerror(errno) << "";
        return {};
    }
    return dirname(ptr.get());
}

class AutoCloseFd final
{
  public:
    AutoCloseFd()
      : fd_(-1)
    {}
    AutoCloseFd(AutoCloseFd&& other) {
        reset(other.fd_);
        other.fd_ = -1;
    }
    explicit AutoCloseFd(int fd)
      : fd_(fd)
    {}
    AutoCloseFd(const AutoCloseFd&) = delete;

    ~AutoCloseFd() {
        reset();
    }

    void reset(int fd = -1) {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

    AutoCloseFd& operator =(const AutoCloseFd& other) = delete;
    AutoCloseFd& operator =(AutoCloseFd&& other) {
        reset(other.fd_);
        other.fd_ = -1;
        return *this;
    }
    operator int() const {
        return fd_;
    }

  private:
    int fd_;
};

static bool
Write(int fd, const void* buffer, size_t size)
{
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer);
    size_t remaining = size;

    while (remaining) {
        ssize_t r = TEMP_FAILURE_RETRY(write(fd, ptr, remaining));
        if (r == -1) {
            Err() << "write failed: " << strerror(errno) << "";
            return false;
        }
        ptr += r;
        remaining -= size_t(r);
    }
    return true;
}

class Pipe
{
  public:
    Pipe() {}

    bool Create() {
        int array[2];
        if (pipe2(array, O_CLOEXEC)) {
            PErr() << "pipe2";
            return false;
        }
        rd_.reset(array[0]);
        wr_.reset(array[1]);
        return true;
    }

    AutoCloseFd& rd() {
        return rd_;
    }
    AutoCloseFd& wr() {
        return wr_;
    }

  private:
    AutoCloseFd rd_;
    AutoCloseFd wr_;
};

class SpawnActions
{
  public:
    ~SpawnActions() {
        if (initialized_)
            posix_spawn_file_actions_destroy(&actions_);
    }
    bool Init() {
        if (posix_spawn_file_actions_init(&actions_)) {
            PErr() << "posix_spawn_file_actions_init";
            return false;
        }
        return true;
    }
    bool AddDup2(int a, int b) {
        if (posix_spawn_file_actions_adddup2(&actions_, a, b)) {
            PErr() << "posix_spawn_file_actions_adddup2";
            return false;
        }
        return true;
    }
    bool AddClose(int a) {
        if (posix_spawn_file_actions_addclose(&actions_, a)) {
            PErr() << "posix_spawn_file_actions_addclose";
            return false;
        }
        return true;
    }

    posix_spawn_file_actions_t* get() {
        return &actions_;
    }

  private:
    bool initialized_ = false;
    posix_spawn_file_actions_t actions_;
};

bool
Run(const std::vector<std::string>& argv, const std::string& input, std::string* output)
{
    Pipe in, out;
    if (!in.Create() || !out.Create())
        return false;

    std::vector<const char*> tmp_argv;
    for (const auto& arg : argv)
        tmp_argv.emplace_back(arg.data());
    tmp_argv.emplace_back(nullptr);

    SpawnActions actions;
    if (!actions.Init())
        return false;

    if (output) {
        if (!actions.AddDup2(out.wr(), STDOUT_FILENO) || !actions.AddClose(out.rd()))
            return false;
    }
    if (!input.empty()) {
        if (!actions.AddDup2(in.rd(), STDIN_FILENO) || !actions.AddClose(in.wr()))
            return false;
    }

    pid_t pid;
    int err = posix_spawn(&pid, argv[0].c_str(), actions.get(), nullptr,
                          const_cast<char**>(tmp_argv.data()), environ);
    if (err) {
        PErr() << "posix_spawn";
        return false;
    }

    out.wr().reset();
    in.rd().reset();

    if (!input.empty()) {
        if (!Write(in.wr(), input.data(), input.size()))
            return false;
        in.wr().reset();
    }

    // Read stdout.
    if (output) {
        std::unique_ptr<FILE, decltype(&fclose)> fp(nullptr, fclose);
        fp.reset(fdopen(dup(out.rd()), "rb"));
        if (!fp) {
            PErr() << "fdopen";
            return false;
        }
        out = {};

        output->clear();

        char line[1024];
        while (fgets(line, sizeof(line), fp.get()) != nullptr)
            *output += line;
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        PErr() << "waitpid";
        return false;
    }
    if (!WIFEXITED(status)) {
        Err() << "process did not exit normally: " << status << "";
        return false;
    }
    if (WEXITSTATUS(status)) {
        Err() << "process exited with code: " << WEXITSTATUS(status) << "";
        return false;
    }
    return true;
}

bool
FileExists(std::string_view path)
{
    return access(path.data(), F_OK) == 0;
}

bool
ReadFile(std::string_view path, std::string* data)
{
    std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(path.data(), "rb"), fclose);
    if (!fp) {
        Err() << "open " << path << " failed: " << strerror(errno) << "";
        return false;
    }

    struct stat s;
    if (fstat(fileno(fp.get()), &s)) {
        Err() << "stat " << path << " failed: " << strerror(errno) << "";
        return false;
    }

    *data = std::string(s.st_size, '\0');

    std::string& buffer = *data;
    if (fread(&buffer[0], 1, buffer.size(), fp.get()) != buffer.size()) {
        Err() << "read " << path << " failed: " << strerror(errno) << "";
        return false;
    }
    return true;
}

bool
SaveFile(const std::string& data, std::string_view path)
{
    std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(path.data(), "wb"), fclose);
    if (!fp) {
        Err() << "Could not open path for writing: " << path << "";
        return false;
    }
    if (fwrite(data.data(), 1, data.size(), fp.get()) != data.size()) {
        Err() << "Failed to write to: " << path << "";
        return false;
    }
    return true;
}

int64_t
GetUtcTime()
{
    time_t rawtime;
    time(&rawtime);

    struct tm tm;
    if (!gmtime_r(&rawtime, &tm))
        return static_cast<uint64_t>(rawtime);
    tm.tm_isdst = -1;

    return mktime(&tm);
}

int64_t
UtcToLocal(int64_t value)
{
    return value - timezone + (daylight ? 3600 : 0);
}

bool
GetFileModTime(std::string_view path, FileTime* time)
{
    struct stat s;
    if (stat(path.data(), &s) < 0) {
        PErr() << "stat failed: " << path;
        return false;
    }
    *time = ke::TimespecToTimePoint<std::chrono::system_clock>(s.st_mtim);
    return true;
}

bool
DaysBetween(const Date& first, const Date& second, int* diff)
{
    struct tm tm_a = { 0, 0, 0, first.day(), first.month() - 1, first.year() - 1900 };
    struct tm tm_b = { 0, 0, 0, second.day(), second.month() - 1, second.year() - 1900 };
    time_t time_a = mktime(&tm_a);
    if (time_a == (time_t)-1) {
        PErr() << "mktime failed";
        return false;
    }
    time_t time_b = mktime(&tm_b);
    if (time_b == (time_t)-1) {
        PErr() << "mktime failed";
        return false;
    }
    *diff = difftime(time_b, time_a) / (60 * 60 * 24);
    return true;
}

int
DaysBetween(const Date& first, const Date& second)
{
    int diff;
    if (!DaysBetween(first, second, &diff))
        abort();
    return diff;
}

} // namespace stone
