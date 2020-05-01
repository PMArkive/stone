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
#include "analysis.h"

#include <inttypes.h>
#include <math.h>

#include <deque>
#include <list>
#include <optional>
#include <unordered_map>

#include <amtl/am-string.h>
#include "logging.h"
#include "mathlib.h"
#include "metamargin.h"
#include "utility.h"

namespace stone {

using namespace std::string_literals;

static constexpr double kStateMinError = 3.0;
static constexpr double kSenateMinError = 3.5;
static constexpr double kGovernorMinError = 6.0;
static constexpr double kHouseMinError = 8.0;

Analysis::Analysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data)
  : cx_(cx), cc_(cc), feed_(feed), data_(data)
{
}

StateAnalysis::StateAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data)
  : Analysis(cx, cc, feed, data)
{
}

void
StateAnalysis::Analyze()
{
    if (!feed_->generic_ballot_polls().empty()) {
        RaceModel& model = *data_->mutable_generic_ballot();
        model.set_race_id(0);
        model.set_race_type(Race::GENERIC_BALLOT);

        Analysis::FindRecentPolls(feed_->generic_ballot_polls(), model.mutable_polls());
        ComputePollModelStats(&model);

        // Undecideds will either be from generic ballot polls, or the Campaign.
        data_->set_undecideds(model.undecideds());
    }

    if (!cc_->IsPresidentialYear())
        return;

    {
        RaceModel& model = *data_->mutable_national();
        model.set_race_id(0);
        model.set_race_type(Race::NATIONAL);

        Analysis::FindRecentPolls(feed_->national_polls(), model.mutable_polls());
        ComputePollModelStats(&model);

        // Undecideds will either be from national polls, or the generic ballot,
        // or the Campaign.
        data_->set_undecideds(model.undecideds());
    }

    std::vector<std::pair<int, double>> state_p;

    // Note: the state list is sorted.
    auto& state_models = *data_->mutable_states();
    for (const auto& [name, state] : cc_->States()) {
        auto& state_model = *state_models.Add();
        state_model.set_race_id(state_models.size() - 1);
        state_model.set_race_type(Race::ELECTORAL_COLLEGE);

        FindRecentPolls(state, &state_model);
        ComputeState(&state_model);

        state_p.emplace_back(state.evs(), state_model.win_prob());
    }

    Convolver cv(std::move(state_p));
    data_->set_dem_ev_mode(cv.FindMode());

    // Compute EV mean.
    int mean_ev = cv.FindMean();
    data_->mutable_mean_ev()->set_dem(mean_ev);
    data_->mutable_mean_ev()->set_gop(cc_->TotalEv() - (int)mean_ev);

    cv.CalcConfidence(data_->mutable_dem_ev_range());

    auto bias_fn = GetBiasFn(cc_, data_);
    int ev_needed = GetTiebreakerMajority(cc_->TotalEv());

    MetamarginFinder mmf(std::move(bias_fn), ev_needed - 1, cv.FindMean(), cc_->TotalEv());
    data_->set_metamargin(mmf.metamargin);
}

Convolver
StateAnalysis::GetConvolverForBias(Campaign* cc, const ModelData* data, double bias)
{
    std::vector<std::pair<int, double>> win_p;
    for (const auto& state : data->states()) {
        int evs = cc->state_list()[win_p.size()].evs();
        win_p.emplace_back(evs, DemWinProb(state, bias));
    }
    return Convolver(std::move(win_p));
}

std::function<int(double)>
StateAnalysis::GetBiasFn(Campaign* cc, const ModelData* data)
{
    return [cc, data](double bias) -> int {
        Convolver bias_cv = StateAnalysis::GetConvolverForBias(cc, data, bias);
        return bias_cv.FindMean();
    };
}

bool
StateAnalysis::GetScoreToWin(Campaign* cc, const ModelData*, int* score, int* offset)
{
    *score = GetTiebreakerMajority(cc->TotalEv());
    *offset = 0;
    return true;
}

void
StateAnalysis::ComputeState(RaceModel* model)
{
    ComputePollModelStats(model);

    // Probability of dems winning.
    model->set_win_prob(DemWinProb(*model));
}

