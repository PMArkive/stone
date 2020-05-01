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
#include "metamargin.h"

#include "logging.h"
#include "utility.h"

namespace stone {

bool MetamarginFinder::Debug = false;

MetamarginFinder::MetamarginFinder(std::function<int(double)> in_bias_fn, int midpoint, int start,
                                   int high)
  : bias_fn(std::move(in_bias_fn))
{
    if (midpoint != start) {
        metamargin = Calc(midpoint, start > midpoint ? -1 : 1);
    } else if (start == 0) {
        metamargin = -Calc(midpoint, 1);
    } else if (start == high) {
        metamargin = Calc(midpoint, -1);
    } else {
        // Go both directions, take whichever result is closer.
        double mm1 = Calc(midpoint, 1);
        double mm2 = Calc(midpoint, -1);
        metamargin = (abs(mm1) > abs(mm2)) ? mm2 : mm1;
    }
}

double
MetamarginFinder::Calc(int midpoint, int direction)
{
    double bias, step;
    if (direction < 0) {
        bias = 0.0;
        step = -0.02;
    } else {
        bias = 0.02;
        step = 0.02;
    }

    double metamargin = 0.0f;
    while (true) {
        int median_ev = bias_fn(bias);
        if (Debug)
            printf("bias = %f  result = %d\n", bias, median_ev);
        if (bias > 101.0 || bias < -101.0) {
            Err() << "Something has gone very wrong, metamargin > 100";
            abort();
        }
        if ((direction < 0 && median_ev <= midpoint) || (direction > 0 && median_ev >= midpoint)) {
            metamargin = -bias;
            break;
        }
        bias += step;
    }
    return RoundMargin(metamargin);
}

} // namespace stone
