/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <levi/levi.h>
#include <DynamicalPlannerPrivate/Constraints/DynamicalConstraints.h>
#include <DynamicalPlannerPrivate/Utilities/QuaternionUtils.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <iDynTree/Core/Utils.h>
#include <cassert>
#include <iostream>
#include <regex>

using namespace DynamicalPlanner::Private;

class DynamicalConstraints::Implementation {
public:
    VariablesLabeller stateVariables;
    VariablesLabeller controlVariables;
    VariablesLabeller dynamics;
    VariablesLabeller lambda;
    double totalMass;
    iDynTree::Vector6 gravityVector;

    iDynTree::Position comPosition;
    iDynTree::MatrixDynSize comjacobianBuffer;


    iDynTree::Rotation baseRotation;
    iDynTree::Position basePosition;
    iDynTree::Vector4 baseQuaternion, baseQuaternionNormalized, baseQuaternionVelocity;
    RobotState robotState;
    std::shared_ptr<SharedKinDynComputations> sharedKinDyn;
    std::shared_ptr<TimelySharedKinDynComputations> timedSharedKinDyn;
    std::shared_ptr<ExpressionsServer> expressionServer;

    HyperbolicTangent activationXY;
    HyperbolicSecant normalForceActivation;
    double normalForceDissipation;
    levi::Variable force;
    levi::Expression skewForce;
    levi::Expression basePositionDerivative, basePositionDerivativeJacobian;
    std::vector<levi::Expression> basePositionDerivativeHessian;

    typedef struct {
        std::vector<iDynTree::IndexRange> positionPoints, forcePoints, velocityControlPoints, forceControlPoints;
    } FootRanges;
    FootRanges leftRanges, rightRanges;
    iDynTree::IndexRange momentumRange, comPositionRange, basePositionRange, baseQuaternionRange, jointsPositionRange, jointsVelocityRange;
//    iDynTree::IndexRange baseVelocityRange;
    iDynTree::IndexRange baseLinearVelocityRange, baseQuaternionDerivativeRange;

    //iDynTree::MatrixDynSize stateJacobianBuffer, controlJacobianBuffer;
    iDynTree::optimalcontrol::SparsityStructure stateJacobianSparsity, controlJacobianSparsity;
    iDynTree::optimalcontrol::SparsityStructure stateHessianSparsity, controlHessianSparsity, mixedHessianSparsity;

    void checkFootVariables(const std::string& footName, size_t numberOfPoints, FootRanges& foot) {
        foot.positionPoints.resize(numberOfPoints);
        foot.forcePoints.resize(numberOfPoints);
        foot.velocityControlPoints.resize(numberOfPoints);
        foot.forceControlPoints.resize(numberOfPoints);

        iDynTree::IndexRange obtainedRange;
        for (size_t i= 0; i < numberOfPoints; ++i) {
            obtainedRange = stateVariables.getIndexRange(footName + "ForcePoint" + std::to_string(i));

            if (!obtainedRange.isValid()){
                std::cerr << "[ERROR][DynamicalConstraints::DynamicalConstraints] Variable " << footName + "ForcePoint" + std::to_string(i) << " not available among state variables." << std::endl;
                assert(false);
            }
            foot.forcePoints[i] = obtainedRange;

            obtainedRange = stateVariables.getIndexRange(footName + "PositionPoint" + std::to_string(i));
            if (!obtainedRange.isValid()){
                std::cerr << "[ERROR][DynamicalConstraints::DynamicalConstraints] Variable " << footName + "PositionPoint" + std::to_string(i) << " not available among state variables." << std::endl;
                assert(false);
            }
            foot.positionPoints[i] = obtainedRange;

            obtainedRange = controlVariables.getIndexRange(footName + "ForceControlPoint" + std::to_string(i));
            if (!obtainedRange.isValid()){
                std::cerr << "[ERROR][DynamicalConstraints::DynamicalConstraints] Variable " << footName + "ForceControlPoint" + std::to_string(i) << " not available among control variables." << std::endl;
                assert(false);
            }
            foot.forceControlPoints[i] = obtainedRange;

            obtainedRange = controlVariables.getIndexRange(footName + "VelocityControlPoint" + std::to_string(i));
            if (!obtainedRange.isValid()){
                std::cerr << "[ERROR][DynamicalConstraints::DynamicalConstraints] Variable " << footName + "VelocityControlPoint" + std::to_string(i) << " not available among control variables." << std::endl;
                assert(false);
            }
            foot.velocityControlPoints[i] = obtainedRange;
        }
    }


