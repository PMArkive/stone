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

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include <proto/model.pb.h>
#include "campaign.h"
#include "context.h"
#include "mathlib.h"

namespace stone {

class Analysis
{
  public:
    Analysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data);

    static double UndecidedFactor(double undecided_pct);

  protected:
    void FindRecentPolls(const google::protobuf::RepeatedPtrField<Poll>& polls,
                         google::protobuf::RepeatedPtrField<Poll>* out);
    void GetWeightedPolls(const google::protobuf::RepeatedPtrField<Poll>& polls,
                          google::protobuf::RepeatedPtrField<Poll>* out);
    void ComputePollModelStats(RaceModel* model);
    double EstimateStdDev(const RaceModel& model);

    virtual double GetMinimumError() = 0;

    static double DemWinProb(double margin, double stddev, double bias = 0.0);
    static double DemWinProb(const RaceModel& model, double bias = 0.0);

    static std::optional<double> GetUndecideds(
        const google::protobuf::RepeatedPtrField<Poll>& polls);

  protected:
    Context* cx_;
    Campaign* cc_;
    const Feed* feed_;
    ModelData* data_;
    double computed_error_;
};

class StateAnalysis : public Analysis
{
  public:
    StateAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data);

    void Analyze();

    // Return a function that can compute a score given a margin bias.
    static std::function<int(double)> GetBiasFn(Campaign* cc, const ModelData* data);
    static Convolver GetConvolverForBias(Campaign* cc, const ModelData* data, double bias);

    // Return the minimum score for D to win. The offset is an optimization. For
    // example, the House has 538 seats, but we only convolve competitive ones.
    // The offset is the delta between the convolved score and the actual score.
    // For example, if only 12 seats are convolved and 180 D points are guaranteed,
    // then score will be 12 and offset will be 180.
    static bool GetScoreToWin(Campaign* cc, const ModelData* data, int* score,
                              int* offset);

    // These are used by SetBayesParameters.
    static inline double GetMetamargin(const ModelData* data) {
        return data->metamargin();
    }
    static inline double GetMetamarginAdjustment(const ModelData*) {
        return 0.0f;
    }

  private:
    void FindRecentPolls(const State& state, RaceModel* model);
    void ComputeState(RaceModel* model);
    double GetMinimumError() override;
};

class SenateAnalysis : public Analysis
{
  public:
    SenateAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data);

    void Analyze();

    static std::function<int(double)> GetBiasFn(Campaign* cc, const ModelData* data);
    static bool GetScoreToWin(Campaign* cc, const ModelData* data, int* score, int* offset);

    // These are used by SetBayesParameters.
    static inline double GetMetamargin(const ModelData* data) {
        return data->senate_mm();
    }
    static inline double GetMetamarginAdjustment(const ModelData*) {
        return 0.0f;
    }

  private:
    void ComputeRace(const Race& race, RaceModel* model);
    double GetMinimumError() override;
};

class GovernorAnalysis : public Analysis
{
  public:
    GovernorAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data);

    void Analyze();

  private:
    void ComputeRace(const Race& race, RaceModel* model);

    double GetMinimumError() override;
};

class HouseAnalysis : public Analysis
{
  public:
    HouseAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data);

    void Analyze(const Date& today);

    static std::function<int(double)> GetBiasFn(Campaign* cc, const ModelData* data);
    static bool GetScoreToWin(Campaign* cc, const ModelData* data, int* score, int* offset);

    // These are used by SetBayesParameters.
    //
    // Many house seats are not polled, or are polled infrequently, which makes
    // the metamargin inaccurate, especially early in the cycle. This makes the
    // Bayesian prior a bit wonky, so instead, we use the generic ballot as a
    // prior instead.  The difference between the generic ballot and the actual
    // metamargin is used correlate the results.
    static inline double GetMetamargin(const ModelData* data) {
        return data->generic_ballot().margin();
    }
    static inline double GetMetamarginAdjustment(const ModelData* data) {
        return data->house_mm() - GetMetamargin(data);
    }

  private:
    void DeriveHouseRatings();
    const HouseRatingMap* UseOldHouseRatings();
    double GetMinimumError() override;
    double EstimateProbability(const std::string& rating, const std::string& presumed_winner);

  private:
    HouseRatingMap derived_ratings_;
};

} // namespace stone
