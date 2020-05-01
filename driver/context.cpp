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

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <amtl/experimental/am-argparser.h>
#include <curl/curl.h>
#include <google/protobuf/text_format.h>
#include <openssl/sha.h>
#include <rapidjson/document.h>

#include "context.h"
#include "logging.h"
#include "utility.h"

namespace stone {

using namespace ke;
using namespace std::string_literals;

args::EnableOption sCacheOnly(nullptr, "--cache-only", false, "Use cached URLs if possible");

Context::Context()
{
    // We cache the global timezone early because access to tzname is not thread safe.
    tzset();
    gTimezoneName = tzname[daylight];

    curl_global_init(CURL_GLOBAL_ALL);
}

Context::~Context()
{
    curl_global_cleanup();
    google::protobuf::ShutdownProtobufLibrary();
}

bool
Context::Init(const std::string& settings_file, int num_threads)
{
    if (num_threads < 0)
        num_threads = 8;
    workers_ = std::make_unique<ThreadPool>(num_threads);

    std::string data;
    if (!ReadFile(settings_file, &data))
        return false;

    rapidjson::Document doc;
    doc.Parse(data.c_str());

    for (const auto& m : doc.GetObject()) {
        if (m.value.IsString())
            props_[m.name.GetString()] = m.value.GetString();
        else if (m.value.IsInt())
            props_[m.name.GetString()] = std::to_string(m.value.GetInt());
    }

    outdir_ = GetProp("data-dir");
    if (outdir_.empty()) {
        Err() << "No data-dir found in config";
        return false;
    }

    if (FileExists("cache.bin")) {
        std::string bits;
        if (!Read("cache.bin", &bits)) {
            Err() << "Could not parse cache.";
            return false;
        }
        if (!cache_.ParseFromString(bits)) {
            Err() << "Could not parse cache protobuf.";
            return false;
        }
    }

    return true;
}

bool
Context::WriteCache()
{
    if (!cache_changed_)
        return true;

    std::string str;
    google::protobuf::TextFormat::PrintToString(cache_, &str);
    if (!Save(str, "cache.text"))
        return false;

    str = {};
    cache_.SerializeToString(&str);
    if (!Save(str, "cache.bin"))
        return false;

    cache_changed_ = false;
    return true;
}

static std::string
sha1sum(const std::string& data)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)data.c_str(), data.size(), hash);

    std::string hex;
    for (size_t i = 0; i < sizeof(hash) / sizeof(hash[0]); i++) {
        char hex_rep[3];
        snprintf(hex_rep, sizeof(hex_rep), "%02x", hash[i]);
        hex += hex_rep;
    }
    return hex;
}

std::string
Context::DownloadUtf8(const std::string& url, bool progress)
{
    return Download(url, progress);
}

bool
Context::FileExists(const std::string& path)
{
    return access(PathTo(path).c_str(), F_OK) == 0;
}

bool
Context::Read(const std::string& path, std::string* data)
{
    return ReadFile(PathTo(path), data);
}

std::string
Context::Download(const std::string& url, bool progress)
{
    auto cache_folder = outdir_ + "/cache";
    if (mkdir(cache_folder.c_str(), 0770) && errno != EEXIST) {
        Err() << "mkdir failed: " << strerror(errno) << "";
        return {};
    }

    auto cache_path = cache_folder + "/" + sha1sum(url);
    if (!sCacheOnly.value() || access(cache_path.c_str(), F_OK) != 0) {
        if (!DownloadUrl(url, cache_path, progress))
            return {};
    }

    std::string data;
    if (!ReadFile(cache_path, &data))
        return {};
    return data;
}

bool
Context::DownloadUrl(const std::string& url, const std::string& to, bool progress)
{
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(to.c_str(), "wb"), fclose);

    if (!fp) {
        Err() << "Could not open: " << url << "";
        return false;
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, progress ? 0L : 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, fp.get());
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");

    if (progress)
        Out() << "Downloading " << url << " ...";

    auto code = curl_easy_perform(curl.get());
    if (code != CURLE_OK) {
        Err() << "CURL failed to download " << url << " - " << curl_easy_strerror(code)
                  << "";
        return false;
    }

    return true;
}

bool
Context::Save(const std::string& data, const std::string& path)
{
    auto local_path = PathTo(path);
    return SaveFile(data, local_path);
}

std::string
Context::PathTo(const std::string& path)
{
    return outdir_ + "/" + path;
}

std::string
Context::GetProp(const std::string& prop, const std::string& default_value)
{
    auto iter = props_.find(prop);
    if (iter == props_.end())
        return default_value;
    return iter->second;
}

int
Context::GetPropInt(const std::string& prop, int default_value)
{
    auto iter = props_.find(prop);
    if (iter == props_.end())
        return default_value;

    int v;
    if (!ParseInt(iter->second, &v)) {
        Err() << "Warning: property " << prop << " is not an integer.";
        return default_value;
    }
    return v;
}

bool
Context::GetCache(const std::string& key, std::string* value)
{
    auto iter = cache_.strings().find(key);
    if (iter == cache_.strings().end())
        return false;
    *value = iter->second;
    return true;
}

std::string
Context::GetCache(const std::string& key, std::string_view default_value)
{
    std::string value;
    if (!GetCache(key, &value))
        return std::string(default_value);
    return value;
}

void
Context::SetCache(const std::string& key, std::string_view value)
{
    (*cache_.mutable_strings())[key] = value;
    cache_changed_ = true;
}

int64_t
Context::GetCacheInt64(const std::string& key, int64_t default_value)
{
    std::string text;
    if (!GetCache(key, &text))
        return default_value;

    int64_t value;
    if (!ParseInt(text, &value))
        return default_value;
    return value;
}

} // namespace stone