void
StateAnalysis::FindRecentPolls(const State& state, RaceModel* model)
{
    if (auto iter = feed_->states().find(state.name()); iter != feed_->states().end())
        Analysis::FindRecentPolls(iter->second.polls(), model->mutable_polls());

    if (!model->polls().empty())
        return;

    auto iter = cc_->AssumedMargins().find(state.name());
    if (iter == cc_->AssumedMargins().end()) {
        Err() << "Could not find assumed margins for: " << state.name() << "";
        abort();
    }

    Poll* poll = model->add_polls();
    poll->set_description(std::to_string(cc_->EndDate().year() - 4) + " election result");
    poll->set_dem(iter->second.first);
    poll->set_gop(iter->second.second);
    poll->set_margin(poll->dem() - poll->gop());
    poll->set_weight(1.0);
    *poll->mutable_start() = cc_->StartDate();
    *poll->mutable_end() = cc_->EndDate();
}

double
StateAnalysis::GetMinimumError()
{
    return kStateMinError;
}

SenateAnalysis::SenateAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data)
  : Analysis(cx, cc, feed, data)
{
}

void
SenateAnalysis::Analyze()
{
    if (cc_->senate_map().races().empty())
        return;
    if (feed_->senate_polls().empty())
        return;

    int total_seats = cc_->senate_map().seats().dem() + cc_->senate_map().seats().gop();
    int dem_seats_needed = cc_->senate_map().dem_seats_for_control();

    auto safe_seats = data_->mutable_senate_safe_seats();
    safe_seats->set_dem(cc_->senate_map().seats().dem() - cc_->senate_map().seats_up().dem());
    safe_seats->set_gop(cc_->senate_map().seats().gop() - cc_->senate_map().seats_up().gop());

    std::vector<double> seat_p;

    auto& senate_models = *data_->mutable_senate_races();
    size_t index = 0;
    for (const auto& race : cc_->senate_map().races()) {
        auto& model = *senate_models.Add();
        model.set_race_id(index);
        model.set_race_type(Race::SENATE);

        auto iter = feed_->senate_polls().find(index);
        if (iter != feed_->senate_polls().end())
            FindRecentPolls(iter->second.polls(), model.mutable_polls());
        ComputeRace(race, &model);

        if (model.polls().empty() && !model.rating().empty()) {
            if (model.rating() == "dem")
                safe_seats->set_dem(safe_seats->dem() + 1);
            else if (model.rating() == "gop")
                safe_seats->set_gop(safe_seats->gop() + 1);
        } else {
            seat_p.emplace_back(model.win_prob());
        }
        index++;
    }

    data_->set_senate_control_alt_seats(dem_seats_needed ^ 1);

    auto& makeup = *data_->mutable_senate_median();
    if (seat_p.empty()) {
        makeup.set_dem(safe_seats->dem());
        makeup.set_gop(total_seats - makeup.dem());
        return;
    }

    Convolver cv(seat_p);
    makeup.set_dem(safe_seats->dem() + cv.FindMean());
    makeup.set_gop(total_seats - makeup.dem());

    cv.CalcConfidence(data_->mutable_dem_senate_range(), safe_seats->dem());

    // Only compute a metamargin if the senate can flip.
    if (safe_seats->dem() >= dem_seats_needed || safe_seats->gop() >= dem_seats_needed) {
        data_->set_senate_can_flip(false);
        return;
    }

    assert(dem_seats_needed > safe_seats->dem());
    data_->set_senate_can_flip(true);

    auto bias_fn = GetBiasFn(cc_, data_);

    MetamarginFinder mmf(std::move(bias_fn), dem_seats_needed - safe_seats->dem() - 1,
                         cv.FindMean(), (int)seat_p.size());
    data_->set_senate_mm(mmf.metamargin);
}

std::function<int(double)>
SenateAnalysis::GetBiasFn(Campaign*, const ModelData* data)
{
    return [data](double bias) -> int {
        std::vector<double> win_p;
        for (const auto& race : data->senate_races()) {
            if (race.polls().empty() && !race.rating().empty())
                continue;
            win_p.emplace_back(DemWinProb(race, bias));
        }

        Convolver bias_cv(win_p);
        return bias_cv.FindMean();
    };
}

bool
SenateAnalysis::GetScoreToWin(Campaign* cc, const ModelData* data, int* score, int* offset)
{
    if (!data->senate_can_flip())
        return false;

    int dem_seats_needed = cc->senate_map().dem_seats_for_control();
    int dem_safe_seats = data->senate_safe_seats().dem();
    assert(dem_seats_needed > dem_safe_seats);

    *score = dem_seats_needed - dem_safe_seats;
    *offset = dem_safe_seats;
    return true;
}

