/*
* Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
* Authors: Stefano Dafarra
* CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
*
*/

#include <levi/levi.h>
#include <DynamicalPlannerPrivate/Utilities/ExpressionsServer.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <cassert>
#include <unordered_map>

using namespace DynamicalPlanner::Private;

using ExpressionMap = std::unordered_map<std::string, levi::Expression>;

class ExpressionsServer::Implementation {
public:
    std::shared_ptr<TimelySharedKinDynComputations> timelySharedKinDyn;
    SharedKinDynComputationsPointer kinDyn;
    levi::ScalarVariable time = levi::ScalarVariable("t");
    levi::Variable quaternion = levi::Variable(4, "q");
    levi::Expression quaternionNormalized;
    levi::Expression baseRotation;
    levi::Variable basePositionExpr = levi::Variable(3, "aPb");
    levi::Variable baseLinearVelocity = levi::Variable(3, "baseLinVel");
    levi::Variable baseQuaternionVelocity = levi::Variable(4, "baseQuatVel");

    levi::Variable q, q_dot;
    ExpressionMap adjointMap, adjointWrenchMap;
    RobotState robotState;

    bool first;

};



ExpressionsServer::ExpressionsServer(std::shared_ptr<TimelySharedKinDynComputations> timelySharedKinDyn)
    : m_pimpl(std::make_unique<Implementation>())
{
    assert(timelySharedKinDyn);
    m_pimpl->timelySharedKinDyn = timelySharedKinDyn;

    m_pimpl->quaternionNormalized = m_pimpl->quaternion/(m_pimpl->quaternion.transpose() * m_pimpl->quaternion).pow(0.5);
    levi::Expression skewQuaternion = m_pimpl->quaternionNormalized.block(1,0,3,1).skew();
    levi::Expression twoSkewQuaternion = 2.0 * skewQuaternion;
    m_pimpl->baseRotation = levi::Identity(3,3) + m_pimpl->quaternionNormalized(0,0) * twoSkewQuaternion + twoSkewQuaternion * skewQuaternion;

    m_pimpl->q = levi::Variable(timelySharedKinDyn->model().getNrOfDOFs(), "q");
    m_pimpl->q_dot = levi::Variable(timelySharedKinDyn->model().getNrOfDOFs(), "q_dot");

    m_pimpl->first = true;

}

ExpressionsServer::~ExpressionsServer()
{

}

bool ExpressionsServer::updateRobotState(double time, const RobotState &currentState)
{
    m_pimpl->time = time;
    m_pimpl->kinDyn = m_pimpl->timelySharedKinDyn->get(time);

    if (m_pimpl->first || !(m_pimpl->kinDyn->sameState(currentState))) {
        if (!(m_pimpl->kinDyn->updateRobotState(currentState))){
            return false;
        }

        m_pimpl->quaternion = iDynTree::toEigen(currentState.base_quaternion);
        m_pimpl->basePositionExpr = iDynTree::toEigen(currentState.base_position);
        m_pimpl->q = iDynTree::toEigen(currentState.s);
        m_pimpl->q_dot = iDynTree::toEigen(currentState.s_dot);
        m_pimpl->baseLinearVelocity = iDynTree::toEigen(currentState.base_linearVelocity);
        m_pimpl->baseQuaternionVelocity = iDynTree::toEigen(currentState.base_quaternionVelocity);

        m_pimpl->robotState = m_pimpl->kinDyn->currentState();

        m_pimpl->first = false;
    }

    return true;
}

levi::Expression *ExpressionsServer::baseRotation()
{
    return &(m_pimpl->baseRotation);
}

levi::Variable *ExpressionsServer::baseQuaternion()
{
    return &(m_pimpl->quaternion);
}

levi::Variable *ExpressionsServer::basePosition()
{
    return &(m_pimpl->basePositionExpr);
}

levi::Variable *ExpressionsServer::baseLinearVelocity()
{
    return &(m_pimpl->baseLinearVelocity);
}

levi::Variable *ExpressionsServer::baseAngularVelocity()
{
    return &(m_pimpl->baseQuaternionVelocity);
}

levi::Variable *ExpressionsServer::jointsPosition()
{
    return &(m_pimpl->q);
}

levi::Variable *ExpressionsServer::jointsVelocity()
{
    return &(m_pimpl->q_dot);
}

levi::Expression *ExpressionsServer::adjointTransform(const std::string &baseFrame, const std::string &targetFrame)
{
    ExpressionMap::iterator element = m_pimpl->adjointMap.find(baseFrame+targetFrame);

    if (element != m_pimpl->adjointMap.end()) {
        return &(element->second);
    } else {
        std::pair<std::string, levi::Expression> newElement;
        newElement.first = baseFrame + targetFrame;
        newElement.second = AdjointTransformExpression(m_pimpl->timelySharedKinDyn,
                                                       &(m_pimpl->robotState),
                                                       baseFrame,
                                                       targetFrame,
                                                       m_pimpl->q,
                                                       m_pimpl->time);
        auto result = m_pimpl->adjointMap.insert(newElement);
        assert(result.second);
        return &(result.first->second);
    }
}

levi::Expression *ExpressionsServer::adjointTransformWrench(const std::string &baseFrame, const std::string &targetFrame)
{
    ExpressionMap::iterator element = m_pimpl->adjointWrenchMap.find(baseFrame+targetFrame);

    if (element != m_pimpl->adjointWrenchMap.end()) {
        return &(element->second);
    } else {
        std::pair<std::string, levi::Expression> newElement;
        newElement.first = baseFrame + targetFrame;
        newElement.second = AdjointTransformWrenchExpression(m_pimpl->timelySharedKinDyn,
                                                             &(m_pimpl->robotState),
                                                             baseFrame,
                                                             targetFrame,
                                                             m_pimpl->q,
                                                             m_pimpl->time);
        auto result = m_pimpl->adjointWrenchMap.insert(newElement);
        assert(result.second);
        return &(result.first->second);
    }
}