    void computeFootRelatedDynamics(FootRanges& foot) {
        Eigen::Vector3d distance, appliedForce;

        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            //Span operator = does not copy content!

            double pz = stateVariables(foot.positionPoints[i])(2);
            double deltaZ = normalForceActivation.eval(pz);
            double fz = stateVariables(foot.forcePoints[i])(2);
            double uz = controlVariables(foot.forceControlPoints[i])(2);

            iDynTree::toEigen(dynamics(foot.forcePoints[i])).topRows<2>() =
                iDynTree::toEigen(controlVariables(foot.forceControlPoints[i])).topRows<2>();

            dynamics(foot.forcePoints[i])(2) = deltaZ * uz + normalForceDissipation * (deltaZ - 1.0) * fz;

            double deltaXY = activationXY.eval(stateVariables(foot.positionPoints[i])(2));
            iDynTree::toEigen(dynamics(foot.positionPoints[i])).topRows<2>() = deltaXY * iDynTree::toEigen(controlVariables(foot.velocityControlPoints[i])).topRows<2>();
            dynamics(foot.positionPoints[i])(2) = controlVariables(foot.velocityControlPoints[i])(2);

            iDynTree::toEigen(dynamics(momentumRange)).topRows<3>() +=  iDynTree::toEigen(stateVariables(foot.forcePoints[i]));

            distance = iDynTree::toEigen(stateVariables(foot.positionPoints[i])) - iDynTree::toEigen(comPosition);
            appliedForce = iDynTree::toEigen(stateVariables(foot.forcePoints[i]));
            iDynTree::toEigen(dynamics(momentumRange)).bottomRows<3>() += distance.cross(appliedForce);
        }
    }

    void computeFootRelatedStateJacobian(const FootRanges& foot, iDynTree::iDynTreeEigenMatrixMap& jacobianMap) {
        Eigen::Vector3d distance, appliedForce;

        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            double deltaXYDerivative = activationXY.evalDerivative(stateVariables(foot.positionPoints[i])(2));

            distance = iDynTree::toEigen(stateVariables(foot.positionPoints[i])) - iDynTree::toEigen(comPosition);
            appliedForce = iDynTree::toEigen(stateVariables(foot.forcePoints[i]));

            jacobianMap.block<3,3>(momentumRange.offset, foot.forcePoints[i].offset).setIdentity();
            jacobianMap.block<3,3>(momentumRange.offset+3, foot.forcePoints[i].offset) = iDynTree::skew(distance);
            jacobianMap.block<3,3>(momentumRange.offset+3, foot.positionPoints[i].offset) = -iDynTree::skew(appliedForce);
            jacobianMap.block<3,3>(momentumRange.offset+3, comPositionRange.offset) += iDynTree::skew(appliedForce);

            double pz = stateVariables(foot.positionPoints[i])(2);
            double deltaZDerivative = normalForceActivation.evalDerivative(pz);
            double deltaZ = normalForceActivation.eval(pz);
            double fz = stateVariables(foot.forcePoints[i])(2);
            double uz = controlVariables(foot.forceControlPoints[i])(2);

            jacobianMap(foot.forcePoints[i].offset + 2, foot.positionPoints[i].offset + 2) = deltaZDerivative * (uz + normalForceDissipation * fz);
            jacobianMap(foot.forcePoints[i].offset + 2, foot.forcePoints[i].offset + 2) = normalForceDissipation * (deltaZ - 1.0);

            jacobianMap.block<2,1>(foot.positionPoints[i].offset, foot.positionPoints[i].offset + 2) = deltaXYDerivative * iDynTree::toEigen(controlVariables(foot.velocityControlPoints[i])).topRows<2>();

//            jacobianMap.block<3,3>(momentumRange.offset+3, basePositionRange.offset) += iDynTree::skew(appliedForce) * iDynTree::toEigen(comjacobianBuffer).leftCols<3>();
//            jacobianMap.block<3,4>(momentumRange.offset+3, baseQuaternionRange.offset) += iDynTree::skew(appliedForce) * iDynTree::toEigen(comjacobianBuffer).block<3,3>(0,3) *
//                    iDynTree::toEigen(iDynTree::Rotation::QuaternionRightTrivializedDerivativeInverse(baseQuaternionNormalized)) *
//                    iDynTree::toEigen(NormalizedQuaternionDerivative(baseQuaternion));
//            jacobianMap.block(momentumRange.offset + 3, jointsPositionRange.offset, 3, jointsPositionRange.size) += iDynTree::skew(appliedForce) * iDynTree::toEigen(comjacobianBuffer).rightCols(jointsPositionRange.size);
        }
    }

    void computeFootRelatedControlJacobian(FootRanges& foot, iDynTree::iDynTreeEigenMatrixMap& jacobianMap) {
        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            double deltaXY = activationXY.eval(stateVariables(foot.positionPoints[i])(2));
            double pz = stateVariables(foot.positionPoints[i])(2);
            double deltaZ = normalForceActivation.eval(pz);

            jacobianMap.block<2,2>(foot.forcePoints[i].offset, foot.forceControlPoints[i].offset).setIdentity();
            jacobianMap(foot.forcePoints[i].offset + 2, foot.forceControlPoints[i].offset + 2) = deltaZ;

            jacobianMap.block<3,3>(foot.positionPoints[i].offset, foot.velocityControlPoints[i].offset).setIdentity();
            jacobianMap.block<2,2>(foot.positionPoints[i].offset, foot.velocityControlPoints[i].offset) *= deltaXY;
        }
    }

    void computeFootRelatedStateHessian(FootRanges& foot, iDynTree::iDynTreeEigenMatrixMap& hessianMap) {
        Eigen::Matrix<double, 1, 3> forceHessian;
        Eigen::Matrix3d derivative;
        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            force = iDynTree::toEigen(stateVariables(foot.forcePoints[i]));
            for (unsigned int j = 0; j < 3; ++j) {
                derivative = skewForce.getColumnDerivative(j, force).evaluate();

                forceHessian = iDynTree::toEigen(lambda(momentumRange)).bottomRows<3>().transpose() * derivative;

                hessianMap.block<1, 3>(comPositionRange.offset + j, foot.forcePoints[i].offset) = forceHessian;

                hessianMap.block<3, 1>(foot.forcePoints[i].offset, comPositionRange.offset + j) = forceHessian.transpose();

                hessianMap.block<1, 3>(foot.positionPoints[i].offset + j, foot.forcePoints[i].offset) =-forceHessian;

                hessianMap.block<3, 1>(foot.forcePoints[i].offset, foot.positionPoints[i].offset + j) = -forceHessian.transpose();
            }

            double pz = stateVariables(foot.positionPoints[i])(2);
            double deltaXYDoubleDerivative = activationXY.evalDoubleDerivative(pz);
            double deltaZDoubleDerivative = normalForceActivation.evalDoubleDerivative(pz);
            double deltaZDerivative = normalForceActivation.evalDerivative(pz);
            double fz = stateVariables(foot.forcePoints[i])(2);
            double uz = controlVariables(foot.forceControlPoints[i])(2);

            hessianMap(foot.positionPoints[i].offset + 2, foot.positionPoints[i].offset + 2) =
                deltaXYDoubleDerivative * (lambda(foot.positionPoints[i])(0) * controlVariables(foot.velocityControlPoints[i])(0) +
                                           lambda(foot.positionPoints[i])(1) * controlVariables(foot.velocityControlPoints[i])(1)) +
                deltaZDoubleDerivative * lambda(foot.forcePoints[i])(2) * (uz + normalForceDissipation * fz);

            hessianMap(foot.positionPoints[i].offset + 2, foot.forcePoints[i].offset + 2) += deltaZDerivative * normalForceDissipation * lambda(foot.forcePoints[i])(2);
            hessianMap(foot.forcePoints[i].offset + 2, foot.positionPoints[i].offset + 2) =
                hessianMap(foot.positionPoints[i].offset + 2, foot.forcePoints[i].offset + 2);
        }
    }

    void computeFootRelatedMixedHessian(FootRanges& foot, iDynTree::iDynTreeEigenMatrixMap& hessianMap) {
        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            double pz = stateVariables(foot.positionPoints[i])(2);
            double deltaXYDerivative = activationXY.evalDerivative(pz);
            double deltaZDerivative = normalForceActivation.evalDerivative(pz);

            hessianMap(foot.positionPoints[i].offset + 2, foot.velocityControlPoints[i].offset) =
                deltaXYDerivative * lambda(foot.positionPoints[i])(0);

            hessianMap(foot.positionPoints[i].offset + 2, foot.velocityControlPoints[i].offset + 1) =
                deltaXYDerivative * lambda(foot.positionPoints[i])(1);

            hessianMap(foot.positionPoints[i].offset + 2, foot.forceControlPoints[i].offset + 2) = deltaZDerivative * lambda(foot.forcePoints[i])(2);
        }
    }

    void updateRobotState() {

        robotState = sharedKinDyn->currentState();

        iDynTree::toEigen(basePosition) = iDynTree::toEigen(stateVariables(basePositionRange));
        baseQuaternion = stateVariables(baseQuaternionRange);
        baseQuaternionNormalized = NormalizedQuaternion(baseQuaternion);
        assert(QuaternionBoundsRespected(baseQuaternionNormalized));
        baseRotation.fromQuaternion(baseQuaternionNormalized);
        iDynTree::toEigen(baseQuaternionVelocity) = iDynTree::toEigen(controlVariables(baseQuaternionDerivativeRange));

        robotState.base_quaternion = baseQuaternion;
        robotState.base_position = basePosition;

        robotState.s = stateVariables(jointsPositionRange);

        robotState.s_dot = controlVariables(jointsVelocityRange);

        iDynTree::LinVelocity baseLinVelocity, baseAngVelocity;

        iDynTree::toEigen(robotState.base_linearVelocity) = iDynTree::toEigen(controlVariables(baseLinearVelocityRange));
        robotState.base_quaternionVelocity = baseQuaternionVelocity;

        iDynTree::toEigen(comPosition) = iDynTree::toEigen(stateVariables(comPositionRange));

        sharedKinDyn->updateRobotState(robotState);
    }

    void setFootRelatedStateSparsity(const FootRanges& foot) {

        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            stateJacobianSparsity.addIdentityBlock(static_cast<size_t>(momentumRange.offset), static_cast<size_t>(foot.forcePoints[i].offset), 3);
            stateJacobianSparsity.addDenseBlock(static_cast<size_t>(momentumRange.offset) + 3,
                                                static_cast<size_t>(foot.forcePoints[i].offset), 3, 3);
            stateJacobianSparsity.addDenseBlock(static_cast<size_t>(momentumRange.offset) + 3,
                                                static_cast<size_t>(foot.positionPoints[i].offset), 3, 3);
            stateJacobianSparsity.addDenseBlock(static_cast<size_t>(foot.positionPoints[i].offset),
                                                static_cast<size_t>(foot.positionPoints[i].offset) + 2, 2, 1);
            stateJacobianSparsity.add(static_cast<size_t>(foot.forcePoints[i].offset + 2), static_cast<size_t>(foot.positionPoints[i].offset + 2));

            stateHessianSparsity.addDenseBlock(comPositionRange, foot.forcePoints[i]);
            stateHessianSparsity.addDenseBlock(foot.forcePoints[i], comPositionRange);
            stateHessianSparsity.addDenseBlock(foot.positionPoints[i], foot.forcePoints[i]);
            stateHessianSparsity.addDenseBlock(foot.forcePoints[i], foot.positionPoints[i]);
            stateHessianSparsity.add(static_cast<size_t>(foot.positionPoints[i].offset + 2),
                                     static_cast<size_t>(foot.positionPoints[i].offset + 2));

            mixedHessianSparsity.add(static_cast<size_t>(foot.positionPoints[i].offset + 2),
                                     static_cast<size_t>(foot.velocityControlPoints[i].offset));
            mixedHessianSparsity.add(static_cast<size_t>(foot.positionPoints[i].offset + 2),
                                     static_cast<size_t>(foot.velocityControlPoints[i].offset + 1));
            mixedHessianSparsity.add(static_cast<size_t>(foot.positionPoints[i].offset + 2),
                                     static_cast<size_t>(foot.forceControlPoints[i].offset + 2));
        }
    }

    void setFootRelatedControlSparsity(const FootRanges& foot) {
        for (size_t i = 0; i < foot.positionPoints.size(); ++i) {
            controlJacobianSparsity.addIdentityBlock(static_cast<size_t>(foot.forcePoints[i].offset),
                                                     static_cast<size_t>(foot.forceControlPoints[i].offset), 3);
            controlJacobianSparsity.addIdentityBlock(static_cast<size_t>(foot.positionPoints[i].offset),
                                                     static_cast<size_t>(foot.velocityControlPoints[i].offset), 3);
        }
    }

    void setSparsity() {
        stateJacobianSparsity.clear();
        controlJacobianSparsity.clear();

        setFootRelatedStateSparsity(leftRanges);
        setFootRelatedStateSparsity(rightRanges);
        stateJacobianSparsity.addDenseBlock(static_cast<size_t>(momentumRange.offset) + 3, static_cast<size_t>(comPositionRange.offset), 3, 3);
        stateJacobianSparsity.addIdentityBlock(static_cast<size_t>(comPositionRange.offset), static_cast<size_t>(momentumRange.offset), 3);
        stateJacobianSparsity.addDenseBlock(basePositionRange, baseQuaternionRange);
        stateJacobianSparsity.addDenseBlock(baseQuaternionRange, baseQuaternionRange);

        setFootRelatedControlSparsity(leftRanges);
        setFootRelatedControlSparsity(rightRanges);
        controlJacobianSparsity.addDenseBlock(basePositionRange, baseLinearVelocityRange);
//        controlSparsity.addDenseBlock(static_cast<size_t>(baseQuaternionRange.offset), static_cast<size_t>(baseVelocityRange.offset) + 3, 4, 3);
        controlJacobianSparsity.addIdentityBlock(static_cast<size_t>(baseQuaternionRange.offset),
                                                 static_cast<size_t>(baseQuaternionDerivativeRange.offset), 4);
        controlJacobianSparsity.addIdentityBlock(static_cast<size_t>(jointsPositionRange.offset),
                                                 static_cast<size_t>(jointsVelocityRange.offset), static_cast<size_t>(jointsPositionRange.size));

        stateHessianSparsity.addDenseBlock(baseQuaternionRange, baseQuaternionRange);
        mixedHessianSparsity.addDenseBlock(baseQuaternionRange, baseLinearVelocityRange);
        //other sparsity set in setFootRelated*Sparsity

        controlHessianSparsity.clear();

    }
};


