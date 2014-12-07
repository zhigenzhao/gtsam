/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation, 
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 *  @file  ImuFactor.cpp
 *  @author Luca Carlone
 *  @author Stephen Williams
 *  @author Richard Roberts
 *  @author Vadim Indelman
 *  @author David Jensen
 *  @author Frank Dellaert
 **/

#include <gtsam/navigation/ImuFactor.h>

/* External or standard includes */
#include <ostream>

namespace gtsam {

using namespace std;

//------------------------------------------------------------------------------
// Inner class PreintegratedMeasurements
//------------------------------------------------------------------------------
ImuFactor::PreintegratedMeasurements::PreintegratedMeasurements(
    const imuBias::ConstantBias& bias, const Matrix3& measuredAccCovariance,
    const Matrix3& measuredOmegaCovariance, const Matrix3& integrationErrorCovariance,
    const bool use2ndOrderIntegration) :
        PreintegrationBase(bias, use2ndOrderIntegration)
{
  measurementCovariance_.setZero();
  measurementCovariance_.block<3,3>(0,0) = integrationErrorCovariance;
  measurementCovariance_.block<3,3>(3,3) = measuredAccCovariance;
  measurementCovariance_.block<3,3>(6,6) = measuredOmegaCovariance;
  preintMeasCov_.setZero();
}

//------------------------------------------------------------------------------
void ImuFactor::PreintegratedMeasurements::print(const string& s) const {
  PreintegrationBase::print(s);
  cout << "  measurementCovariance = \n [ " << measurementCovariance_ << " ]" << endl;
  cout << "  preintMeasCov = \n [ " << preintMeasCov_ << " ]" << endl;
}

//------------------------------------------------------------------------------
bool ImuFactor::PreintegratedMeasurements::equals(const PreintegratedMeasurements& expected, double tol) const {
  return equal_with_abs_tol(measurementCovariance_, expected.measurementCovariance_, tol)
  && equal_with_abs_tol(preintMeasCov_, expected.preintMeasCov_, tol)
  && PreintegrationBase::equals(expected, tol);
}

//------------------------------------------------------------------------------
void ImuFactor::PreintegratedMeasurements::resetIntegration(){
  PreintegrationBase::resetIntegration();
  preintMeasCov_.setZero();
}

//------------------------------------------------------------------------------
void ImuFactor::PreintegratedMeasurements::integrateMeasurement(
    const Vector3& measuredAcc, const Vector3& measuredOmega, double deltaT,
    boost::optional<const Pose3&> body_P_sensor,
    boost::optional<Matrix&> F_test, boost::optional<Matrix&> G_test) {

  // NOTE: order is important here because each update uses old values (i.e., we have to update
  // jacobians and covariances before updating preintegrated measurements).

  Vector3 correctedAcc, correctedOmega;
  correctMeasurementsByBiasAndSensorPose(measuredAcc, measuredOmega, correctedAcc, correctedOmega, body_P_sensor);

  const Vector3 theta_incr = correctedOmega * deltaT; // rotation vector describing rotation increment computed from the current rotation rate measurement
  const Rot3 Rincr = Rot3::Expmap(theta_incr); // rotation increment computed from the current rotation rate measurement
  const Matrix3 Jr_theta_incr = Rot3::rightJacobianExpMapSO3(theta_incr); // Right jacobian computed at theta_incr

  // Update Jacobians
  /* ----------------------------------------------------------------------------------------------------------------------- */
  updatePreintegratedJacobians(correctedAcc, Jr_theta_incr, Rincr, deltaT);

  // Update preintegrated measurements covariance
  // as in [2] we consider a first order propagation that can be seen as a prediction phase in an EKF framework
  /* ----------------------------------------------------------------------------------------------------------------------- */
  const Vector3 theta_i = thetaRij(); // super-expensive parametrization of so(3)
  const Matrix3 R_i = deltaRij();
  const Matrix3 Jr_theta_i = Rot3::rightJacobianExpMapSO3(theta_i);

  // Update preintegrated measurements
  updatePreintegratedMeasurements(correctedAcc, Rincr, deltaT);

  const Vector3 theta_j = thetaRij(); // super-expensive parametrization of so(3)
  const Matrix3 Jrinv_theta_j = Rot3::rightJacobianExpMapSO3inverse(theta_j);

  Matrix H_vel_angles = - R_i * skewSymmetric(correctedAcc) * Jr_theta_i * deltaT;
  Matrix H_angles_angles = Jrinv_theta_j * Rincr.inverse().matrix() * Jr_theta_i;

  // overall Jacobian wrt preintegrated measurements (df/dx)
  Matrix F(9,9);
  //   pos          vel              angle
  F << I_3x3,       I_3x3 * deltaT,  Z_3x3,          // pos
       Z_3x3,       I_3x3,           H_vel_angles,   // vel
       Z_3x3,       Z_3x3,           H_angles_angles;// angle

  // first order uncertainty propagation:
  // the deltaT allows to pass from continuous time noise to discrete time noise
  // measurementCovariance_discrete = measurementCovariance_contTime * (1/deltaT)
  // Gt * Qt * G =(approx)= measurementCovariance_discrete * deltaT^2 = measurementCovariance_contTime * deltaT
  preintMeasCov_ = F * preintMeasCov_ * F.transpose() + measurementCovariance_ * deltaT ;

  // F_test and Gout are used for testing purposes and are not needed by the factor
  if(F_test){
    // This in only for testing
    F_test->resize(9,9);
    (*F_test) << F;
  }
  if(G_test){
    // Extended version, without approximation: Gt * Qt * G =(approx)= measurementCovariance_contTime * deltaT
    // This in only for testing

    G_test->resize(9,9);
    //           intNoise         accNoise      omegaNoise
    (*G_test) << I_3x3 * deltaT,  Z_3x3,        Z_3x3,                                 // pos
                Z_3x3,            R_i * deltaT, Z_3x3,                                 // vel
                Z_3x3,            Z_3x3,        Jrinv_theta_j * Jr_theta_incr * deltaT;// angle
    //preintMeasCov = F * preintMeasCov * F.transpose() + G_test * (1/deltaT) * measurementCovariance * G_test.transpose();
  }
}

//------------------------------------------------------------------------------
// ImuFactor methods
//------------------------------------------------------------------------------
ImuFactor::ImuFactor() :
    ImuFactorBase(), _PIM_(imuBias::ConstantBias(), Z_3x3, Z_3x3, Z_3x3) {}

//------------------------------------------------------------------------------
ImuFactor::ImuFactor(
    Key pose_i, Key vel_i, Key pose_j, Key vel_j, Key bias,
    const PreintegratedMeasurements& preintegratedMeasurements,
    const Vector3& gravity, const Vector3& omegaCoriolis,
    boost::optional<const Pose3&> body_P_sensor,
    const bool use2ndOrderCoriolis) :
        Base(noiseModel::Gaussian::Covariance(preintegratedMeasurements.preintMeasCov_),
            pose_i, vel_i, pose_j, vel_j, bias),
        ImuFactorBase(gravity, omegaCoriolis, body_P_sensor, use2ndOrderCoriolis),
        _PIM_(preintegratedMeasurements) {}

//------------------------------------------------------------------------------
gtsam::NonlinearFactor::shared_ptr ImuFactor::clone() const {
  return boost::static_pointer_cast<gtsam::NonlinearFactor>(
      gtsam::NonlinearFactor::shared_ptr(new This(*this)));
}

//------------------------------------------------------------------------------
void ImuFactor::print(const string& s, const KeyFormatter& keyFormatter) const {
  cout << s << "ImuFactor("
      << keyFormatter(this->key1()) << ","
      << keyFormatter(this->key2()) << ","
      << keyFormatter(this->key3()) << ","
      << keyFormatter(this->key4()) << ","
      << keyFormatter(this->key5()) << ")\n";
  ImuFactorBase::print("");
  _PIM_.print("  preintegrated measurements:");
  this->noiseModel_->print("  noise model: ");
}

//------------------------------------------------------------------------------
bool ImuFactor::equals(const NonlinearFactor& expected, double tol) const {
  const This *e =  dynamic_cast<const This*> (&expected);
  return e != NULL && Base::equals(*e, tol)
  && _PIM_.equals(e->_PIM_, tol)
  && ImuFactorBase::equals(*e, tol);
}

//------------------------------------------------------------------------------
Vector ImuFactor::evaluateError(const Pose3& pose_i, const Vector3& vel_i,
    const Pose3& pose_j, const Vector3& vel_j,
    const imuBias::ConstantBias& bias_i, boost::optional<Matrix&> H1,
    boost::optional<Matrix&> H2, boost::optional<Matrix&> H3,
    boost::optional<Matrix&> H4, boost::optional<Matrix&> H5) const {

  return _PIM_.computeErrorAndJacobians(pose_i, vel_i, pose_j, vel_j, bias_i,
      gravity_, omegaCoriolis_, use2ndOrderCoriolis_, H1, H2, H3, H4, H5);
}

} /// namespace gtsam