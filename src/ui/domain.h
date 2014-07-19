/**
 * This file is part of the "FnordMetric" project
 *   Copyright (c) 2014 Paul Asmuth, Google Inc.
 *
 * Licensed under the MIT license (see LICENSE).
 */
#ifndef _FNORDMETRIC_DOMAIN_H
#define _FNORDMETRIC_DOMAIN_H
#include <stdlib.h>
#include <assert.h>
#include "../util/format.h"

namespace fnordmetric {
namespace ui {

class Domain {
public:
  static const int kNumTicks = 6; // FIXPAUL!!

  virtual ~Domain() {}

  /**
   * Returns the label at the specified tick/index
   *
   * @param index the index/tick
   */
  virtual std::string labelAt(double index) const = 0;

  /**
   * Returns the "ticks" of this domain
   */
  virtual std::vector<double> getTicks() const = 0;

};

class NumericalDomain : public Domain{
public:

  /**
   * Create a new numerical domain with explicit parameters
   *
   * @param min_value the smallest value
   * @param max_value the largest value
   * @param logarithmic is this domain a logarithmic domain?
   */
  NumericalDomain(
    double min_value,
    double max_value,
    bool is_logarithmic = false) :
    min_value_(min_value),
    max_value_(max_value),
    is_logarithmic_(is_logarithmic) {}

  double scale(double value) const {
    if (value <= min_value_) {
      return 0.0f;
    }

    if (value >= max_value_) {
      return 1.0f;
    }

    return (value - min_value_) / (max_value_ - min_value_);
  }

  double valueAt(double index) const {
    return min_value_ + (max_value_ - min_value_) * index;
  }

  std::string labelAt(double index) const override {
    return util::format::numberToHuman(valueAt(index));
  }

  std::vector<double> getTicks() const override {
    std::vector<double> ticks;

    for (int i=0; i < kNumTicks; i++) {
      auto tick = (double) i / (kNumTicks - 1);
      ticks.push_back(tick);
    }

    return ticks;
  }



protected:
  double min_value_;
  double max_value_;
  bool is_logarithmic_;
};

}
}
#endif