DynamicalConstraints::DynamicalConstraints(const VariablesLabeller &stateVariables, const VariablesLabeller &controlVariables,
                                           std::shared_ptr<TimelySharedKinDynComputations> timelySharedKinDyn,
                                           std::shared_ptr<ExpressionsServer> expressionsServer, const HyperbolicTangent& planarVelocityActivation,
                                           const HyperbolicSecant &normalForceActivation,  double forceDissipationRatio)
   : iDynTree::optimalcontrol::DynamicalSystem (stateVariables.size(), controlVariables.size())
   , m_pimpl(std::make_unique<Implementation>())
{
    assert(timelySharedKinDyn);
    assert(timelySharedKinDyn->isValid());
    m_pimpl->timedSharedKinDyn = timelySharedKinDyn;
    m_pimpl->expressionServer = expressionsServer;
    m_pimpl->stateVariables = stateVariables;
    m_pimpl->controlVariables = controlVariables;
    m_pimpl->dynamics = m_pimpl->stateVariables;
    m_pimpl->dynamics.zero();
    m_pimpl->lambda = m_pimpl->stateVariables;
    m_pimpl->lambda.zero();

    m_pimpl->totalMass = 0.0;

    const iDynTree::Model & model = timelySharedKinDyn->model();

    for(size_t l=0; l < model.getNrOfLinks(); l++)
    {
        m_pimpl->totalMass += model.getLink(static_cast<iDynTree::LinkIndex>(l))->getInertia().getMass();
    }


    m_pimpl->gravityVector.zero();
    m_pimpl->gravityVector(2) = -9.81;

//    m_pimpl->stateJacobianBuffer.resize(static_cast<unsigned int>(stateVariables.size()), static_cast<unsigned int>(stateVariables.size()));
//    m_pimpl->stateJacobianBuffer.zero();

//    m_pimpl->controlJacobianBuffer.resize(static_cast<unsigned int>(stateVariables.size()), static_cast<unsigned int>(controlVariables.size()));
//    m_pimpl->controlJacobianBuffer.zero();

    m_pimpl->normalForceActivation = normalForceActivation;
    m_pimpl->activationXY = planarVelocityActivation;
    m_pimpl->normalForceDissipation = forceDissipationRatio;

    size_t leftPoints = 0, rightPoints = 0;
    for (auto& label : stateVariables.listOfLabels()) {
        if (label.find("LeftForcePoint") != std::string::npos) {
            leftPoints++;
        }

        if (label.find("RightForcePoint") != std::string::npos) {
             rightPoints++;
        }
    }

    m_pimpl->checkFootVariables("Left",leftPoints, m_pimpl->leftRanges);
    m_pimpl->checkFootVariables("Right", rightPoints, m_pimpl->rightRanges);

    m_pimpl->momentumRange = m_pimpl->stateVariables.getIndexRange("Momentum");
    assert(m_pimpl->momentumRange.isValid());

    m_pimpl->comPositionRange = m_pimpl->stateVariables.getIndexRange("CoMPosition");
    assert(m_pimpl->comPositionRange.isValid());

    m_pimpl->basePositionRange = m_pimpl->stateVariables.getIndexRange("BasePosition");
    assert(m_pimpl->basePositionRange.isValid());

    m_pimpl->baseQuaternionRange = m_pimpl->stateVariables.getIndexRange("BaseQuaternion");
    assert(m_pimpl->baseQuaternionRange.isValid());

    m_pimpl->jointsPositionRange = m_pimpl->stateVariables.getIndexRange("JointsPosition");
    assert(m_pimpl->jointsPositionRange.isValid());

//    m_pimpl->baseVelocityRange = m_pimpl->controlVariables.getIndexRange("BaseVelocity");
//    assert(m_pimpl->baseVelocityRange.isValid());

    m_pimpl->baseLinearVelocityRange = m_pimpl->controlVariables.getIndexRange("BaseLinearVelocity");
    assert(m_pimpl->baseLinearVelocityRange.isValid());

    m_pimpl->baseQuaternionDerivativeRange = m_pimpl->controlVariables.getIndexRange("BaseQuaternionDerivative");
    assert(m_pimpl->baseQuaternionDerivativeRange.isValid());

    m_pimpl->jointsVelocityRange = m_pimpl->controlVariables.getIndexRange("JointsVelocity");
    assert(m_pimpl->jointsVelocityRange.isValid());

    m_pimpl->comjacobianBuffer.resize(3, 6 + static_cast<unsigned int>(m_pimpl->jointsPositionRange.size));
    m_pimpl->comjacobianBuffer.zero();

    m_pimpl->setSparsity();

    m_pimpl->force = levi::Variable(3, "f");
    m_pimpl->skewForce = m_pimpl->force.skew();

    m_pimpl->basePositionDerivative = (m_pimpl->expressionServer->baseRotation()) * (m_pimpl->expressionServer->baseLinearVelocity());
    m_pimpl->basePositionDerivativeJacobian = m_pimpl->basePositionDerivative.getColumnDerivative(0, m_pimpl->expressionServer->baseQuaternion());

    for (long i = 0; i < 4; ++i) {
        m_pimpl->basePositionDerivativeHessian.push_back(m_pimpl->basePositionDerivativeJacobian.getColumnDerivative(i, m_pimpl->expressionServer->baseQuaternion()));
    }


}

