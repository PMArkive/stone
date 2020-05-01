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

#include <proto/history.pb.h>

#include <functional>
#include <vector>

namespace stone {

class Campaign;
class Context;

struct MarginPredictor {
    // Inputs.
    const std::vector<double>* max_swing_by_day;
    double metamargin;
    double swing;
    double prior_mm;
    double prior_swing;
    int score_to_win;
    int score_offset;
    double mm_adjust; // For generic ballot -> house mm.
    std::function<int(double)> bias_fn;

    // Outputs.
    std::vector<double> mm_range;
    std::vector<double> prediction;
    std::vector<double> cs;
};

class Predictor
{
  public:
    Predictor(Context* cx, Campaign* cc, CampaignData* data)
      : cx_(cx), cc_(cc), data_(data)
    {}

    bool Predict();

  private:
    bool PredictDay(ModelData* day);
    void PredictPresident(ModelData* day, int days_left);

    void Bayes(MarginPredictor* mp, Prediction *p, int days_left);

  private:
    Context* cx_;
    Campaign* cc_;
    CampaignData* data_;
    std::vector<const ModelData*> priors_;
    int days_in_campaign_;
};

} // namespace stone
