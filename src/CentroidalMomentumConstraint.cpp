/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <DynamicalPlannerPrivate/CentroidalMomentumConstraint.h>
#include <DynamicalPlannerPrivate/CheckEqualVector.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <DynamicalPlannerPrivate/QuaternionUtils.h>
#include <cassert>

using namespace DynamicalPlanner::Private;

class CentroidalMomentumConstraint::Implementation {
public:
    VariablesLabeller stateVariables;
    VariablesLabeller controlVariables;

    iDynTree::IndexRange momentumRange, comPositionRange, basePositionRange, baseQuaternionRange, jointsPositionRange, baseVelocityRange, jointsVelocityRange;
    iDynTree::VectorDynSize constraintValueBuffer;
    iDynTree::Position basePosition, comPositionInverse;
    iDynTree::Vector3 comPosition;
    iDynTree::Vector4 baseQuaternion, baseQuaternionNormalized;
    iDynTree::Rotation baseRotation;
    iDynTree::Transform comTransform;
    iDynTree::Vector6 momentum;

    iDynTree::MatrixDynSize cmmMatrixInCoMBuffer, cmmMatrixInBaseBuffer, momentumDerivativeBuffer, stateJacobianBuffer, controlJacobianBuffer;
    iDynTree::MatrixFixSize<3, 4> notNormalizedQuaternionMap;

    RobotState robotState;
    std::shared_ptr<SharedKinDynComputation> sharedKinDyn;

    bool updateDoneOnceConstraint = false;
    bool updateDoneOnceStateJacobian = false;
    bool updateDoneOnceControlJacobian = false;
    double tolerance;

    void getRanges() {

        momentumRange = stateVariables.getIndexRange("Momentum");
        assert(momentumRange.isValid());

        comPositionRange = stateVariables.getIndexRange("CoMPosition");
        assert(comPositionRange.isValid());

        basePositionRange = stateVariables.getIndexRange("BasePosition");
        assert(basePositionRange.isValid());

        baseQuaternionRange = stateVariables.getIndexRange("BaseQuaternion");
        assert(baseQuaternionRange.isValid());

        jointsPositionRange = stateVariables.getIndexRange("JointsPosition");
        assert(jointsPositionRange.isValid());

        baseVelocityRange = controlVariables.getIndexRange("BaseVelocity");
        assert(baseVelocityRange.isValid());

        jointsVelocityRange = controlVariables.getIndexRange("JointsVelocity");
        assert(jointsVelocityRange.isValid());
    }

    void updateRobotState() {

        robotState = sharedKinDyn->currentState();

        iDynTree::toEigen(basePosition) = iDynTree::toEigen(stateVariables(basePositionRange));
        baseQuaternion = stateVariables(baseQuaternionRange);
        baseQuaternionNormalized = NormalizedQuaternion(baseQuaternion);
        assert(QuaternionBoundsRespected(baseQuaternionNormalized));
        baseRotation.fromQuaternion(baseQuaternionNormalized);

        robotState.world_T_base.setRotation(baseRotation);
        robotState.world_T_base.setPosition(basePosition);

        robotState.s = stateVariables(jointsPositionRange);

        robotState.s_dot = controlVariables(jointsVelocityRange);

        iDynTree::LinVelocity baseLinVelocity, baseAngVelocity;

        iDynTree::toEigen(baseLinVelocity) = iDynTree::toEigen(controlVariables(baseVelocityRange)).topRows<3>();
        iDynTree::toEigen(baseAngVelocity) = iDynTree::toEigen(controlVariables(baseVelocityRange)).bottomRows<3>();


        robotState.base_velocity = iDynTree::Twist(baseLinVelocity, baseAngVelocity);
    }

    void updateVariables (){
        updateRobotState();
        comPosition = stateVariables(comPositionRange);
        iDynTree::toEigen(comPositionInverse) = -1 * iDynTree::toEigen(comPosition);
        comTransform.setPosition(comPositionInverse);
        comTransform.setRotation(iDynTree::Rotation::Identity());
        momentum = stateVariables(momentumRange);
    }