DynamicalConstraints::~DynamicalConstraints()
{

}

bool DynamicalConstraints::dynamics(const iDynTree::VectorDynSize &state, double time, iDynTree::VectorDynSize &stateDynamics)
{
    m_pimpl->stateVariables = state; //this line must remain before those computing the feet related quantities
    m_pimpl->controlVariables = controlInput(); //this line must remain before those computing the feet related quantities

    m_pimpl->sharedKinDyn = m_pimpl->timedSharedKinDyn->get(time);

    m_pimpl->updateRobotState();

//    m_pimpl->comPosition = m_pimpl->sharedKinDyn->getCenterOfMassPosition(m_pimpl->robotState);

    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->momentumRange)) = m_pimpl->totalMass * iDynTree::toEigen(m_pimpl->gravityVector); //this line must remain before those computing the feet related quantities

    m_pimpl->computeFootRelatedDynamics(m_pimpl->leftRanges);

    m_pimpl->computeFootRelatedDynamics(m_pimpl->rightRanges);

    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->comPositionRange)) = iDynTree::toEigen(m_pimpl->stateVariables(m_pimpl->momentumRange)).topRows<3>()/m_pimpl->totalMass;

    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->basePositionRange)) = iDynTree::toEigen(m_pimpl->baseRotation) * iDynTree::toEigen(m_pimpl->robotState.base_linearVelocity);