void
SenateAnalysis::ComputeRace(const Race& race, RaceModel* model)
{
    assert(race.type() == Race::SENATE);

    if (model->polls().empty()) {
        // Note: all parameters will be zero by default.
        model->set_rating(race.presumed_winner());
        if (model->rating() == "dem") {
            model->set_win_prob(1.0);
        } else if (model->rating() != "gop") {
            assert(model->rating().empty());
            model->set_win_prob(0.5);
            model->set_undecideds(data_->national().undecideds());
            model->set_stddev(kSenateMinError);
        }
        return;
    }

    ComputePollModelStats(model);

    // Probability of dems winning.
    model->set_win_prob(DemWinProb(*model));
}

double
SenateAnalysis::GetMinimumError()
{
    return kSenateMinError;
}

GovernorAnalysis::GovernorAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data)
  : Analysis(cx, cc, feed, data)
{
}

void
GovernorAnalysis::Analyze()
{
    if (cc_->governor_map().races().empty())
        return;
    if (feed_->governor_polls().empty())
        return;

    std::vector<double> seat_p;

    auto& governor_models = *data_->mutable_gov_races();
    for (const auto& race : cc_->governor_map().races()) {
        auto& model = *governor_models.Add();
        model.set_race_id(race.race_id());
        model.set_race_type(Race::GOVERNOR);

        auto iter = feed_->governor_polls().find(race.race_id());
        if (iter != feed_->governor_polls().end())
            FindRecentPolls(iter->second.polls(), model.mutable_polls());
        ComputeRace(race, &model);
        seat_p.emplace_back(model.win_prob());
    }

    // This helper function is to avoid very complex expressions when computing
    // the metamargin.
    int total_seats = cc_->governor_map().seats().dem() + cc_->governor_map().seats().gop();
    int dem_start_seats = cc_->governor_map().seats().dem() - cc_->governor_map().seats_up().dem();

    Convolver cv(seat_p);

    auto& makeup = *data_->mutable_gov_median();
    makeup.set_dem(dem_start_seats + cv.FindMean());
    makeup.set_gop(total_seats - makeup.dem());
}

void
GovernorAnalysis::ComputeRace(const Race& race, RaceModel* model)
{
    assert(race.type() == Race::GOVERNOR);

    if (model->polls().empty()) {
        if (race.presumed_winner() == "dem")
            model->set_win_prob(1.0);
        model->set_rating(race.presumed_winner());
        return;
    }

    ComputePollModelStats(model);

    // Probability of dems winning.
    if (!model->polls().empty())
        model->set_win_prob(1.0 - NormalCdf(0.0, model->mean(), model->stddev()));
}

double
GovernorAnalysis::GetMinimumError()
{
    return kGovernorMinError;
}

HouseAnalysis::HouseAnalysis(Context* cx, Campaign* cc, const Feed* feed, ModelData* data)
  : Analysis(cx, cc, feed, data)
{
}

