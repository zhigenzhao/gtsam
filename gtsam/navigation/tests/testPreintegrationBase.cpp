/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    testPreintegrationBase.cpp
 * @brief   Unit test for the InertialNavFactor
 * @author  Frank Dellaert
 */

#include <gtsam/navigation/PreintegrationBase.h>
#include <gtsam/base/numericalDerivative.h>

#include <CppUnitLite/TestHarness.h>
#include <boost/bind.hpp>

using namespace std;
using namespace gtsam;

static const double kDt = 0.1;

static const double kGyroSigma = 0.02;
static const double kAccelSigma = 0.1;

// Create default parameters with Z-down and above noise parameters
static boost::shared_ptr<PreintegrationBase::Params> defaultParams() {
  auto p = PreintegrationParams::MakeSharedD(10);
  p->gyroscopeCovariance = kGyroSigma * kGyroSigma * I_3x3;
  p->accelerometerCovariance = kAccelSigma * kAccelSigma * I_3x3;
  p->integrationCovariance = 0.0000001 * I_3x3;
  return p;
}

Vector9 f(const Vector9& zeta, const Vector3& a, const Vector3& w) {
  PreintegrationBase::TangentVector zeta_plus =
      PreintegrationBase::UpdateEstimate(a, w, kDt, zeta);
  return zeta_plus.vector();
}

/* ************************************************************************* */
TEST(PreintegrationBase, UpdateEstimate1) {
  PreintegrationBase pim(defaultParams());
  const Vector3 acc(0.1, 0.2, 10), omega(0.1, 0.2, 0.3);
  Vector9 zeta;
  zeta.setZero();
  Matrix9 aH1;
  Matrix93 aH2, aH3;
  pim.UpdateEstimate(acc, omega, kDt, zeta, aH1, aH2, aH3);
  EXPECT(assert_equal(numericalDerivative31(f, zeta, acc, omega), aH1, 1e-9));
  EXPECT(assert_equal(numericalDerivative32(f, zeta, acc, omega), aH2, 1e-9));
  EXPECT(assert_equal(numericalDerivative33(f, zeta, acc, omega), aH3, 1e-9));
}

/* ************************************************************************* */
TEST(PreintegrationBase, UpdateEstimate2) {
  PreintegrationBase pim(defaultParams());
  const Vector3 acc(0.1, 0.2, 10), omega(0.1, 0.2, 0.3);
  Vector9 zeta;
  zeta << 0.01, 0.02, 0.03, 100, 200, 300, 10, 5, 3;
  Matrix9 aH1;
  Matrix93 aH2, aH3;
  pim.UpdateEstimate(acc, omega, kDt, zeta, aH1, aH2, aH3);
  // NOTE(frank): tolerance of 1e-3 on H1 because approximate away from 0
  EXPECT(assert_equal(numericalDerivative31(f, zeta, acc, omega), aH1, 1e-3));
  EXPECT(assert_equal(numericalDerivative32(f, zeta, acc, omega), aH2, 1e-7));
  EXPECT(assert_equal(numericalDerivative33(f, zeta, acc, omega), aH3, 1e-9));
}

/* ************************************************************************* */
TEST(PreintegrationBase, computeError) {
  PreintegrationBase pim(defaultParams());
  NavState x1, x2;
  imuBias::ConstantBias bias;
  Matrix9 aH1, aH2;
  Matrix96 aH3;
  pim.computeError(x1, x2, bias, aH1, aH2, aH3);
  boost::function<Vector9(const NavState&, const NavState&,
                          const imuBias::ConstantBias&)> f =
      boost::bind(&PreintegrationBase::computeError, pim, _1, _2, _3,
                  boost::none, boost::none, boost::none);
  // NOTE(frank): tolerance of 1e-3 on H1 because approximate away from 0
  EXPECT(assert_equal(numericalDerivative31(f, x1, x2, bias), aH1, 1e-9));
  EXPECT(assert_equal(numericalDerivative32(f, x1, x2, bias), aH2, 1e-9));
  EXPECT(assert_equal(numericalDerivative33(f, x1, x2, bias), aH3, 1e-9));
}

/* ************************************************************************* */
int main() {
  TestResult tr;
  return TestRegistry::runAllTests(tr);
}
/* ************************************************************************* */