//    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->baseQuaternionRange)) = iDynTree::toEigen(QuaternionLeftTrivializedDerivative(m_pimpl->baseQuaternionNormalized)) * iDynTree::toEigen(m_pimpl->robotState.base_velocity.getAngularVec3());

    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->baseQuaternionRange)) = iDynTree::toEigen(m_pimpl->baseQuaternionVelocity);

    iDynTree::toEigen(m_pimpl->dynamics(m_pimpl->jointsPositionRange)) = iDynTree::toEigen(m_pimpl->robotState.s_dot);

    stateDynamics = m_pimpl->dynamics.values();

    return true;
}

bool DynamicalConstraints::dynamicsStateFirstDerivative(const iDynTree::VectorDynSize &state, double time, iDynTree::MatrixDynSize &dynamicsDerivative)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = controlInput();
    m_pimpl->sharedKinDyn = m_pimpl->timedSharedKinDyn->get(time);

    m_pimpl->updateRobotState();
//    m_pimpl->expressionServer->updateRobotState(time);
//    m_pimpl->comPosition = m_pimpl->sharedKinDyn->getCenterOfMassPosition(m_pimpl->robotState);
//    bool ok = m_pimpl->sharedKinDyn->getCenterOfMassJacobian(m_pimpl->robotState, m_pimpl->comjacobianBuffer, iDynTree::FrameVelocityRepresentation::MIXED_REPRESENTATION);