void
HouseAnalysis::Analyze(const Date& today)
{
    const auto& house_polls = feed_->house_polls();
    const auto* house_ratings = &feed_->house_ratings();
    if (house_ratings->empty()) {
        // Try to derive pre-recorded ratings.
        DeriveHouseRatings();
        house_ratings = &derived_ratings_;
    }
    if (data_->date() != today && today != cc_->EndDate()) {
        // This is a backdated run. Try to use the historical ratings we saved
        // permanently, because unlike poll lists, ratings are not otherwise
        // dated.
        if (auto found = UseOldHouseRatings(); found != nullptr)
            house_ratings = found;
    }

    // This state is used to track the "safe" makeup of the house. In years
    // where we've bothered to fill the entire house makeup, we can just
    // count safe seats directly. Otherwise, we impute by assuming that all
    // missing house races are safe.
    int safe_dem = 0;
    int safe_gop = 0;
    int unsafe_dem = 0;
    int unsafe_gop = 0;
    int flips_to_dem = 0;
    int flips_to_gop = 0;

    // Analyze individual races.
    std::vector<double> win_p;
    for (const auto& race : cc_->house_map().races()) {
        const HouseRating* hr = nullptr;
        if (auto hr_iter = house_ratings->find(race.race_id()); hr_iter != house_ratings->end())
            hr = &hr_iter->second;

        RaceModel model;
        model.set_race_id(race.race_id());
        model.set_race_type(Race::HOUSE);
        if (auto iter = house_polls.find(race.race_id()); iter != house_polls.end())
            FindRecentPolls(iter->second.polls(), model.mutable_polls());
        if (hr) {
            if (hr->rating() != "tossup")
                model.set_rating(hr->rating() + " " + hr->presumed_winner());
            else
                model.set_rating(hr->rating());
        }

        if (!model.polls().empty()) {
            ComputePollModelStats(&model);
            model.set_win_prob(DemWinProb(model));
        } else {
            std::string_view rating;
            std::string_view presumed_winner;
            if (hr) {
                rating = hr->rating();
                presumed_winner = hr->presumed_winner();
                model.set_win_prob(EstimateProbability(hr->rating(), hr->presumed_winner()));
            } else if (!race.presumed_winner().empty()) {
                presumed_winner = race.presumed_winner();
            } else {
                presumed_winner = race.current_holder();
            }

            // If we don't have an incumbent, and we don't have a rating, we
            // have no way of estimating what is going on with this seat.
            if (race.current_holder().empty() && rating.empty() && presumed_winner.empty())
                Fatal() << "No rating or incumbency for seat: " << race.region();

            // If a seat has no rating, we assume it's safe.
            if (rating.empty() || rating == "safe") {
                if (presumed_winner == "gop") {
                    safe_gop++;
                    if (race.current_holder() == "dem") {
                        flips_to_gop++;
                        unsafe_dem++;
                    }
                } else if (presumed_winner == "dem") {
                    safe_dem++;
                    if (race.current_holder() == "gop") {
                        flips_to_dem++;
                        unsafe_gop++;
                    }
                } else {
                    Fatal() << "No presumed winner for safe seat: " << race.region();
                }

                // Don't add safe races to the convolution process, it just
                // slows it down and we already know the outcome.
                continue;
            }
        }

        if (race.current_holder() == "dem")
            unsafe_dem++;
        else if (race.current_holder() == "gop")
            unsafe_gop++;

        win_p.emplace_back(model.win_prob());
        *data_->mutable_house_races()->Add() = std::move(model);
    }
    if (win_p.empty())
        return;

    // In non-census years, we're lazy and we don't enumerate all of the house
    // seats. When this is the case, we must recompute which seats are safe,
    // by counting flips. We can't do this in census years because when seats
    // are created or destroyed, there is no way of calculating the delta
    // until after the election.
    if (cc_->house_map().total_seats() != cc_->house_map().races().size()) {
        safe_dem = cc_->house_map().seats().dem() - unsafe_dem + flips_to_dem;
        safe_gop = cc_->house_map().seats().gop() - unsafe_gop + flips_to_gop;
    }

    assert(win_p.size() + safe_dem + safe_gop == (size_t)cc_->house_map().total_seats());

    int total_seats = cc_->house_map().total_seats();
    int majority_seats = GetTiebreakerMajority(total_seats);

    Convolver cv(win_p);
    cv.CalcConfidence(data_->mutable_dem_house_range(), safe_dem);

    auto seats = data_->mutable_house_median();
    seats->set_dem(cv.FindMean() + safe_dem);
    seats->set_gop(total_seats - seats->dem());

    auto safe_seats = data_->mutable_house_safe_seats();
    safe_seats->set_dem(safe_dem);
    safe_seats->set_gop(safe_gop);

    // If a majority change is not at all possible, we can't compute a metamargin.
    if (safe_dem >= majority_seats || safe_gop >= majority_seats)
        return;

    data_->set_house_can_flip(true);

    auto bias_fn = GetBiasFn(cc_, data_);

    MetamarginFinder mmf(std::move(bias_fn), majority_seats - safe_dem - 1, cv.FindMean(),
                         (int)data_->house_races().size());
    data_->set_house_mm(mmf.metamargin);
}

void
HouseAnalysis::DeriveHouseRatings()
{
    const auto& races = cc_->house_map().races();
    for (size_t i = 0; i < (size_t)races.size(); i++) {
        const auto& race = races[i];

        if (race.rating().empty())
            continue;

        HouseRating hr;
        hr.set_presumed_winner(race.presumed_winner());
        hr.set_rating(race.rating());
        hr.set_race_id((int)i);
        derived_ratings_[i] = std::move(hr);
    }
}

