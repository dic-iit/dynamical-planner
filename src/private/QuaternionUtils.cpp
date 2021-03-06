/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <DynamicalPlannerPrivate/Utilities/QuaternionUtils.h>
#include <iDynTree/Core/EigenHelpers.h>

iDynTree::MatrixFixSize<4, 3> DynamicalPlanner::Private::QuaternionLeftTrivializedDerivative(const iDynTree::Vector4 &quaternion)
{
    iDynTree::MatrixFixSize<4, 3> outputMatrix;
    Eigen::Map<Eigen::Matrix<double, 4, 3, Eigen::RowMajor> > map = iDynTree::toEigen(outputMatrix);
    map.topRows<1>() = -iDynTree::toEigen(quaternion).tail<3>().transpose();
    map.bottomRows<3>().setIdentity();
    map.bottomRows<3>() *= quaternion(0);
    map.bottomRows<3>() += iDynTree::skew(iDynTree::toEigen(quaternion).tail<3>());
    map *= 0.5;
    return outputMatrix;
}

iDynTree::MatrixFixSize<3, 4> DynamicalPlanner::Private::QuaternionLeftTrivializedDerivativeInverse(const iDynTree::Vector4& quaternion)
{
    iDynTree::MatrixFixSize<3, 4> outputMatrix;
    Eigen::Map<Eigen::Matrix<double, 3, 4, Eigen::RowMajor> > map = iDynTree::toEigen(outputMatrix);

    map.setZero();
    map.leftCols<1>() = -iDynTree::toEigen(quaternion).tail<3>();
    map.rightCols<3>().setIdentity();
    map.rightCols<3>() *= iDynTree::toEigen(quaternion)(0);
    map.rightCols<3>() -= iDynTree::skew(iDynTree::toEigen(quaternion).tail<3>());

    map *= 2;
    return outputMatrix;
}

iDynTree::Vector4 DynamicalPlanner::Private::NormalizedQuaternion(const iDynTree::Vector4& quaternion)
{
    iDynTree::Vector4 normalized;
    iDynTree::toEigen(normalized) = iDynTree::toEigen(quaternion).normalized();
    return normalized;
}

double DynamicalPlanner::Private::QuaternionNorm(const iDynTree::Vector4 &quaternion)
{
    return iDynTree::toEigen(quaternion).norm();
}

double DynamicalPlanner::Private::QuaternionSquaredNorm(const iDynTree::Vector4& quaternion)
{
    return iDynTree::toEigen(quaternion).squaredNorm();
}

iDynTree::Matrix4x4 DynamicalPlanner::Private::NormalizedQuaternionDerivative(const iDynTree::Vector4 &quaternion)
{
    iDynTree::Matrix4x4 derivative;
    Eigen::Map<Eigen::Matrix<double, 4, 4, Eigen::RowMajor> > derivativeMap = iDynTree::toEigen(derivative);

    double powerThreeNorm = QuaternionNorm(quaternion) * QuaternionSquaredNorm(quaternion);
    Eigen::Matrix<double, 4, 4, Eigen::RowMajor> outerProduct = iDynTree::toEigen(quaternion) * iDynTree::toEigen(quaternion).transpose();
    Eigen::Matrix<double, 4, 4, Eigen::RowMajor> identity;
    identity.setIdentity();
    derivativeMap = identity * QuaternionSquaredNorm(quaternion) - outerProduct;
    derivativeMap *= 1.0/powerThreeNorm;
    return derivative;
}

iDynTree::MatrixFixSize<4, 4> DynamicalPlanner::Private::QuaternionLeftTrivializedDerivativeTimesOmegaJacobian(const iDynTree::Vector3 &omega)
{
    iDynTree::Matrix4x4 jacobian;
    Eigen::Map<Eigen::Matrix<double, 4, 4, Eigen::RowMajor> > jacobianMap = iDynTree::toEigen(jacobian);
    Eigen::Map<const Eigen::Vector3d> omegaMap = iDynTree::toEigen(omega);

    jacobianMap(0,0) = 0;
    jacobianMap.topRightCorner<1, 3>() = -omegaMap.transpose();
    jacobianMap.bottomLeftCorner<3,1>() = omegaMap;
    jacobianMap.bottomRightCorner<3,3>() = -iDynTree::skew(omegaMap);
    jacobianMap *= 0.5;

    return jacobian;

}

