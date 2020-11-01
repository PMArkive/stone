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

#include <mutex>
#include <string>

#include <inja/inja.hpp>
#include <proto/history.pb.h>
#include "context.h"
#include "utility.h"

namespace stone {

class Renderer
{
  public:
    Renderer(Context* cx, const CampaignData& data);

    bool Generate();

    std::string Render(const std::string& tpl, const nlohmann::json& obj);
    void RenderTo(const std::string& tpl, const nlohmann::json& obj, const std::string& path);
    void Save(const std::string& file, const std::string& text);

    typedef std::pair<std::string, std::string> GraphCommand;
    void AddGraphCommands(nlohmann::json& obj, const std::vector<GraphCommand>& commands,
                          const Date& date);

    int total_evs() const { return total_evs_; }
    bool backdating() const { return backdating_; }
    const CampaignData& campaign_data() { return data_; }

    std::string OutputPath(const std::string& path);

  private:
    bool OutputExists(const std::string& path);
    bool CalcLatestUpdate(FileTime* time);
    bool CopyNonTemplateFiles();
    bool GenerateGraphs();

  private:
    Context* cx_;
    const CampaignData& data_;

    std::string dir_;
    std::string out_;
    inja::Environment env_;

    std::unordered_map<std::string, State> state_map_;
    int total_evs_ = 0;
    bool backdating_ = false;

    std::mutex lock_;
    std::unordered_map<std::string, std::string> doc_cache_;
    std::vector<std::vector<std::string>> graph_commands_;
};

class HtmlGenerator
{
  public:
    HtmlGenerator(Renderer* renderer, const ModelData& data, const ModelData* prev_data);

    bool RenderMain(const std::string& path);
    void RenderWrongometer();

  private:
    typedef ::google::protobuf::RepeatedPtrField<Poll> RepeatedPoll;

    static constexpr double kSafeMargin = 5.0;

    void AddWinner(nlohmann::json& obj, const std::string& prefix, double value,
                   bool is_precise = false);
    void AddWinnerRating(nlohmann::json& obj, const std::string& prefix, const RaceModel& model,
                         double safe_zone = kSafeMargin);
    void AddPollWinner(nlohmann::json& obj, const std::string& prefix, const RaceModel& model);
    bool AddMapEv(const std::string& prefix, const MapEv& evs, bool no_ties,
                  const std::string& dem = {}, const std::string& gop = {});
    void AddNav();
    bool AddPollData(nlohmann::json& obj, const RepeatedPoll& polls,
                     const RepeatedPoll* prev_polls);
    bool BuildPollRows(const std::vector<const Poll*>& polls, const std::string& icon,
                       std::vector<nlohmann::json>* out);

    void RenderMap(const std::string& title, bool no_ties, const std::string& path, MapEv* evs);
    bool RenderStates();
    bool RenderSenate();
    bool RenderGovernor();
    bool RenderHouse();
    bool RenderNational();
    void RenderSeatChange(const std::string& prefix, int change);
    void RenderDelta(nlohmann::json& obj, double prev_margin, double new_margin);
    std::string RenderWinner(double win_p);
    bool IsLatestPrediction();

  private:
    Renderer* renderer_;
    const CampaignData& campaign_;
    const ModelData& data_;
    const ModelData* prev_data_;
    nlohmann::json main_;
    bool is_prediction_;
    bool is_wrongometer_ = false;
};

} // namespace stone
