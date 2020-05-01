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
#include <stdio.h>
#include <sysexits.h>

#include <iostream>
#include <list>
#include <memory>

#include <amtl/experimental/am-argparser.h>
#include <google/protobuf/text_format.h>
#include <proto/history.pb.h>
#include <proto/poll.pb.h>
#include "analysis.h"
#include "campaign.h"
#include "context.h"
#include "datasource-538.h"
#include "htmlgen.h"
#include "logging.h"
#include "mathlib.h"
#include "predict.h"
#include "progress-bar.h"
#include "utility.h"

using namespace ke;

args::StringOption settings_file("settings_file", "Settings file");
args::EnableOption reset_history(nullptr, "--reset-history", false, "Reset history (do not import)");
args::EnableOption skip_html(nullptr, "--skip-html", false, "Do not generate HTML");
args::IntOption num_threads(nullptr, "--num-threads", ke::Some(-1), "Number of threads");

namespace stone {

class Driver final
{
  public:
    Driver(Context* cx, Campaign* cc);

    bool Run();

  private:
    bool ImportHistory();
    void RunForDay(const Date& date, const Feed* feed);
    bool Export();
    void BuildFeedFromResults();

  private:
    Context* cx_;
    Campaign* cc_;
    Date today_;
    Feed feed_;
    Feed results_feed_;
    CampaignData out_;
    std::list<ModelData> history_;
    std::list<ModelData>::iterator history_pos_;
    std::vector<std::function<void(ThreadPool*)>> work_;
};

Driver::Driver(Context* cx, Campaign* cc)
  : cx_(cx),
    cc_(cc),
    today_(Today())
{
    if (today_ > cc->EndDate())
        today_ = cc->EndDate();
}

bool
Driver::Run()
{
    auto feed = cc_->Fetch(cx_);
    if (!feed) {
        Err() << "No feeds found.";
        return false;
    }
    feed_ = std::move(*feed);

    SortPolls(feed_.mutable_national_polls());
    SortPolls(feed_.mutable_generic_ballot_polls());
    for (auto& [_, state] : *feed_.mutable_states())
        SortPolls(state.mutable_polls());
    for (auto& [_, list] : *feed_.mutable_senate_polls())
        SortPolls(list.mutable_polls());
    for (auto& [_, list] : *feed_.mutable_governor_polls())
        SortPolls(list.mutable_polls());

    *out_.mutable_feed_info() = feed_.info();
    *out_.mutable_senate() = cc_->senate_map();
    *out_.mutable_governor_map() = cc_->governor_map();
    *out_.mutable_house_map() = cc_->house_map();
    out_.set_presidential_year(cc_->IsPresidentialYear());
    out_.set_dem_pres(cc_->dem_pres());
    out_.set_gop_pres(cc_->gop_pres());
    out_.set_last_updated(GetUtcTime());
    for (const auto& important_date : cc_->important_dates())
        *out_.mutable_important_dates()->Add() = important_date;

    if (!ImportHistory()) {
        Err() << "Failed to import history.";
        return false;
    }
    history_pos_ = history_.begin();

    bool has_final_results = false;
    if (today_ == cc_->EndDate() && !cc_->race_results().empty()) {
        BuildFeedFromResults();
        has_final_results = true;
    }

    Date day = cc_->StartDate();
    while (day <= today_) {
        RunForDay(day, &feed_);
        day = NextDay(day);
    }
    if (has_final_results)
        RunForDay(day, &results_feed_);

    // Stuff starts getting submitted to the worker pool right here.
    ProgressBar pbar("Analyzing polls ", work_.size());
    for (auto& fn : work_) {
        auto inc_pbar = [&pbar]() -> void {
            pbar.Increment();
        };
        cx_->workers().Do(std::move(fn), std::move(inc_pbar));
    }
    work_.clear();

    // Wait for workers to finish up.
    cx_->workers().RunCompletionTasks();
    pbar.Finish();

    out_.clear_history();
    for (auto iter = history_.rbegin(); iter != history_.rend(); iter++)
        *out_.add_history() = std::move(*iter);
    history_.clear();

    out_.clear_states();
    for (const auto& state : cc_->state_list())
        *out_.add_states() = state;
    for (const auto& [state_name, state_code] : kStateCodes)
        (*out_.mutable_state_codes())[state_name] = state_code;

    *out_.mutable_election_day() = cc_->EndDate();

    Predictor pr(cx_, cc_, &out_);
    pr.Predict();

    if (!Export())
        return false;

    if (!skip_html.value()) {
        Renderer renderer(cx_, out_);
        if (!renderer.Generate())
            return false;
        cx_->WriteCache();
    }
    return true;
}

void
Driver::RunForDay(const Date& date, const Feed* feed)
{
    while (history_pos_ != history_.end()) {
        if (history_pos_->date() < date) {
            history_pos_++;
            continue;
        }
        if (history_pos_->date() == date)
            break;
    }

    ModelData* data;
    if (history_pos_ == history_.end() || history_pos_->date() > date) {
        history_pos_ = history_.emplace(history_pos_);
    } else {
        assert(history_pos_->date() == date);
        if (history_pos_->date() < today_)
            return;
        *history_pos_ = {};
    }
    data = &*history_pos_;

    // Note: the worker thread does not modify any data. The history is
    // populated on the main thread. The "models" variable is considered
    // stable even if history_pos_ or the list changes, because it is a
    // linked list. A vector would not work.
    auto work = [this, date, data, feed](ThreadPool* pool) -> void {
        *data->mutable_date() = date;
        data->set_generated(GetUtcTime());

        {
            StateAnalysis sa(cx_, cc_, feed, data);
            sa.Analyze();
        }
        {
            SenateAnalysis sa(cx_, cc_, feed, data);
            sa.Analyze();
        }
        {
            GovernorAnalysis ga(cx_, cc_, feed, data);
            ga.Analyze();
        }
        {
            HouseAnalysis ha(cx_, cc_, feed, data);
            ha.Analyze(today_);
        }
    };
    work_.emplace_back(std::move(work));
}

bool
Driver::ImportHistory()
{
    if (reset_history.value())
        return true;
    if (!cx_->FileExists("history.bin"))
        return true;

    std::string bits;
    if (!cx_->Read("history.bin", &bits)) {
        Err() << "History protobuf is empty.";
        return false;
    }

    CampaignData data;
    if (!data.ParseFromString(bits)) {
        Err() << "Could not parse history protobuf.";
        return false;
    }

    if (data.election_day() != cc_->EndDate()) {
        Err() << "Saved campaign end date " << data.election_day()
              << " does not match settings: " << cc_->EndDate();
        return false;
    }

    for (auto& entry : *data.mutable_history())
        history_.emplace_front(std::move(entry));
    return true;
}

bool
Driver::Export()
{
    std::string str;
    google::protobuf::TextFormat::PrintToString(out_, &str);
    if (!cx_->Save(str, "history.text"))
        return false;

    str = {};
    out_.SerializeToString(&str);
    if (!cx_->Save(str, "history.bin"))
        return false;

    return cx_->WriteCache();
}

static Poll
MakePollFromMargins(const Date& date, const std::pair<double, double>& margins)
{
    Poll poll;
    poll.set_description(std::to_string(date.year()) + " Election Results");
    *poll.mutable_start() = date;
    *poll.mutable_end() = date;
    poll.set_dem(margins.first);
    poll.set_gop(margins.second);
    poll.set_margin(margins.first - margins.second);
    return poll;
}

void
Driver::BuildFeedFromResults()
{
    results_feed_.mutable_info()->set_description("Final Results");
    results_feed_.mutable_info()->set_short_name("final_results");
    results_feed_.mutable_info()->set_feed_type("normal");

    auto ri = cc_->race_results().find(Race::ELECTORAL_COLLEGE);
    if (ri != cc_->race_results().end()) {
        const auto& races = ri->second;
        for (const auto& [race_id, margins] : races) {
            const auto& state = cc_->state_list()[race_id];

            PollList pl;
            *(pl.mutable_polls()->Add()) = MakePollFromMargins(today_, margins);
            (*results_feed_.mutable_states())[state.name()] = std::move(pl);
        }
    }

    ri = cc_->race_results().find(Race::SENATE);
    if (ri != cc_->race_results().end()) {
        const auto& races = ri->second;
        for (const auto& [race_id, margins] : races) {
            PollList pl;
            *(pl.mutable_polls()->Add()) = MakePollFromMargins(today_, margins);
            (*results_feed_.mutable_senate_polls())[race_id] = std::move(pl);
        }
    }

    ri = cc_->race_results().find(Race::HOUSE);
    if (ri != cc_->race_results().end()) {
        const auto& races = ri->second;
        for (const auto& [race_id, margins] : races) {
            PollList pl;
            *(pl.mutable_polls()->Add()) = MakePollFromMargins(today_, margins);
            (*results_feed_.mutable_house_polls())[race_id] = std::move(pl);
        }
    }

    ri = cc_->race_results().find(Race::GOVERNOR);
    if (ri != cc_->race_results().end()) {
        const auto& races = ri->second;
        for (const auto& [race_id, margins] : races) {
            PollList pl;
            *(pl.mutable_polls()->Add()) = MakePollFromMargins(today_, margins);
            (*results_feed_.mutable_governor_polls())[race_id] = std::move(pl);
        }
    }

    auto it = cc_->national_race_results().find(Race::ELECTORAL_COLLEGE);
    if (it != cc_->national_race_results().end()) {
        MapEv* evs = out_.mutable_results()->mutable_evs();
        evs->set_dem((int)it->second.first);
        evs->set_gop((int)it->second.second);
    }

    it = cc_->national_race_results().find(Race::NATIONAL);
    if (it != cc_->national_race_results().end())
        *results_feed_.mutable_national_polls()->Add() = MakePollFromMargins(today_, it->second);

    it = cc_->national_race_results().find(Race::GENERIC_BALLOT);
    if (it != cc_->national_race_results().end()) {
        *results_feed_.mutable_generic_ballot_polls()->Add() =
            MakePollFromMargins(today_, it->second);
    }
}

} // namespace stone

using namespace stone;

int main(int argc, char** argv)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    args::Parser parser(nullptr);
    if (!parser.parse(argc, argv)) {
        parser.usage(stderr, argc, argv);
        return EX_USAGE;
    }

    auto cx = std::make_unique<Context>();
    if (!cx->Init(settings_file.value().c_str(), num_threads.value()))
        return EX_USAGE;

    int campaign_year = cx->GetPropInt("year", 0);
    if (campaign_year == 0) {
        Err() << "Missing valid year for campaign.";
        return EX_USAGE;
    }

    auto cc = std::make_unique<Campaign>();
    if (!cc->Init(cx.get(), campaign_year)) {
        Err() << "No campaign found for given year";
        return EX_USAGE;
    }

    Driver driver(cx.get(), cc.get());
    if (!driver.Run())
        return EX_SOFTWARE;

    return 0;
}