//    iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(m_pimpl->stateJacobianBuffer);
    iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(dynamicsDerivative);

//    jacobianMap.block<3,3>(m_pimpl->momentumRange.offset+3, m_pimpl->basePositionRange.offset).setZero();
//    jacobianMap.block<3,4>(m_pimpl->momentumRange.offset+3, m_pimpl->baseQuaternionRange.offset).setZero();
//    jacobianMap.block(m_pimpl->momentumRange.offset + 3, m_pimpl->jointsPositionRange.offset, 3, m_pimpl->jointsPositionRange.size).setZero();
    jacobianMap.block<3,3>(m_pimpl->momentumRange.offset+3, m_pimpl->comPositionRange.offset).setZero();

    m_pimpl->computeFootRelatedStateJacobian(m_pimpl->leftRanges, jacobianMap);
    m_pimpl->computeFootRelatedStateJacobian(m_pimpl->rightRanges, jacobianMap);

    jacobianMap.block<3,3>(m_pimpl->comPositionRange.offset, m_pimpl->momentumRange.offset).setIdentity();
    jacobianMap.block<3,3>(m_pimpl->comPositionRange.offset, m_pimpl->momentumRange.offset) *= 1.0/m_pimpl->totalMass;


    iDynTree::Matrix4x4 normalizedQuaternionDerivative = NormalizedQuaternionDerivative(m_pimpl->baseQuaternion);
    jacobianMap.block<3,4>(m_pimpl->basePositionRange.offset, m_pimpl->baseQuaternionRange.offset) =
            iDynTree::toEigen(RotatedVectorQuaternionJacobian(m_pimpl->robotState.base_linearVelocity, m_pimpl->baseQuaternionNormalized)) * iDynTree::toEigen(normalizedQuaternionDerivative);

