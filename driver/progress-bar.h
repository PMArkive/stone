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

#include <indicators/indicators.hpp>

namespace stone {

class ProgressBar
{
  public:
    ProgressBar(const std::string& title, size_t max) {
        bar_.set_option(indicators::option::MaxProgress{max});
        bar_.set_option(indicators::option::PrefixText{title});
        bar_.print_progress();
    }
    ~ProgressBar() {
        Finish();
    }

    void Increment() {
        bar_.tick();
    }

    void Finish() {
        if (!bar_.is_completed())
            bar_.mark_as_completed();
    }

  private:
    indicators::ProgressBar bar_;
};

struct AutoIncrement {
    ProgressBar* pbar = nullptr;

    explicit AutoIncrement(ProgressBar* pbar)
      : pbar(pbar)
    {}
    ~AutoIncrement() {
        if (pbar)
            pbar->Increment();
    }
    void Cancel() {
        pbar = nullptr;
    }
};

} // namespace stone
