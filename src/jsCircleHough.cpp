/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "jsCircleHough.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef __linux__
// Linux specific build details
#else
// Windows specific build details
// use fast, less precise, floating point calculations
#pragma float_control(except, off)  // disable exception semantics
#pragma fenv_access(off)            // disable environment sensitivity
#pragma float_control(precise, off) // disable precise semantics
#pragma fp_contract(on)             // enable contractions
#endif

/**
 * @brief Class used to calculating a symetric triangle distribution.
 */
class SymTriangleDist {
 public:
  SymTriangleDist(double mu, double sigma) :
    m_mu(mu), m_sigma(sigma), m_one_over_sigma(1.0 / sigma)
    { }
  ~SymTriangleDist() = default;

  double pdf(double x)
  {
    double px = 0.0;

    if (fabs(x - m_mu) > m_sigma) {
      return 0.0;
    }

    px = 1 - fabs((x - m_mu) * m_one_over_sigma);
    px *= m_one_over_sigma;

    return px;
  }

 private:
  double m_mu;
  double m_sigma;
  double m_one_over_sigma;
};

/**
 * @brief Struct used for tracking contraints related to circle finding.
 */
struct Constraints {
  Constraints(int32_t lower, int32_t upper, uint32_t step_size) :
    lower(lower), upper(upper), step_size(step_size),
    steps((upper - lower) / step_size)
    { }

  int32_t lower;
  int32_t upper;
  uint32_t step_size;
  uint32_t steps;
};

class CircleHough {
 public:
  CircleHough(int32_t radius, jsCircleHoughConstraints *c) :
    m_cx(c->x_lower, c->x_upper, c->step_size),
    m_cy(c->y_lower, c->y_upper, c->step_size),
    m_dist(static_cast<double>(radius), static_cast<double>(c->step_size)),
    m_radius(radius)
  {
    assert(m_cx.lower < m_cx.upper);
    assert(m_cy.lower < m_cy.upper);
    assert(m_cx.step_size > 0);
    assert(m_cy.step_size > 0);
    assert(m_radius > 0);

    m_bx = linrange(m_cx.lower, m_cx.upper, m_cx.step_size, m_cx.steps);
    m_by = linrange(m_cy.lower, m_cy.upper, m_cy.step_size, m_cy.steps);
    m_bins = zeros(m_cy.steps, m_cx.steps);
    m_is_set = std::vector<bool>(m_cy.steps, false);
  }

  ~CircleHough() = default;

  jsCircleHoughResults map(jsProfile* profile)
  {
    jsCircleHoughResults results;
    results.weight = 0.0;
    results.x = 0;
    results.y = 0;
    // we know X and Y have same step size
    int32_t step_size = m_cx.step_size;
    int32_t radius = m_radius;

    double upper_lim = pow(radius + step_size, 2);
    double lower_lim = pow(radius - step_size, 2);

    for (uint32_t n = 0; n < m_bins.size(); n++) {
      std::fill(m_bins[n].begin(), m_bins[n].end(), 0.0);
    }

    for (uint32_t n = 0; n < profile->data_len; n++) {
      jsProfileData p = profile->data[n];

      int32_t x_start = find_index(p.x - radius - step_size, m_cx);
      int32_t x_end = find_index(p.x + radius + step_size, m_cx);
      int32_t y_start = find_index(p.y - radius - step_size, m_cy);
      int32_t y_end = find_index(p.y + step_size, m_cy);
      for (int y = y_start; y < y_end; y++) {
        for (int x = x_start; x < x_end; x++) {
          auto a = static_cast<double>(p.x - m_bx[x]);
          auto b = static_cast<double>(p.y - m_by[y]);
          auto r_sqr = (a * a) + (b * b);

          if (r_sqr < lower_lim || r_sqr > upper_lim) {
            continue;
          }

          auto r = sqrt(r_sqr);
          double px = m_dist.pdf(r);
          m_bins[y][x] += px;
          if (m_bins[y][x] > results.weight) {
            results.weight = m_bins[y][x];
            results.x = m_bx[x];
            results.y = m_by[y];
          }
        }
      }
    }

    return results;
  }

 private:
  std::vector<double> linrange(double start,
                               double stop,
                               double delta,
                               uint32_t bins)
  {
    std::vector<double> v;

    v.resize(bins);

    v[0] = start;
    for (unsigned int n = 1; n < v.size(); n++) {
      v[n] = v[n-1] + delta;
    }

    return v;
  }

  std::vector<std::vector<double>> zeros(int col, int row)
  {
    std::vector<std::vector<double>> v;

    v.resize(col);
    for (unsigned int m = 0; m < v.size(); m++) {
      v[m].resize(row, 0.0);
    }

    return v;
  }

  uint32_t find_index(int32_t point, Constraints &c)
  {
    int32_t i = ((point - c.lower) / static_cast<int32_t>(c.step_size));

    int32_t low = 0;
    int32_t high = c.steps - 1;

    int32_t r = (i < low) ? low : (high < i) ? high : i;
    return static_cast<uint32_t>(r);
  }

  Constraints m_cx;
  Constraints m_cy;
  SymTriangleDist m_dist;
  int32_t m_radius;
  std::vector<std::vector<double>> m_bins;
  std::vector<bool> m_is_set;
  std::vector<double> m_bx;
  std::vector<double> m_by;
};

jsCircleHough jsCircleHoughCreate(int32_t radius, jsCircleHoughConstraints *c)
{
  CircleHough *ch = nullptr;

  try {
    ch = new CircleHough(radius, c);
  } catch (std::exception &e) {
    (void)e;
    ch = nullptr;
  }

  return static_cast<jsCircleHough>(ch);
}

jsCircleHoughResults jsCircleHoughCalculate(jsCircleHough circle_hough,
                                            jsProfile *profile)
{
  CircleHough *ch = static_cast<CircleHough*>(circle_hough);

  auto results = ch->map(profile);

  return results;
}

void jsCircleHoughFree(jsCircleHough circle_hough)
{
  CircleHough *ch = static_cast<CircleHough*>(circle_hough);

  delete ch;
}