//    jacobianMap.block<4,4>(m_pimpl->baseQuaternionRange.offset, m_pimpl->baseQuaternionRange.offset) = iDynTree::toEigen(QuaternionLeftTrivializedDerivativeTimesOmegaJacobian(m_pimpl->robotState.base_velocity.getAngularVec3())) * iDynTree::toEigen(normalizedQuaternionDerivative);

   // dynamicsDerivative = m_pimpl->stateJacobianBuffer;
    return true;
}

bool DynamicalConstraints::dynamicsControlFirstDerivative(const iDynTree::VectorDynSize &state, double time, iDynTree::MatrixDynSize &dynamicsDerivative)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = controlInput();

    m_pimpl->sharedKinDyn = m_pimpl->timedSharedKinDyn->get(time);

    m_pimpl->updateRobotState();

//    iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(m_pimpl->controlJacobianBuffer);
    iDynTree::iDynTreeEigenMatrixMap jacobianMap = iDynTree::toEigen(dynamicsDerivative);


    m_pimpl->computeFootRelatedControlJacobian(m_pimpl->leftRanges, jacobianMap);
    m_pimpl->computeFootRelatedControlJacobian(m_pimpl->rightRanges, jacobianMap);


    jacobianMap.block<3,3>(m_pimpl->basePositionRange.offset, m_pimpl->baseLinearVelocityRange.offset) = iDynTree::toEigen(m_pimpl->baseRotation);

    jacobianMap.block<4,4>(m_pimpl->baseQuaternionRange.offset, m_pimpl->baseQuaternionDerivativeRange.offset).setIdentity();

    jacobianMap.block(m_pimpl->jointsPositionRange.offset, m_pimpl->jointsVelocityRange.offset, m_pimpl->jointsPositionRange.size, m_pimpl->jointsVelocityRange.size).setIdentity();

