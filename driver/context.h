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

#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <proto/cache.pb.h>
#include "threadpool.h"

namespace stone {

class Context final
{
  public:
    Context();
    ~Context();

    bool Init(const std::string& settings_file, int num_threads);
    bool WriteCache();

    std::string Download(const std::string& url, bool progress = true);
    std::string DownloadUtf8(const std::string& url, bool progress = true);

    bool Save(const std::string& data, const std::string& path);
    bool Read(const std::string& path, std::string* data);
    bool FileExists(const std::string& path);
    std::string PathTo(const std::string& path);

    std::string GetProp(const std::string& prop, const std::string& default_value = {});
    int GetPropInt(const std::string& prop, int default_value = 0);

    bool GetCache(const std::string& key, std::string* value);
    std::string GetCache(const std::string& key, std::string_view default_value);
    void SetCache(const std::string& key, std::string_view value);
    int64_t GetCacheInt64(const std::string& key, int64_t default_value);

    ThreadPool& workers() const {
        return *workers_.get();
    }

  private:
    bool DownloadUrl(const std::string& url, const std::string& to, bool progress);

  private:
    std::string outdir_;
    std::unique_ptr<ThreadPool> workers_;
    std::unordered_map<std::string, std::string> props_;
    DataCache cache_;
    bool cache_changed_ = false;
};

} // namespace stone
