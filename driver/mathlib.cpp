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

#include <assert.h>

#include <algorithm>
#include <mutex>
#include <numeric>
#include <unordered_set>

#include <erfinv.h>
#include "mathlib.h"

namespace stone {

double
Average(const std::vector<double>& values)
{
    assert(!values.empty());
    return std::accumulate(values.begin(), values.end(), 0.0) / double(values.size());
}

double
Median(const std::vector<double>& values_in)
{
    auto values = values_in;
    std::sort(values.begin(), values.end());

    if (values.size() % 2 == 1)
        return values[values.size() / 2];

    return (values[values.size() / 2 - 1] +
            values[values.size() / 2]) / 2.0;
}

double
StandardDeviation(const std::vector<double>& values)
{
    double mean = Average(values);
    double sigma = 0.0;
    for (const auto& val : values) {
        double x = val - mean;
        sigma += x * x;
    }
    return sqrt(sigma / double(values.size()));
}

double
SampleStdDev(const std::vector<double>& values)
{
    if (values.size() == 1)
        return 0.0;

    double mean = Average(values);
    double sigma = 0.0;
    for (const auto& val : values) {
        double x = val - mean;
        sigma += x * x;
    }
    return sqrt(sigma / double(values.size() - 1));
}

double
MeanAbsDeviation(const std::vector<double>& values)
{
    std::vector<double> work;

    double mean = Average(values);
    for (const auto& val : values)
        work.push_back(fabs(val - mean));
    std::sort(work.begin(), work.end());

    return Median(work) / 0.6745 / sqrt(values.size());
}

static double kSqrt2 = sqrt(2.0);

double
NormalCdf(double z_score)
{
    return (1.0 + erf(z_score / kSqrt2)) / 2.0;
}

double
NormalCdf(double x, double mean, double stddev)
{
    return (1.0 + erf((x - mean) / (stddev * kSqrt2))) / 2.0;
}

double
InverseCdf(double x, double p, double stddev)
{
    double inverse = -erfinv(2.0 * p - 1.0);
    return inverse * stddev * kSqrt2 + x;
}

std::vector<double>
Cumsum(const std::vector<double>& values)
{
    if (values.empty())
        return {};

    double a = 0.0;
    std::vector<double> r;
    for (const auto& val : values) {
        a += val;
        r.emplace_back(a);
    }
    return r;
}

double
WeightedAverage(const std::vector<double>& weights)
{
    double average = 0.0;
    for (size_t i = 0; i < weights.size(); i++)
        average += double(i) * weights[i];
    return average;
}

double
WeightedAverage(const std::vector<double>& values, const std::vector<double>& weights)
{
    assert(values.size() == weights.size());

    double average = 0.0f;
    double weight = 0.0f;
    for (size_t i = 0; i < values.size(); i++) {
        average += values[i] * weights[i];
        weight += weights[i];
    }
    return average / weight;
}

double
WeightedStdDev(const std::vector<double>& weights, int mean)
{
    double stddev = 0.0;
    double weight = 0.0;
    int non_zero_weights = 0;
    for (size_t i = 0; i < weights.size(); i++) {
        stddev += weights[i] * (double(i) - double(mean)) * (double(i) - double(mean));
        weight += weights[i];
        if (weights[i] != 0.0)
            non_zero_weights++;
    }
    double denom = ((non_zero_weights - 1) * weight) / non_zero_weights;
    return sqrt(stddev / denom);
}

int
RoundToNearest(double d)
{
    double rounded = std::round(d);
    int truncated = (int)rounded;
    return truncated;
}

static double
GetTpdfCoeff(int df)
{
    static const double kPi = 4.0 * atan(1.0);
    return tgamma((df + 1.0) / 2.0) / tgamma(df / 2.0) / sqrt(df * kPi);
}

double
Tpdf(double x, int df)
{
    double coeff = GetTpdfCoeff(df);
    return coeff * pow(1.0 + (x * x) / double(df), -((df + 1.0) / 2.0));
}

double
Sum(const std::vector<double>& values)
{
    double total = 0.0f;
    for (const auto& v : values)
        total += v;
    return total;
}

} // namespace stone
