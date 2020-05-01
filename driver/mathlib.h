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

#include <math.h>

#include <limits>
#include <vector>

#include <proto/model.pb.h>

namespace stone {

double Average(const std::vector<double>& values);
double Median(const std::vector<double>& values);
double SampleStdDev(const std::vector<double>& values);
double StandardDeviation(const std::vector<double>& values);
double MeanAbsDeviation(const std::vector<double>& values);
double NormalCdf(double x, double mean, double stddev);
double InverseCdf(double x, double p, double stddev);
std::vector<double> Cumsum(const std::vector<double>& values);
double WeightedAverage(const std::vector<double>& weights);
double WeightedAverage(const std::vector<double>& values, const std::vector<double>& weights);
double WeightedStdDev(const std::vector<double>& weights, int mean);
int RoundToNearest(double d);
double Tpdf(double value, int df);
double Tcdf(double value, int df);
double Sum(const std::vector<double>& values);

static inline void
Convolve(const std::vector<double>& x, const std::vector<double>& h, std::vector<double>* out)
{
    assert(!x.empty() && !h.empty());
    assert(x.size() < (size_t)std::numeric_limits<ssize_t>::max());
    assert(h.size() < (size_t)std::numeric_limits<ssize_t>::max());

    out->resize(x.size() + h.size() - 1);
    for (size_t i = 0; i < out->size(); i++) {
        double val = 0.0;
        ssize_t k = i;
        for (size_t j = 0; j < h.size() && k >= 0; j++, k--) {
            if (k < (ssize_t)x.size())
                val += x[k] * h[j];
        }
        out->at(i) = val;
    }
}

static inline int
GetTiebreakerMajority(int total)
{
    return (total / 2) + 1;
}

class Convolver
{
  public:
    Convolver(Convolver&& other) = default;
    Convolver(const std::vector<double>& win_p) {
        for (const auto& p : win_p)
            data_.emplace_back(1, p);
        Compute();
    }
    Convolver(std::vector<std::pair<int, double>>&& data)
      : data_(std::move(data))
    {
        Compute();
    }

    int FindMedian() {
        ComputeCumsum();

        for (int i = 0; i < (int)cumsum.size(); i++) {
            if (cumsum[i] >= 0.5)
                return i;
        }

        assert(false);
        return (int)cumsum.size();
    }

    int FindMode() {
        int mode = 0;
        for (int i = 1; i < (int)histogram.size(); i++) {
            if (histogram[i] > histogram[mode])
                mode = i;
        }
        return mode;
    }

    int FindMean() {
        if (mean_ == -1)
            mean_ = RoundToNearest(WeightedAverage(histogram));
        return mean_;
    }

    double DemWinProbForValue(int value) {
        if (value == 0)
            return 1.0;

        // Eg if our outcomes look like:
        //   Histogram    Cumsum
        //   [0] = 0.1    0.1
        //   [1] = 0.2    0.3
        //   [2] = 0.3    0.6
        //   [3] = 0.2    0.8
        //   [4] = 0.1    0.9
        //   [5] = 0.1    1.0
        //
        // If P(>=3) = 1 - P(2)

        ComputeCumsum();
        return 1.0 - cumsum[value - 1];
    }

    void CalcConfidence(EvRange* range, int base = 0) {
        int mean = FindMean();
        double stddev = WeightedStdDev(histogram, mean);

        static constexpr double kBand = 2;

        int dt = (int)round(stddev * kBand);
        assert(dt >= 0);
        assert(mean + dt <= (int)histogram.size() + 1); // +1 because of float rounding.

        range->set_low(mean - dt + base);
        range->set_high(std::clamp(mean + dt, 0, (int)histogram.size()) + base - 1);
    }

    void ComputeCumsum() {
        if (cumsum.size() != histogram.size())
            cumsum = Cumsum(histogram);
    }

    Convolver& operator =(Convolver&& other) = default;

    std::vector<double> histogram;
    std::vector<double> cumsum;

  private:
    std::vector<double> MakeSlice(size_t i) {
        std::vector<double> v(data_[i].first + 1, 0);
        v.front() = data_[i].second;
        v.back() = 1.0 - data_[i].second;
        return v;
    }

    void Compute() {
        histogram = MakeSlice(0);

        std::vector<double> temp;
        for (int i = 1; i < (int)data_.size(); i++) {
            Convolve(histogram, MakeSlice(i), &temp);
            std::swap(temp, histogram);
        }
        std::reverse(histogram.begin(), histogram.end());
    }

  private:
    int mean_ = -1;
    std::vector<std::pair<int, double>> data_;
};

} // namespace stone