const HouseRatingMap*
HouseAnalysis::UseOldHouseRatings()
{
    const HouseRatingMap* candidate = nullptr;
    for (const auto& entry : cc_->house_history().entries()) {
        if (entry.date() > data_->date())
            break;
        candidate = &entry.ratings();
    }
    return candidate;
}

std::function<int(double)>
HouseAnalysis::GetBiasFn([[maybe_unused]] Campaign* cc, const ModelData* data)
{
    // Try to build a margin list for computing a meta-margin.
    std::vector<std::pair<double, double>> margins;
    for (const auto& race : data->house_races()) {
        if (!race.polls().empty()) {
            margins.emplace_back(race.margin(), race.stddev());
        } else {
            static const double kEstimatedError = kHouseMinError;
            margins.emplace_back(InverseCdf(0.0, 1.0 - race.win_prob(), kEstimatedError),
                                 kEstimatedError);
            if (margins.back().first == INFINITY)
                margins.back().first = 24.0;
            else if (margins.back().first == -INFINITY)
                margins.back().first = -24.0;
        }
    }
    assert((int)margins.size() + data->house_safe_seats().dem() + data->house_safe_seats().gop() ==
           cc->house_map().total_seats());

    return [margins{std::move(margins)}](double bias) -> int {
        std::vector<double> win_p;
        for (const auto& pair : margins)
            win_p.emplace_back(DemWinProb(pair.first, pair.second, bias));
        return Convolver(win_p).FindMean();
    };
}

bool
HouseAnalysis::GetScoreToWin(Campaign* cc, const ModelData* data, int* score, int* offset)
{
    if (!data->house_can_flip())
        return false;

    int majority = GetTiebreakerMajority(cc->house_map().total_seats());
    int safe_dem = data->house_safe_seats().dem();
    assert(majority > safe_dem);

    *score = majority - safe_dem;
    *offset = safe_dem;
    return true;
}

double
HouseAnalysis::EstimateProbability(const std::string& rating, const std::string& presumed_winner)
{
    auto parts = ke::Split(rating, " ");
    auto winner = presumed_winner;
    if (parts.size() == 2)
        winner = parts[1];
    auto score = parts[0];

    double p;
    if (score == "tossup") {
        p = 0.5;
    } else if (score == "leans") {
        p = 0.7;
    } else if (score == "likely") {
        p = 0.85;
    } else if (score == "safe") {
        p = 1.0;
    } else {
        Err() << "Unknown hr rating: " << rating;
        p = 0.5;
    }
    if (winner == "gop")
        p = 1.0 - p;
    return p;
}

double
HouseAnalysis::GetMinimumError()
{
    return kHouseMinError;
}

static bool
SamePollDate(const Poll& a, const Poll& b)
{
    return a.start() == b.start() && a.end() == b.end();
}

static inline int
SampleTypeScore(const std::string& sample_type)
{
    if (sample_type == "lv")
        return 4;
    if (sample_type == "rv")
        return 3;
    if (sample_type == "a")
        return 2;
    return 0;
}

static bool
IsBetterPoll(const Poll& a, const Poll& b)
{
    if (a.sample_type() != b.sample_type())
        return SampleTypeScore(a.sample_type()) > SampleTypeScore(b.sample_type());
    return a.sample_size() > b.sample_size();
}

typedef std::map<std::string, std::vector<Poll>> PollsterMap;

static void
AddPollToMap(PollsterMap* map, const Poll& poll)
{
    auto& batch = (*map)[poll.description()];

    auto iter = batch.begin();
    while (iter != batch.end()) {
        if (iter->tracking() && poll.tracking()) {
            // Older version of tracking poll. Throw away.
            if (iter->end() > poll.end())
                return;
            // Newer version. remove the current.
            if (iter->end() < poll.end()) {
                iter = batch.erase(iter);
                continue;
            }
        }

        // If this is a duplicate, we either remove it, or replace the
        // existing poll if the new one has a better sample.
        if (SamePollDate(poll, *iter)) {
            if (IsBetterPoll(poll, *iter)) {
                *iter = poll;
                return;
            }
            if (IsBetterPoll(*iter, poll)) {
                // Exclude this poll entirely.
                return;
            }
            // If we get here, there were two polls on the same date with
            // equal sample types, so use both and they'll get averaged.
        }
        iter++;
    }
    batch.emplace_back(poll);
}