    bool sameVariables(bool updateDoneOnce) {
        bool same = updateDoneOnce;
        same = same && VectorsAreEqual(momentum, stateVariables(momentumRange), tolerance);
        same = same && VectorsAreEqual(comPosition, stateVariables(comPositionRange), tolerance);
        same = same && VectorsAreEqual(basePosition, stateVariables(basePositionRange), tolerance);
        same = same && VectorsAreEqual(baseQuaternion, stateVariables(baseQuaternionRange), tolerance);
        same = same && VectorsAreEqual(robotState.s, stateVariables(jointsPositionRange), tolerance);
        same = same && VectorsAreEqual(robotState.base_velocity.asVector(), controlVariables(baseVelocityRange), tolerance);
        same = same && VectorsAreEqual(robotState.s_dot, controlVariables(jointsVelocityRange), tolerance);

        return same;
    }

};



CentroidalMomentumConstraint::CentroidalMomentumConstraint(const VariablesLabeller &stateVariables, const VariablesLabeller &controlVariables, std::shared_ptr<SharedKinDynComputation> sharedKinDyn)
    : iDynTree::optimalcontrol::Constraint (6, "CentroidalMomentum")
    , m_pimpl(new Implementation)
{
    assert(sharedKinDyn);
    assert(sharedKinDyn->isValid());
    m_pimpl->sharedKinDyn = sharedKinDyn;

    m_pimpl->stateVariables = stateVariables;
    m_pimpl->controlVariables = controlVariables;

    m_isLowerBounded = true;
    m_isUpperBounded = true;
    m_upperBound.zero();
    m_lowerBound.zero();

    m_pimpl->getRanges();

    m_pimpl->constraintValueBuffer.resize(6);
    m_pimpl->constraintValueBuffer.zero();
    m_pimpl->cmmMatrixInCoMBuffer.resize(6, 6 + static_cast<unsigned int>(m_pimpl->jointsPositionRange.size));
    m_pimpl->cmmMatrixInCoMBuffer.zero();
    m_pimpl->cmmMatrixInBaseBuffer.resize(6, 6 + static_cast<unsigned int>(m_pimpl->jointsPositionRange.size));
    m_pimpl->cmmMatrixInBaseBuffer.zero();
    m_pimpl->momentumDerivativeBuffer.resize(6, static_cast<unsigned int>(m_pimpl->jointsPositionRange.size));
    m_pimpl->stateJacobianBuffer.resize(6, static_cast<unsigned int>(stateVariables.size()));
    m_pimpl->stateJacobianBuffer.zero();
    m_pimpl->controlJacobianBuffer.resize(6, static_cast<unsigned int>(controlVariables.size()));
    m_pimpl->controlJacobianBuffer.zero();

    m_pimpl->robotState = sharedKinDyn->currentState();
    m_pimpl->tolerance = sharedKinDyn->getUpdateTolerance();

}

void CentroidalMomentumConstraint::setEqualityTolerance(double tolerance)
{
    assert(tolerance > 0);

    iDynTree::toEigen(m_lowerBound).setConstant(-tolerance/2.0);
    iDynTree::toEigen(m_upperBound).setConstant(tolerance/2.0);

}

CentroidalMomentumConstraint::~CentroidalMomentumConstraint()
{ }

bool CentroidalMomentumConstraint::evaluateConstraint(double /*time*/, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &control, iDynTree::VectorDynSize &constraint)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = control;

    if (!(m_pimpl->sameVariables(m_pimpl->updateDoneOnceConstraint))) {

        m_pimpl->updateDoneOnceConstraint = true;
        m_pimpl->updateVariables();

        iDynTree::SpatialMomentum expectedMomentum;
        expectedMomentum = m_pimpl->comTransform *
                (m_pimpl->robotState.world_T_base *
                 m_pimpl->sharedKinDyn->getLinearAngularMomentum(m_pimpl->robotState, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION));

        iDynTree::toEigen(m_pimpl->constraintValueBuffer) = iDynTree::toEigen(expectedMomentum) - iDynTree::toEigen(m_pimpl->momentum);
    }

    constraint = m_pimpl->constraintValueBuffer;

    return true;
}