//    dynamicsDerivative = m_pimpl->controlJacobianBuffer;
    return true;
}

bool DynamicalConstraints::dynamicsStateFirstDerivativeSparsity(iDynTree::optimalcontrol::SparsityStructure &stateSparsity)
{
    stateSparsity = m_pimpl->stateJacobianSparsity;
    return true;
}

bool DynamicalConstraints::dynamicsControlFirstDerivativeSparsity(iDynTree::optimalcontrol::SparsityStructure &controlSparsity)
{
    controlSparsity = m_pimpl->controlJacobianSparsity;
    return true;
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTState(double time, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &lambda, iDynTree::MatrixDynSize &partialDerivative)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = controlInput();
    m_pimpl->lambda = lambda;

    m_pimpl->sharedKinDyn = m_pimpl->timedSharedKinDyn->get(time);

    m_pimpl->updateRobotState();
    m_pimpl->expressionServer->updateRobotState(time);


    iDynTree::iDynTreeEigenMatrixMap hessianMap = iDynTree::toEigen(partialDerivative);

    Eigen::Matrix<double, 1, 4> quaternionHessian;

    for (unsigned int i = 0; i < 4; ++i) {
        quaternionHessian = iDynTree::toEigen(m_pimpl->lambda(m_pimpl->basePositionRange)).transpose() *
            m_pimpl->basePositionDerivativeHessian[i].evaluate();

        hessianMap.block<1,4>(m_pimpl->baseQuaternionRange.offset + i, m_pimpl->baseQuaternionRange.offset) = quaternionHessian;
    }

    m_pimpl->computeFootRelatedStateHessian(m_pimpl->leftRanges, hessianMap);
    m_pimpl->computeFootRelatedStateHessian(m_pimpl->rightRanges, hessianMap);

    return true;
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTControl(double /*time*/, const iDynTree::VectorDynSize &/*state*/, const iDynTree::VectorDynSize &/*lambda*/, iDynTree::MatrixDynSize &/*partialDerivative*/)
{
    return true; //assume that the input hessian is zero
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTStateControl(double time, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &lambda, iDynTree::MatrixDynSize &partialDerivative)
{
    m_pimpl->stateVariables = state;
    m_pimpl->controlVariables = controlInput();
    m_pimpl->lambda = lambda;

    m_pimpl->sharedKinDyn = m_pimpl->timedSharedKinDyn->get(time);

    m_pimpl->updateRobotState();
    m_pimpl->expressionServer->updateRobotState(time);

    iDynTree::iDynTreeEigenMatrixMap hessianMap = iDynTree::toEigen(partialDerivative);

    levi::Expression baseRotation = (m_pimpl->expressionServer->baseRotation());

    for (unsigned int i = 0; i < 3; ++i) {
        hessianMap.block<4,1>(m_pimpl->baseQuaternionRange.offset, m_pimpl->baseLinearVelocityRange.offset + i) =
            (baseRotation.getColumnDerivative(i, (m_pimpl->expressionServer->baseQuaternion())).evaluate()).transpose() *
            iDynTree::toEigen(m_pimpl->lambda(m_pimpl->basePositionRange));
    }

    m_pimpl->computeFootRelatedMixedHessian(m_pimpl->leftRanges, hessianMap);
    m_pimpl->computeFootRelatedMixedHessian(m_pimpl->rightRanges, hessianMap);

    return true;
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTStateSparsity(iDynTree::optimalcontrol::SparsityStructure &stateSparsity)
{
    stateSparsity = m_pimpl->stateHessianSparsity;
    return true;
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTStateControlSparsity(iDynTree::optimalcontrol::SparsityStructure &stateControlSparsity)
{
    stateControlSparsity = m_pimpl->mixedHessianSparsity;
    return true;
}

bool DynamicalConstraints::dynamicsSecondPartialDerivativeWRTControlSparsity(iDynTree::optimalcontrol::SparsityStructure &controlSparsity)
{
    controlSparsity = m_pimpl->controlHessianSparsity;
    return true;
}
