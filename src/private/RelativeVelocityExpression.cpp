/*
* Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
* Authors: Stefano Dafarra
* CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
*
*/

#include <levi/levi.h>
#include <DynamicalPlannerPrivate/Utilities/levi/RelativeVelocityExpression.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <cassert>


namespace DynamicalPlanner {
    namespace Private {
        class RelativeLeftVelocityEvaluable;
    }
}

using namespace DynamicalPlanner::Private;

class DynamicalPlanner::Private::RelativeLeftVelocityEvaluable : public levi::DefaultEvaluable {

    ExpressionsServer* m_expressionsServer;
    std::string m_baseFrameName, m_targetFrameName;
    iDynTree::FrameIndex m_baseFrame, m_targetFrame;
    levi::Expression m_thisExpression;

    levi::Variable m_jointsVariable, m_jointsVelocityVariable;

public:

    RelativeLeftVelocityEvaluable(ExpressionsServer* expressionsServer,
                                  const std::string& baseFrame,
                                  const std::string &targetFrame)
        : levi::DefaultEvaluable(6,1, targetFrame + "_V_" + baseFrame + "," + targetFrame)
          , m_expressionsServer(expressionsServer)
          , m_baseFrameName(baseFrame)
          , m_targetFrameName(targetFrame)
          , m_jointsVariable(expressionsServer->jointsPosition())
          , m_jointsVelocityVariable(expressionsServer->jointsVelocity())
    {
        const iDynTree::Model& model = expressionsServer->model();
        m_baseFrame = model.getFrameIndex(baseFrame);
        assert(m_baseFrame != iDynTree::FRAME_INVALID_INDEX);
        m_targetFrame = model.getFrameIndex(targetFrame);
        assert(m_targetFrame != iDynTree::FRAME_INVALID_INDEX);
        iDynTree::LinkIndex baseLink = model.getFrameLink(m_baseFrame);
        assert(baseLink != iDynTree::LINK_INVALID_INDEX);

        levi::Expression jacobian = expressionsServer->relativeLeftJacobian(baseFrame, targetFrame);
        m_thisExpression = jacobian * expressionsServer->jointsVelocity();

        addDependencies(m_jointsVariable, m_jointsVelocityVariable);
    }

    virtual const LEVI_DEFAULT_MATRIX_TYPE& evaluate() final {
        SharedKinDynComputationsPointer kinDyn = m_expressionsServer->currentKinDyn();

        iDynTree::Twist velocityInInertial = kinDyn->getFrameVel(m_expressionsServer->currentState(), m_targetFrame, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION);
        iDynTree::Twist baseVelocity = kinDyn->getFrameVel(m_expressionsServer->currentState(), m_baseFrame, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION);
        iDynTree::Transform relativeTransform = kinDyn->getRelativeTransform(m_expressionsServer->currentState(), m_targetFrame, m_baseFrame);

        m_evaluationBuffer = iDynTree::toEigen(velocityInInertial - relativeTransform*baseVelocity);

        return m_evaluationBuffer;
    }

    virtual void clearDerivativesCache() final {
        this->m_derivativeBuffer.clear();
        m_thisExpression.clearDerivativesCache();
    }

    virtual levi::Expression getNewColumnDerivative(Eigen::Index column, std::shared_ptr<levi::VariableBase> variable) final;
};

levi::Expression DynamicalPlanner::Private::RelativeLeftVelocityEvaluable::getNewColumnDerivative(Eigen::Index column, std::shared_ptr<levi::VariableBase> variable) {
    assert(column == 0);
    levi::unused(column);
    return m_thisExpression.getColumnDerivative(0, variable);
}

levi::Expression DynamicalPlanner::Private::RelativeLeftVelocityExpression(ExpressionsServer *expressionsServer, const std::string &baseFrame,
                                                                           const std::string &targetFrame)
{
    return levi::ExpressionComponent<RelativeLeftVelocityEvaluable>(expressionsServer, baseFrame, targetFrame);
}
