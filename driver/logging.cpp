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
#include "logging.h"

#include <errno.h>
#include <string.h>

#include <mutex>

namespace stone {

std::mutex sIoMutex;

LogMessage::~LogMessage()
{
    {
        std::lock_guard<std::mutex> lock(sIoMutex);
        if (errno_)
            out_ << buffer_.str() << ": " << strerror(errno_) << std::endl;
        else
            out_ << buffer_.str() << std::endl;
    }
    errno = errno_;
    if (abort_)
        abort();
}

} // namespace stone

