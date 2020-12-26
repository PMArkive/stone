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
#include <proto/poll.pb.h>
#include <proto/state.pb.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ini-reader.h"

namespace stone {

class Context;

typedef std::pair<double, double> RaceResult;
typedef std::unordered_map<int32_t, RaceResult> RaceResultMap;

typedef google::protobuf::Map<google::protobuf::int32, HouseRating> HouseRatingMap;
typedef google::protobuf::Map<google::protobuf::int32, PollList> ProtoPollMap;

class Campaign
{
  public:
    bool Init(Context* cx, int year);

    std::optional<Feed> Fetch(Context* cx);

    Date StartDate() const {
        return start_date_;
    }
    Date EndDate() const {
        return end_date_;
    }
    const std::unordered_map<std::string, std::pair<double, double>>& AssumedMargins() const {
        return assumed_margins_;
    }
    const std::map<std::string, State>& States() const {
        return states_;
    }
    int TotalEv() const {
        return total_evs_;
    }
    bool IsPresidentialYear() const {
        return is_presidential_year_;
    }
    double UndecidedPercent() const {
        return undecided_percent_;
    }
    const std::vector<State>& state_list() const {
        return state_list_;
    }
    const SenateMap& senate_map() const {
        return senate_map_;
    }
    const GovernorMap& governor_map() const {
        return governor_map_;
    }
    const HouseMap& house_map() const {
        return house_map_;
    }
    bool IsPollBanned(const std::string& poll_id) {
        return banned_polls_.count(poll_id) > 0;
    }
    const std::string& dem_pres() const { return dem_pres_; }
    const std::string& gop_pres() const { return gop_pres_; }

    std::unordered_map<std::string, int>& district_to_house_race() {
        return district_to_house_race_;
    }
    const std::vector<ImportantDate> important_dates() const {
        return important_dates_;
    }
    const HouseRatingHistory& house_history() const {
        return house_history_;
    }
    const std::unordered_map<Race_RaceType, RaceResultMap>& race_results() const {
        return race_results_;
    }
    const std::unordered_map<Race_RaceType, RaceResult>& national_race_results() const {
        return national_race_results_;
    }
    const std::string& election_type() const { return election_type_; }

  private:
    bool InitMain(const IniFile& file, std::string_view file_name);
    bool InitStateMap(const std::string& name);
    bool InitAssumedMargins(const IniFile& file, std::string_view file_name);
    bool InitHouse(const IniFile& file, std::string_view file_name);
    bool InitSenate(const IniFile& file, std::string_view file_name);
    bool InitGovernor(const IniFile& file, std::string_view file_name);
    bool InitRaceList(const std::string& file, Race_RaceType type,
                      ::google::protobuf::RepeatedPtrField<Race>* races, MapEv* seats_up);
    bool InitImportantDates(const IniFile& file, std::string_view file_name);
    void InitBannedPolls(const IniFile& file);
    bool InitElectionResults(std::string_view file_name);

  protected:
    Date start_date_;
    Date end_date_;
    int total_evs_ = 0;
    bool is_presidential_year_ = false;
    double undecided_percent_ = 0.0f;
    std::string default_feed_;
    std::string governor_feed_;
    std::string house_ratings_feed_;
    std::string generic_ballot_feed_;
    std::string dem_pres_;
    std::string gop_pres_;

    std::unordered_map<std::string, std::pair<double, double>> assumed_margins_;
    std::map<std::string, State> states_;
    std::vector<State> state_list_;
    std::unordered_set<std::string> banned_polls_;
    SenateMap senate_map_;
    GovernorMap governor_map_;
    HouseMap house_map_;
    std::unordered_map<std::string, int> district_to_house_race_;
    std::vector<ImportantDate> important_dates_;
    HouseRatingHistory house_history_;
    std::string election_type_;

    std::unordered_map<Race_RaceType, RaceResultMap> race_results_;
    std::unordered_map<Race_RaceType, RaceResult> national_race_results_;
};

extern const std::unordered_map<std::string, std::string> kStateCodes;

} // namespace stone