iDynTree::MatrixFixSize<3, 4> DynamicalPlanner::Private::RotatedVectorQuaternionJacobian(const iDynTree::Vector3 &originalVector, const iDynTree::Vector4 &quaternion)
{
    iDynTree::MatrixFixSize<3, 4> jacobian;
    Eigen::Map<Eigen::Matrix<double, 3, 4, Eigen::RowMajor> > jacobianMap = iDynTree::toEigen(jacobian);
    double vectorNorm = iDynTree::toEigen(originalVector).norm();
    Eigen::Vector3d vectorNormalized;
    if (vectorNorm > 1){
     vectorNormalized = iDynTree::toEigen(originalVector).normalized();
    } else {
        vectorNorm = 1;
        vectorNormalized = iDynTree::toEigen(originalVector);
    }

    Eigen::Map<const Eigen::Vector4d> quaternionMap = iDynTree::toEigen(quaternion);

    Eigen::Vector3d rCrossX = quaternionMap.bottomRows<3>().cross(vectorNormalized);
    Eigen::Matrix<double, 3, 3, Eigen::RowMajor> rCrossXskew = iDynTree::skew(rCrossX);
    Eigen::Matrix<double, 3, 3, Eigen::RowMajor> xSkew = iDynTree::skew(vectorNormalized);
    Eigen::Matrix<double, 3, 3, Eigen::RowMajor> rSkew = iDynTree::skew(quaternionMap.bottomRows<3>());

    jacobianMap.leftCols<1>() = rCrossX;
    jacobianMap.rightCols<3>() = -quaternionMap(0) * xSkew - rCrossXskew - rSkew * xSkew;

    jacobianMap *= 2 * vectorNorm;

    return jacobian;
}

bool DynamicalPlanner::Private::QuaternionBoundsRespected(const iDynTree::Vector4 &quaternion)
{
    bool ok = true;
    ok = ok && quaternion(0) >= -1.0;
    ok = ok && quaternion(0) <= 1.0 + 1E-6;
    for (unsigned int i = 1; i < 4; ++i) {
        ok = ok && quaternion(i) >= -1.0 - 1E-6;
        ok = ok && quaternion(i) <= 1.0 + 1E-6;
    }

    if (!ok) {
        std::cerr << "[DynamicalPlanner::Private::QuaternionBoundsRespected] Quaternion: " << std::endl << quaternion.toString() << std::endl;
    }

    return ok;
}

iDynTree::Vector4 DynamicalPlanner::Private::ErrorQuaternion(const iDynTree::Rotation &frameRotation, const iDynTree::Rotation &desiredRotation)
{
    iDynTree::Rotation rotationError = desiredRotation.inverse() * frameRotation;
    return rotationError.asQuaternion();
}


iDynTree::Vector4 DynamicalPlanner::Private::InverseQuaternion(const iDynTree::Vector4 &quaternion)
{
    iDynTree::Vector4 inverseQuaternion;
    inverseQuaternion(0) = quaternion(0);
    iDynTree::toEigen(inverseQuaternion).bottomRows<3>() = - iDynTree::toEigen(quaternion).bottomRows<3>();
    return inverseQuaternion;
}

iDynTree::Matrix4x4 DynamicalPlanner::Private::InverseQuaternionDerivative()
{
    iDynTree::Matrix4x4 inverseMap;
    iDynTree::toEigen(inverseMap) << 1.0,  0.0,  0.0,  0.0,
                                     0.0, -1.0,  0.0,  0.0,
                                     0.0,  0.0, -1.0,  0.0,
                                     0.0,  0.0,  0.0, -1.0;
    return inverseMap;
}

iDynTree::MatrixFixSize<3, 4> DynamicalPlanner::Private::QuaternionLeftTrivializedDerivativeInverseTimesQuaternionDerivativeJacobian(
        const iDynTree::Vector4 &quatDerivative)
{
    iDynTree::MatrixFixSize<3, 4> jacobian;

    iDynTree::toEigen(jacobian).leftCols<1>() = iDynTree::toEigen(quatDerivative).bottomRows<3>();
    iDynTree::toEigen(jacobian).rightCols<3>().setIdentity();
    iDynTree::toEigen(jacobian).rightCols<3>() *= -quatDerivative(0);
    iDynTree::toEigen(jacobian).rightCols<3>() += iDynTree::skew(iDynTree::toEigen(quatDerivative).bottomRows<3>());

    iDynTree::toEigen(jacobian) *= 2;

    return jacobian;
}