static inline int
GetPollWindow(const Date& election_date, const Date& window_start)
{
    static const int kMinDaysInWindow = 7;
    static const int kMaxDaysInWindow = 14;

    // For the last days of the election, we want the minimum window.
    // Before that, use the maximum window.
    static const int kMaxWindowEnd = 28;
    static const int kMinWindowStart = 7;

    int diff = DaysBetween(window_start, election_date);
    if (diff > kMaxWindowEnd)
        return kMaxDaysInWindow;
    if (diff <= kMinWindowStart)
        return kMinDaysInWindow;

    // For example if there are 20 days left in the cycle (from the poll
    // start), we use 20 - 14 = 6, the stepping lasts (28 - 14) = 14 days.
    // The number of steps is (14 - 7) = 7, so:
    //    (7 * 6) / 14.
    //
    // Examples:
    // 28 days left: (7 * (28 - 14)) / 14 = 7 days + min window
    // 21 days left: (7 * (21 - 14)) / 14 = 4 days + min window
    // 16 days left: (7 * (21 - 14)) / 14 = 1 day + min window
    int days_until_min_window = diff - kMinWindowStart;
    int window = (kMaxWindowEnd - kMinWindowStart);
    int days = kMinDaysInWindow +
               RoundToNearest((7.0 * double(days_until_min_window)) / double(window));
    assert(days >= kMinDaysInWindow && days <= kMaxDaysInWindow);
    return days;
}

void
Analysis::FindRecentPolls(const google::protobuf::RepeatedPtrField<Poll>& polls,
                          google::protobuf::RepeatedPtrField<Poll>* out)
{
    if (polls.empty())
        return;

    auto iter = polls.begin();
    while (iter != polls.end()) {
        if (iter->end() <= data_->date())
            break;
        iter++;
    }
    if (iter == polls.end())
        return;

    // We want at least 4 polls, even if they're not within the same time
    // window.
    static const size_t kMinPolls = 4;

    // This structure allows us to group polls that overlap, to de-weight
    // clusters from the same pollster.
    PollsterMap staging;

    // This gets filled in based on the most recent poll. This allows us
    // to keep poll lists consistent until new data comes in, otherwise,
    // the data would change every day making things harder to understand.
    // We don't do anything like 538 and try to correct a lack of polls
    // by using national trends.
    std::optional<Date> earliest;

    Date cutoff = cc_->StartDate() - 60;

    for (; iter < polls.end(); iter++) {
        const auto& poll = *iter;

        // Don't use ridiculously early polls. Our cutoff is 2 months from
        // before the official start of what we consider the campaign to be.
        if (poll.start() < cutoff)
            continue;

        // Only go past the most recent week if we don't have many samples.
        // Note that we count *pollsters* and not polls, since we don't want
        // three polls from the same pollster to knock out other candidates.
        if (earliest.has_value() && poll.end() <= earliest.value() && staging.size() >= kMinPolls)
            break;

        // Try to make the behavior of new runs to be the same as backdated
        // runs, by not including polls until they were published.
        if (poll.has_published() && poll.published() > data_->date())
            continue;

        if (!earliest.has_value())
            earliest = {poll.end() - GetPollWindow(cc_->EndDate(), poll.end())};

        AddPollToMap(&staging, poll);
    }

    // If each pollster has one poll, all polls will be weighted equally (1/N).
    // Otherwise, all polls by the same pollster will be de-weighted equally
    // eg:
    //   Pollster A 5/5, weight: 1/3
    //   Pollster B 5/6, weight: 1/6  <- Halved
    //   Pollster B 5/7, weight: 1/6  <- Halved
    //   Pollster C 5/8, weight: 1/3
    //
    // Total weight: 1.0.
    for (auto& pair : staging) {
        auto& batch = pair.second;
        for (auto& poll : batch) {
            poll.set_weight(1.0 / (double(batch.size() * staging.size())));
            *out->Add() = std::move(poll);
        }
    }
    SortPolls(out);
}

double
Analysis::DemWinProb(double margin, double stddev, double bias)
{
    return 1.0 - NormalCdf(0.0, margin + bias, stddev);
}

