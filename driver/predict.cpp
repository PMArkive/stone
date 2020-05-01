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
#include "predict.h"

#include "analysis.h"
#include "campaign.h"
#include "context.h"
#include "logging.h"
#include "mathlib.h"
#include "metamargin.h"
#include "progress-bar.h"
#include "utility.h"

namespace stone {

static const std::vector<double> kMaxNationalSwing = {
    0.00, 0.04, 0.26, 0.86, 1.02, 1.08, 1.20, 1.42, 1.54, 1.90, 2.06, 2.06, 2.06, 2.40, 2.40, 2.40,
    2.70, 2.70, 3.18, 3.18, 3.20, 3.48, 3.48, 3.48, 3.48, 3.48, 3.48, 3.48, 3.48, 3.48, 3.74, 4.32,
    4.44, 4.52, 4.62, 4.84, 5.34, 5.68, 6.20, 6.20, 6.20, 6.30, 6.52, 6.70, 6.76, 7.04, 7.04, 7.04,
    7.08, 7.08, 7.08, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18,
    7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18,
    7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18,
    7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18,
    7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18,
    7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.18, 7.38, 7.78,
    7.78, 7.78, 7.86, 7.90, 8.02, 8.08, 8.18
};

static const std::vector<double> kMaxBallotSwing_PresYear = {
    0.00, 0.57, 0.69, 1.02, 1.02, 1.02, 1.40, 2.83, 3.45, 3.45, 3.58, 3.58, 3.58, 3.58, 3.75, 3.75,
    3.75, 3.75, 3.75, 3.75, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12,
    4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12, 4.12,
    4.12, 4.12, 5.49, 5.49, 5.49, 5.49, 6.19, 7.33, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58,
    7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58, 7.58,
    7.58, 7.58, 7.58, 8.25, 8.25, 8.25, 8.25, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50,
    8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50,
    8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50, 8.50,
    9.25, 9.25, 9.25, 9.25, 9.25, 9.65, 9.65, 9.65, 9.65, 9.65, 9.65, 11.00
};

static const std::vector<double> kMaxBallotSwing_Midterm = {
    0.00, 0.39, 2.04, 2.62, 2.62, 3.54, 3.54, 3.54, 3.54, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51,
    4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 4.51, 5.18,
    5.18, 5.18, 5.18, 5.35, 5.35, 5.35, 5.35, 5.35, 7.67, 7.67, 7.67, 7.67, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.86,
    7.86, 7.86, 7.86, 7.86, 7.86, 7.86, 7.98, 9.27, 11.27
};

template <class AT>
static void
SetBayesParameters(MarginPredictor* mp, Campaign* cc, const ModelData* day,
                   const std::vector<const ModelData*>& priors)
{
    mp->metamargin = AT::GetMetamargin(day);
    mp->swing = Analysis::UndecidedFactor(day->undecideds());

    std::vector<double> prior_mm, prior_swing;
    for (const auto& day : priors) {
        prior_mm.emplace_back(AT::GetMetamargin(day));
        prior_swing.emplace_back(day->undecideds());
    }

    if (prior_mm.empty())
        mp->prior_mm = AT::GetMetamargin(day);
    else
        mp->prior_mm = Average(prior_mm);

    if (prior_swing.empty())
        mp->prior_swing = day->undecideds();
    else
        mp->prior_swing = Average(prior_swing);

    mp->prior_swing = std::max(6.0f, (float)Analysis::UndecidedFactor(mp->prior_swing));
    mp->bias_fn = AT::GetBiasFn(cc, day);
    mp->mm_adjust = AT::GetMetamarginAdjustment(day);

    // Debugging.
#if 0
    printf("mm=%f prior_mm=%f swing=%f prior_Swing=%f\n",
            mp->metamargin,
            mp->prior_mm,
            mp->swing,
            mp->prior_swing);
#endif

    if (!AT::GetScoreToWin(cc, day, &mp->score_to_win, &mp->score_offset)) {
        mp->score_to_win = 0;
        mp->score_offset = 0;
    }
}

bool
Predictor::Predict()
{
    if (!DaysBetween(cc_->StartDate(), cc_->EndDate(), &days_in_campaign_))
        return false;

    auto& history = *data_->mutable_history();

    ProgressBar pbar("Predicting      ", history.size());

    bool should_predict = false;
    for (auto iter = history.rbegin(); iter != history.rend(); iter++) {
        if (iter->generated() >= data_->last_updated())
            should_predict = true;

        if (should_predict && !PredictDay(&*iter))
            return false;
        priors_.emplace_back(&*iter);
        pbar.Increment();
    }
    pbar.Finish();

    return true;
}

[[maybe_unused]] static void
DumpP(const Prediction &p)
{
    printf("avg = %f (score=%d) win_p = %f\n", p.metamargin(), p.average(), p.dem_win_p());
    printf("1sig = [%f, %f]   2sig= [%f, %f]\n",
        p.mm_1sig().low(),
        p.mm_1sig().high(),
        p.mm_2sig().low(),
        p.mm_2sig().high());
    printf("SCORE 1sig = [%d, %d]   2sig= [%d, %d]\n",
        p.score_1sig().low(),
        p.score_1sig().high(),
        p.score_2sig().low(),
        p.score_2sig().high());
}

bool
Predictor::PredictDay(ModelData* day)
{
    int days_left;
    if (!DaysBetween(day->date(), cc_->EndDate(), &days_left))
        return false;

    if (cc_->IsPresidentialYear()) {
        auto* p = day->mutable_ec_prediction();

        MarginPredictor mp;
        mp.max_swing_by_day = &kMaxNationalSwing;
        SetBayesParameters<StateAnalysis>(&mp, cc_, day, priors_);
        Bayes(&mp, p, days_left);

        Convolver cv =
            StateAnalysis::GetConvolverForBias(cc_, day, p->metamargin() - day->metamargin());
        day->set_predicted_dem_ev_mode(cv.FindMode());

        // Hack: for now, clamp win P for presidential races, since the prediction
        // doesn't fat have enough tails.
        p->set_dem_win_p(std::clamp(p->dem_win_p(), 0.5, 0.95));
    }

    if (!day->senate_races().empty()) {
        MarginPredictor mp;
	if (cc_->IsPresidentialYear())
	    mp.max_swing_by_day = &kMaxBallotSwing_PresYear;
	else
	    mp.max_swing_by_day = &kMaxBallotSwing_Midterm;
        SetBayesParameters<SenateAnalysis>(&mp, cc_, day, priors_);
        Bayes(&mp, day->mutable_senate_prediction(), days_left);

        int dem_seats_to_control = cc_->senate_map().dem_seats_for_control();
        int alt_delta = dem_seats_to_control - day->senate_control_alt_seats();

        double win_prob_inv = 1.0;
        int alt_seats = mp.score_to_win - alt_delta;
        for (size_t i = 0; i < mp.mm_range.size(); i++) {
            if (mp.bias_fn(mp.mm_range[i] - mp.metamargin) >= alt_seats) {
                if (i == 0)
                    win_prob_inv = 0.0;
                else
                    win_prob_inv = mp.cs[i - 1];
                break;
            }
        }
        day->set_senate_win_prob_alt(1.0 - win_prob_inv);
    }

    if (day->house_can_flip()) {
        MarginPredictor mp;
	if (cc_->IsPresidentialYear())
	    mp.max_swing_by_day = &kMaxBallotSwing_PresYear;
	else
	    mp.max_swing_by_day = &kMaxBallotSwing_Midterm;
        SetBayesParameters<HouseAnalysis>(&mp, cc_, day, priors_);
        Bayes(&mp, day->mutable_house_prediction(), days_left);
    }
    return true;
}

static double GetWinP(MarginPredictor* mp, const std::vector<double>& cs)
{
    for (size_t i = 0; i < cs.size(); i++) {
        if (mp->mm_range[i] < 0.0)
            continue;
        if (mp->bias_fn(mp->mm_range[i] - mp->metamargin) >= mp->score_to_win) {
            if (i == 0)
                return 1.0;
            return 1.0 - cs[i - 1];
        }
    }
    return 0.0f;
}

void
Predictor::Bayes(MarginPredictor* mp, Prediction *p, int days_left)
{
    double min_swing = mp->max_swing_by_day->back();
    if (size_t(days_left) < mp->max_swing_by_day->size())
        min_swing = mp->max_swing_by_day->at(days_left);

    // Empirically, the metamargin is off by ~2 points each election.
    min_swing = std::max(min_swing, 2.0);

    double swing = std::max(mp->swing, min_swing);

    // Get a four-sigma range of metamargin values.
    double mm_4sig_low = mp->metamargin - 4 * swing;
    double mm_4sig_high = mp->metamargin + 4 * swing;
    for (double mm = mm_4sig_low; mm <= mm_4sig_high; mm += 0.02)
        mp->mm_range.emplace_back(mm);

    auto& mm_range = mp->mm_range;

    std::vector<double> now;
    for (const auto& mm : mm_range)
        now.emplace_back(Tpdf((mm - mp->metamargin) / swing, 3));
    double now_sum = Sum(now);

    std::vector<double> prior;
    for (const auto& mm : mm_range)
        prior.emplace_back(Tpdf((mm - mp->prior_mm) / mp->prior_swing, 1));
    double prior_sum = Sum(prior);

    double pred_sum = 0;
    for (size_t i = 0; i < now.size(); i++) {
        mp->prediction.emplace_back((now[i] / now_sum) * (prior[i] / prior_sum));
        pred_sum += mp->prediction.back();
    }
    for (auto& v : mp->prediction)
        v /= pred_sum;

    double predicted_mm = WeightedAverage(mm_range, mp->prediction);
    p->set_metamargin(RoundMargin(predicted_mm));

    mp->cs = Cumsum(mp->prediction);

    // The metamargin represents the movement toward a tie. For the EC it's
    // fine to use 0.0 as the win point, because the outcomes tend to cluster
    // close together. But for the senate, the difference between 50 and 51
    // seats can be a steep cliff. So, we walk the MM list and find the first
    // margin to bring us to a win. We do know however that this will be at a
    // margin of >= 0, so we can optimize the walk a bit.
    //
    // Note that we clamp the result to not go below 0.01 or above 0.99. A 0%
    // or 100% chance does not make sense as long as both candidates are
    // running.
    if (mp->score_to_win > 0) {
        p->set_dem_win_p(std::clamp(GetWinP(mp, mp->cs), 0.01, 0.99));
    }

    // Walk the cumulative sum looking for interesting points (sorted).
    std::vector<double> points = {
        NormalCdf(-2.0, 0.0, 1.0),
        NormalCdf(-1.0, 0.0, 1.0),
        NormalCdf(1.0, 0.0, 1.0),
        NormalCdf(2.0, 0.0, 1.0),
    };
    auto pt_iter = points.begin();
    auto cs_iter = mp->cs.begin();
    auto mm_iter = mm_range.begin();
    while (pt_iter != points.end()) {
        double result = mm_range.back();
        while (cs_iter != mp->cs.end() && mm_iter != mm_range.end()) {
            if (*cs_iter >= *pt_iter) {
                result = *mm_iter;
                break;
            }
            cs_iter++;
            mm_iter++;
        }
        *pt_iter = RoundMargin(result);
        pt_iter++;
    }

    // This is the only place where we need to account for the metamargin
    // adjustment (eg generic ballot to house conversion). Everywhere else,
    // we work in terms of deltas between the prior and prediction, so the
    // absolute values don't matter.
    auto adj_points = points;
    for (auto& point : adj_points)
        point += mp->mm_adjust;
    p->mutable_mm_2sig()->set_low(adj_points[0]);
    p->mutable_mm_1sig()->set_low(adj_points[1]);
    p->mutable_mm_1sig()->set_high(adj_points[2]);
    p->mutable_mm_2sig()->set_high(adj_points[3]);

    if (!mp->score_to_win)
        return;

    std::vector<int> scores;
    for (const auto& mm : points) {
        int score = mp->bias_fn(mm - mp->metamargin);
        scores.emplace_back(score + mp->score_offset);
    }
    p->mutable_score_2sig()->set_low(scores[0]);
    p->mutable_score_1sig()->set_low(scores[1]);
    p->mutable_score_1sig()->set_high(scores[2]);
    p->mutable_score_2sig()->set_high(scores[3]);
    p->set_average(mp->bias_fn(p->metamargin() - mp->metamargin) + mp->score_offset);
}

} // namespace stone

