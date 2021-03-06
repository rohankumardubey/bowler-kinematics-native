/*
 * This file is part of bowler-kinematics-native.
 *
 * bowler-kinematics-native is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * bowler-kinematics-native is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with bowler-kinematics-native.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "cppoptlib/meta.h"
#include "cppoptlib/problem.h"
#include "cppoptlib/solver/lbfgsbsolver.h"
#include <cmath>
#include <jni.h>

template <typename Scalar> class DhParam {
  public:
  DhParam(Scalar d, Scalar theta, Scalar r, Scalar alpha) : d(d), theta(theta), r(r), alpha(alpha) {
    ft.coeffRef(2, 0) = 0;
    ft.coeffRef(2, 3) = d;
    ft.coeffRef(3, 0) = 0;
    ft.coeffRef(3, 1) = 0;
    ft.coeffRef(3, 2) = 0;
    ft.coeffRef(3, 3) = 1;
  }

  void computeFT(const Scalar jointAngle) {
    const Scalar ct = std::cos(theta + jointAngle);
    const Scalar st = std::sin(theta + jointAngle);
    const Scalar ca = std::cos(alpha);
    const Scalar sa = std::sin(alpha);
    ft.coeffRef(0, 0) = ct;
    ft.coeffRef(0, 1) = -st * ca;
    ft.coeffRef(0, 2) = st * sa;
    ft.coeffRef(0, 3) = r * ct;
    ft.coeffRef(1, 0) = st;
    ft.coeffRef(1, 1) = ct * ca;
    ft.coeffRef(1, 2) = -ct * sa;
    ft.coeffRef(1, 3) = r * st;
    ft.coeffRef(2, 1) = sa;
    ft.coeffRef(2, 2) = ca;
  }

  const Scalar d;
  const Scalar theta;
  const Scalar r;
  const Scalar alpha;
  Eigen::Matrix<Scalar, 4, 4> ft;
};

template <typename T> class IkProblem : public cppoptlib::BoundedProblem<T> {
  public:
  using typename cppoptlib::Problem<T>::Scalar;
  using typename cppoptlib::Problem<T>::TVector;
  using FT = Eigen::Matrix<Scalar, 4, 4>;

  IkProblem(std::vector<DhParam<T>> dhParams, const FT target, const TVector &l, const TVector &u)
    : cppoptlib::BoundedProblem<T>(l, u), target(std::move(target)), dhParams(std::move(dhParams)) {
  }

  T value(const TVector &jointAngles) override {
    FT tip = FT::Identity();
    for (size_t i = 0; i < dhParams.size(); ++i) {
      dhParams[i].computeFT(jointAngles[i]);
      tip *= dhParams[i].ft;
    }

    const T targetX = target.coeff(0, 3);
    const T targetY = target.coeff(1, 3);
    const T targetZ = target.coeff(2, 3);

    const T tipX = tip.coeff(0, 3);
    const T tipY = tip.coeff(1, 3);
    const T tipZ = tip.coeff(2, 3);

    return std::sqrt(std::pow(tipX - targetX, 2) + std::pow(tipY - targetY, 2) +
                     std::pow(tipZ - targetZ, 2));
  }

  void gradient(const TVector &x, TVector &grad) override {
    // TODO: Compute jacobian
    cppoptlib::Problem<T>::finiteGradient(x, grad, 3);
  }

  const FT target;
  std::vector<DhParam<T>> dhParams;
};

using IkProblemf = IkProblem<float>;
using IkProblemd = IkProblem<double>;

extern "C" {
JNIEXPORT jdoubleArray JNICALL
Java_com_neuronrobotics_bowlerkinematicsnative_solver_NativeIKSolver_solve(
  JNIEnv *env,
  jobject,
  jint numberOfLinks,
  jdoubleArray dhParamsJArray,
  jdoubleArray upperJointLimitsJArray,
  jdoubleArray lowerJointLimitsJArray,
  jdoubleArray initialJointAnglesJArray,
  jdoubleArray targetJArray) {
  jdouble *dhParamsData = env->GetDoubleArrayElements(dhParamsJArray, nullptr);
  std::vector<DhParam<double>> dhParams;
  dhParams.reserve(numberOfLinks);
  for (int i = 0; i < numberOfLinks; ++i) {
    dhParams.emplace_back(dhParamsData[i * 4 + 0],
                          dhParamsData[i * 4 + 1],
                          dhParamsData[i * 4 + 2],
                          dhParamsData[i * 4 + 3]);
  }
  env->ReleaseDoubleArrayElements(dhParamsJArray, dhParamsData, 0);

  jdouble *upperJointLimitsData = env->GetDoubleArrayElements(upperJointLimitsJArray, nullptr);
  Eigen::VectorXd upperJointLimits(numberOfLinks);
  for (int i = 0; i < numberOfLinks; ++i) {
    upperJointLimits.coeffRef(i) = upperJointLimitsData[i];
  }
  env->ReleaseDoubleArrayElements(upperJointLimitsJArray, upperJointLimitsData, 0);

  jdouble *lowerJointLimitsData = env->GetDoubleArrayElements(lowerJointLimitsJArray, nullptr);
  Eigen::VectorXd lowerJointLimits(numberOfLinks);
  for (int i = 0; i < numberOfLinks; ++i) {
    lowerJointLimits.coeffRef(i) = lowerJointLimitsData[i];
  }
  env->ReleaseDoubleArrayElements(lowerJointLimitsJArray, lowerJointLimitsData, 0);

  jdouble *initialJointAnglesData = env->GetDoubleArrayElements(initialJointAnglesJArray, nullptr);
  Eigen::VectorXd initialJointAngles(numberOfLinks);
  for (int i = 0; i < numberOfLinks; ++i) {
    initialJointAngles.coeffRef(i) = initialJointAnglesData[i];
  }
  env->ReleaseDoubleArrayElements(initialJointAnglesJArray, initialJointAnglesData, 0);

  jdouble *targetData = env->GetDoubleArrayElements(targetJArray, nullptr);
  Eigen::Matrix4d target;
  for (int i = 0; i < 16; ++i) {
    const int row = i / 4;
    const int col = i % 4;
    target.coeffRef(row, col) = targetData[i];
  }
  env->ReleaseDoubleArrayElements(targetJArray, targetData, 0);

  IkProblemd f(std::move(dhParams), std::move(target), lowerJointLimits, upperJointLimits);

  cppoptlib::LbfgsbSolver<IkProblemd> solver;
  solver.minimize(f, initialJointAngles);
  jdoubleArray result = env->NewDoubleArray(numberOfLinks);
  env->SetDoubleArrayRegion(result, 0, numberOfLinks, initialJointAngles.data());
  return result;
}
}