double
Analysis::DemWinProb(const RaceModel& model, double bias)
{
    return DemWinProb(model.mean(), model.stddev(), bias);
}

double
Analysis::UndecidedFactor(double undecided_pct)
{
    // Given 10% undecided voters, a 50-50 split would result in no change to
    // the margin. However, a 60-40 split would result in a 2 point change. In
    // a race with 5% undecided voters, a 60-40 split would result in a 1 point
    // change. We take the maximum movement of undecideds to be a 65-35 split.
    // Derivation, for a population |p|, undecided percent |u|, and split bound
    // |X|:
    //
    //    p - p*u
    //    -------  + p*u*X
    //       2
    //    ------------------
    //            p
    //
    // Which simplifies down to: |u * X|.
    //
    // (This also factors in 3rd party votes.)
    return (undecided_pct * 0.65) - (undecided_pct * 0.35);
}

std::optional<double>
Analysis::GetUndecideds(const google::protobuf::RepeatedPtrField<Poll>& polls)
{
    std::vector<double> undecideds;
    for (const auto& poll : polls) {
        if (poll.dem() && poll.gop()) {
            auto undecided = 100.0 - poll.dem() - poll.gop();
            if (undecided >= 0.0)
                undecideds.emplace_back(undecided);
        }
    }
    if (undecideds.empty())
        return {};
    return {Average(undecideds)};
}

void
Analysis::ComputePollModelStats(RaceModel* model)
{
    assert(!model->polls().empty());

    double weighted_average = 0.0;
    std::vector<double> margins;
    for (const auto &poll : model->polls()) {
        weighted_average += poll.margin() * poll.weight();
        margins.emplace_back(poll.margin());
    }

    // Round to three significant digits. This works around something like:
    //     2.0 * .33333... +
    //     1.0 * .33333... +
    //    -3.0 * .33333...
    // Averaging to an extremely small number close to, but not equal, to zero.
    // Such idiosyncracies are common with IEEE floats, but when it's close to
    // zero, it (1) risks not being considered a tie, and (2) renders weird in
    // the HTML generator as a result (R+0.00 or D+0.00).
    //
    // To avoid all this, we just round to a few significant digits.
    weighted_average = double(RoundToNearest(weighted_average * 1000.0f)) / 1000.0f;

    // Note: we don't bother with a weighted median since we don't use the
    // median anywhere yet.
    model->set_mean(weighted_average);
    model->set_median(Median(margins));
    model->set_margin(model->mean());

    auto undecideds = GetUndecideds(model->polls());
    if (!undecideds.has_value()) {
        if (cc_->IsPresidentialYear() && data_->national().undecideds())
            undecideds = {data_->national().undecideds()};
        else if (data_->generic_ballot().undecideds())
            undecideds = {data_->generic_ballot().undecideds()};
        else
            undecideds = {cc_->UndecidedPercent()};
    }
    model->set_undecideds(undecideds.value());

    if (model->race_type() == Race::NATIONAL) {
        model->set_stddev(StandardDeviation(margins));
    } else {
        double stddev = 0.0;
        double expected_error = EstimateStdDev(*model);
        if (model->polls().size() > 1)
            stddev = SampleStdDev(margins);
        model->set_stddev(std::max(expected_error, stddev));
    }
}

double
Analysis::EstimateStdDev(const RaceModel& model)
{
    // There are a few sources of error in polls.
    //
    // First, they have a margin of error of +/- 3.5 to 5 points. Eg, a race with
    // candidates 50-50 and a MoE of 5, a 55-45 or 45-55 outcome is possible; eg
    // a swing of 10 points in either direction.
    //
    // Furthermore, polls themselves can be off. The average according to 538 is
    // a miss of the margin of 4 points. This can be due to things like undecided
    // voters or mismodeling of the elecorate. This means the 55-45 outcome could
    // really be 57-43, for a margin of 16!
    //
    // Since we are averaging polls, we use the sample standard deviation as a
    // baseline, and make sure it is less than the expected error. The expected
    // error is computed as the baseline error (~4 for presidential races, and
    // ~5 for governor/state, and ~6 for house). Then, we add the potential
    // swing from undecided voters.
    return std::max(GetMinimumError(), UndecidedFactor(model.undecideds()));
}

} // namespace stone
