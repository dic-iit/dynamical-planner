/*
* Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
* Authors: Stefano Dafarra
* CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
*
*/

#include <levi/levi.h>
#include <iDynTree/Model/Model.h>
#include <iDynTree/Model/Traversal.h>
#include <iDynTree/Core/EigenHelpers.h>
#include <DynamicalPlannerPrivate/Utilities/levi/RelativeJacobianExpression.h>
#include <cassert>
#include <vector>

namespace DynamicalPlanner {
    namespace Private {
        class RelativeLeftJacobianEvaluable;
    }
}

using namespace DynamicalPlanner::Private;

class DynamicalPlanner::Private::RelativeLeftJacobianEvaluable : public levi::DefaultEvaluable {

    ExpressionsServer* m_expressionsServer;
    iDynTree::MatrixDynSize m_jacobian;
    iDynTree::FrameIndex m_baseFrame, m_targetFrame;
    levi::Variable m_jointsVariable;

    std::vector<levi::Expression> m_columns;

public:

    RelativeLeftJacobianEvaluable(ExpressionsServer* expressionsServer,
                                  const std::string& baseFrame,
                                  const std::string &targetFrame)
        : levi::DefaultEvaluable(6, expressionsServer->jointsPosition().rows(), baseFrame + "_J_" + targetFrame)
          , m_expressionsServer(expressionsServer)
          , m_jointsVariable(expressionsServer->jointsPosition())
    {
        const iDynTree::Model& model = expressionsServer->model();
        m_baseFrame = model.getFrameIndex(baseFrame);
        assert(m_baseFrame != iDynTree::FRAME_INVALID_INDEX);
        m_targetFrame = model.getFrameIndex(targetFrame);
        assert(m_targetFrame != iDynTree::FRAME_INVALID_INDEX);
        iDynTree::LinkIndex baseLink = model.getFrameLink(m_baseFrame);
        assert(baseLink != iDynTree::LINK_INVALID_INDEX);

        iDynTree::Traversal traversal;

        bool ok = model.computeFullTreeTraversal(traversal, baseLink);
        assert(ok);

        m_columns.resize(static_cast<size_t>(expressionsServer->jointsPosition().rows()), levi::Null(6,1));

        iDynTree::IJointConstPtr jointPtr;
        size_t jointIndex;
        iDynTree::LinkIndex childLink, parentLink;
        iDynTree::LinkIndex visitedLink = model.getFrameLink(m_targetFrame);

        while (visitedLink != baseLink) {
            jointPtr = traversal.getParentJointFromLinkIndex(visitedLink);
            jointIndex = static_cast<size_t>(jointPtr->getIndex());

            childLink =  traversal.getChildLinkIndexFromJointIndex(model, jointPtr->getIndex());
            parentLink = traversal.getParentLinkIndexFromJointIndex(model, jointPtr->getIndex());

            m_columns[jointIndex] = m_expressionsServer->adjointTransform(targetFrame, model.getLinkName(childLink)) *
                m_expressionsServer->motionSubSpaceVector(jointPtr->getIndex(), parentLink, childLink);

            visitedLink = traversal.getParentLinkFromLinkIndex(visitedLink)->getIndex();
        }

        addDependencies(m_jointsVariable);

    }

    virtual levi::ColumnExpression col(Eigen::Index col) final {
        return m_columns[static_cast<size_t>(col)];
    }

    virtual levi::ScalarExpression element(Eigen::Index row, Eigen::Index col) final {
        return m_columns[static_cast<size_t>(col)](row, 0);
    }

    virtual const LEVI_DEFAULT_MATRIX_TYPE& evaluate() final {

        SharedKinDynComputationsPointer kinDyn = m_expressionsServer->currentKinDyn();

        bool ok = kinDyn->getRelativeJacobian(m_expressionsServer->currentState(), m_baseFrame, m_targetFrame, m_jacobian, iDynTree::FrameVelocityRepresentation::BODY_FIXED_REPRESENTATION);
        assert(ok);

        m_evaluationBuffer = iDynTree::toEigen(m_jacobian);

        return m_evaluationBuffer;
    }

    virtual void clearDerivativesCache() final {
        this->m_derivativeBuffer.clear();
        for (auto& expression : m_columns) {
            expression.clearDerivativesCache();
        }
    }

    virtual levi::ExpressionComponent<typename levi::DefaultEvaluable::derivative_evaluable>
    getNewColumnDerivative(Eigen::Index column, std::shared_ptr<levi::VariableBase> variable) final;
};

levi::ExpressionComponent<typename levi::DefaultEvaluable::derivative_evaluable>
RelativeLeftJacobianEvaluable::getNewColumnDerivative(Eigen::Index column, std::shared_ptr<levi::VariableBase> variable)
{
    if (variable->variableName() == m_jointsVariable.name()) { //m_expression contains the jointsVariable specified in the constructor

        return m_columns[static_cast<size_t>(column)].getColumnDerivative(0, variable);

    } else {
        return levi::Null(6, variable->dimension());
    }

}


levi::Expression DynamicalPlanner::Private::RelativeLeftJacobianExpression(ExpressionsServer *expressionsServer, const std::string &baseFrame,
                                                                           const std::string &targetFrame)
{
    return levi::ExpressionComponent<RelativeLeftJacobianEvaluable>(expressionsServer, baseFrame, targetFrame);
}


