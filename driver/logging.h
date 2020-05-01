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

#include <iostream>
#include <sstream>

namespace stone {

class LogMessage
{
  public:
    LogMessage(std::ostream& out, int put_errno = 0)
      : out_(out),
        errno_(put_errno)
    {}
    ~LogMessage();

    std::ostream& buffer() {
        return buffer_;
    }

  private:
    std::ostream& out_;
    std::ostringstream buffer_;
    int errno_;
  protected:
    bool abort_ = false;
};

class FatalLogMessage : public LogMessage
{
  public:
    FatalLogMessage()
      : LogMessage(std::cerr)
    {
        abort_ = true;
    }
};

#define Out() LogMessage(std::cout).buffer()

#define Err() LogMessage(std::cerr).buffer()
#define PErr() LogMessage(std::cerr, errno).buffer()
#define Fatal() FatalLogMessage().buffer()
#define PFatal() FatalLogMessage().buffer()

} // namespace stone