bool CentroidalMomentumConstraint::constraintJacobianWRTState(double /*time*/, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &control, iDynTree::MatrixDynSize &jacobian)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = control;

    if (!(m_pimpl->sameVariables(m_pimpl->updateDoneOnceStateJacobian))) {

        m_pimpl->updateDoneOnceStateJacobian = true;
        m_pimpl->updateVariables();

        iDynTree::Transform G_T_B = m_pimpl->comTransform * m_pimpl->robotState.world_T_base;

        iDynTree::SpatialMomentum momentumInCoM, momentumInBase;
        momentumInBase = m_pimpl->sharedKinDyn->getLinearAngularMomentum(m_pimpl->robotState, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION);
        momentumInCoM = G_T_B * momentumInBase;


        bool ok = m_pimpl->sharedKinDyn->getLinearAngularMomentumJointsDerivative(m_pimpl->robotState, m_pimpl->momentumDerivativeBuffer);
        assert(ok);

        iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(m_pimpl->stateJacobianBuffer);

        jacobianMap.block(0, m_pimpl->jointsPositionRange.offset, 6, m_pimpl->jointsPositionRange.size) = iDynTree::toEigen(G_T_B.asAdjointTransformWrench()) * iDynTree::toEigen(m_pimpl->momentumDerivativeBuffer);

        jacobianMap.block<6,6>(0, m_pimpl->momentumRange.offset).setIdentity();

        jacobianMap.block<6,6>(0, m_pimpl->momentumRange.offset) *= -1;

        jacobianMap.block<3,3>(3, m_pimpl->comPositionRange.offset) = iDynTree::skew(iDynTree::toEigen(momentumInCoM).topRows<3>());

        jacobianMap.block<3,3>(3, m_pimpl->basePositionRange.offset) = -1 * iDynTree::skew(iDynTree::toEigen(momentumInCoM).topRows<3>());

        iDynTree::Matrix4x4 normalizedQuaternionDerivative = NormalizedQuaternionDerivative(m_pimpl->baseQuaternion);

        iDynTree::MatrixFixSize<3,4> linearPartDerivative;

        iDynTree::toEigen(linearPartDerivative) = iDynTree::toEigen(RotatedVectorQuaternionJacobian(momentumInBase.getLinearVec3(), m_pimpl->baseQuaternionNormalized)) *
                iDynTree::toEigen(normalizedQuaternionDerivative);

        jacobianMap.block<3,4>(0, m_pimpl->baseQuaternionRange.offset) = iDynTree::toEigen(linearPartDerivative);

        iDynTree::Position baseCoMDistance = m_pimpl->robotState.world_T_base.getPosition() + m_pimpl->comPositionInverse;

        jacobianMap.block<3,4>(3, m_pimpl->baseQuaternionRange.offset) = iDynTree::skew(iDynTree::toEigen(baseCoMDistance)) * iDynTree::toEigen(linearPartDerivative) +
                iDynTree::toEigen(RotatedVectorQuaternionJacobian(momentumInBase.getAngularVec3(), m_pimpl->baseQuaternionNormalized)) *
                iDynTree::toEigen(normalizedQuaternionDerivative);

    }

    jacobian = m_pimpl->stateJacobianBuffer;

    return true;
}

bool CentroidalMomentumConstraint::constraintJacobianWRTControl(double /*time*/, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &control, iDynTree::MatrixDynSize &jacobian)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = control;

    if (!(m_pimpl->sameVariables(m_pimpl->updateDoneOnceControlJacobian))) {

        m_pimpl->updateDoneOnceControlJacobian = true;
        m_pimpl->updateVariables();

        iDynTree::Transform G_T_B = m_pimpl->comTransform * m_pimpl->robotState.world_T_base;

        bool ok = m_pimpl->sharedKinDyn->getLinearAngularMomentumJacobian(m_pimpl->robotState, m_pimpl->cmmMatrixInBaseBuffer, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION);
        assert(ok);

        iDynTree::toEigen(m_pimpl->cmmMatrixInCoMBuffer) = iDynTree::toEigen(G_T_B.asAdjointTransformWrench()) * iDynTree::toEigen(m_pimpl->cmmMatrixInBaseBuffer);

        iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(m_pimpl->controlJacobianBuffer);

        jacobianMap.block<6,6>(0, m_pimpl->baseVelocityRange.offset) = iDynTree::toEigen(m_pimpl->cmmMatrixInCoMBuffer).leftCols<6>();

        jacobianMap.block(0, m_pimpl->jointsVelocityRange.offset, 6, m_pimpl->jointsVelocityRange.size) = iDynTree::toEigen(m_pimpl->cmmMatrixInCoMBuffer).rightCols(m_pimpl->jointsVelocityRange.size);
    }
    jacobian = m_pimpl->controlJacobianBuffer;

    return true;
}

size_t CentroidalMomentumConstraint::expectedStateSpaceSize() const
{
    return m_pimpl->stateVariables.size();
}

size_t CentroidalMomentumConstraint::expectedControlSpaceSize() const
{
    return m_pimpl->controlVariables.size();
}
