/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */

#include <DynamicalPlannerPrivate/Constraints/ContactFrictionConstraint.h>
#include <iDynTree/Core/MatrixDynSize.h>

using namespace DynamicalPlanner::Private;

class ContactFrictionConstraint::Implementation {
public:
    VariablesLabeller stateVariables, controlVariables;

    std::string footName;
    size_t contactIndex;
    double frictionCoefficient;

    iDynTree::IndexRange forcePointRange;

    iDynTree::Vector3 pointForce;

    iDynTree::optimalcontrol::SparsityStructure stateJacobianSparsity, controlJacobianSparsity;
    iDynTree::optimalcontrol::SparsityStructure stateHessianSparsity, controlHessianSparsity, mixedHessianSparsity;
};



ContactFrictionConstraint::ContactFrictionConstraint(const VariablesLabeller &stateVariables, const VariablesLabeller &controlVariables,
                                                     const std::string &footName, size_t contactIndex)
    : iDynTree::optimalcontrol::Constraint (1, "ContactFriction" + footName + std::to_string(contactIndex))
    , m_pimpl(std::make_unique<Implementation>())
{
    m_pimpl->stateVariables = stateVariables;
    m_pimpl->controlVariables = controlVariables;

    m_pimpl->footName = footName;
    m_pimpl->contactIndex = contactIndex;

    m_pimpl->forcePointRange = stateVariables.getIndexRange(footName + "ForcePoint" + std::to_string(contactIndex));
    assert(m_pimpl->forcePointRange.isValid());

    m_pimpl->frictionCoefficient = 0.3;

    m_isLowerBounded = false;
    m_isUpperBounded = true;
    m_upperBound.zero();

    m_pimpl->stateJacobianSparsity.clear();
    m_pimpl->controlJacobianSparsity.clear();

    size_t fCol = static_cast<size_t>(m_pimpl->forcePointRange.offset);

    m_pimpl->stateJacobianSparsity.addDenseBlock(0, fCol, 1, 3);

    m_pimpl->stateHessianSparsity.addIdentityBlock(fCol, fCol, 3);
    m_pimpl->controlHessianSparsity.clear();
    m_pimpl->mixedHessianSparsity.clear();
}

ContactFrictionConstraint::~ContactFrictionConstraint()
{ }

bool ContactFrictionConstraint::setFrictionCoefficient(double frictionCoefficient)
{
    if (frictionCoefficient <= 0.0)
        return false;

    m_pimpl->frictionCoefficient = frictionCoefficient;
    return true;
}

bool ContactFrictionConstraint::evaluateConstraint(double, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &, iDynTree::VectorDynSize &constraint)
{
    m_pimpl->stateVariables = state;

    m_pimpl->pointForce = m_pimpl->stateVariables(m_pimpl->forcePointRange);

    constraint(0) = m_pimpl->pointForce(0) * m_pimpl->pointForce(0) + m_pimpl->pointForce(1) * m_pimpl->pointForce(1) -
            m_pimpl->frictionCoefficient * m_pimpl->frictionCoefficient * m_pimpl->pointForce(2) * m_pimpl->pointForce(2);

    return true;

}

bool ContactFrictionConstraint::constraintJacobianWRTState(double, const iDynTree::VectorDynSize &state, const iDynTree::VectorDynSize &, iDynTree::MatrixDynSize &jacobian)
{
    m_pimpl->stateVariables = state;

    m_pimpl->pointForce = m_pimpl->stateVariables(m_pimpl->forcePointRange);

    unsigned int col = static_cast<unsigned int>(m_pimpl->forcePointRange.offset);

    jacobian(0, col) = 2 * m_pimpl->pointForce(0);
    jacobian(0, col+1) = 2 * m_pimpl->pointForce(1);
    jacobian(0, col+2) = -2 * m_pimpl->frictionCoefficient * m_pimpl->frictionCoefficient * m_pimpl->pointForce(2);

    return true;
}

bool ContactFrictionConstraint::constraintJacobianWRTControl(double, const iDynTree::VectorDynSize &, const iDynTree::VectorDynSize &, iDynTree::MatrixDynSize &/*jacobian*/)
{
//    jacobian = m_pimpl->controlJacobianBuffer;

    return true;
}

size_t ContactFrictionConstraint::expectedStateSpaceSize() const
{
    return m_pimpl->stateVariables.size();
}

size_t ContactFrictionConstraint::expectedControlSpaceSize() const
{
    return m_pimpl->controlVariables.size();
}

bool ContactFrictionConstraint::constraintJacobianWRTStateSparsity(iDynTree::optimalcontrol::SparsityStructure &stateSparsity)
{
    stateSparsity = m_pimpl->stateJacobianSparsity;
    return true;
}

bool ContactFrictionConstraint::constraintJacobianWRTControlSparsity(iDynTree::optimalcontrol::SparsityStructure &controlSparsity)
{
    controlSparsity = m_pimpl->controlJacobianSparsity;
    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTState(double /*time*/, const iDynTree::VectorDynSize &/*state*/,
                                                                          const iDynTree::VectorDynSize &/*control*/,
                                                                          const iDynTree::VectorDynSize &lambda, iDynTree::MatrixDynSize &hessian)
{
    unsigned int col = static_cast<unsigned int>(m_pimpl->forcePointRange.offset);

    hessian(col, col) = 2.0 * lambda(0);
    hessian(col + 1, col + 1) = 2.0 * lambda(0);
    hessian(col + 2, col + 2) = -2.0 * m_pimpl->frictionCoefficient * m_pimpl->frictionCoefficient * lambda(0);

    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTControl(double /*time*/, const iDynTree::VectorDynSize &/*state*/,
                                                                            const iDynTree::VectorDynSize &/*control*/,
                                                                            const iDynTree::VectorDynSize &/*lambda*/,
                                                                            iDynTree::MatrixDynSize &/*hessian*/)
{
    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTStateControl(double /*time*/, const iDynTree::VectorDynSize &/*state*/,
                                                                                 const iDynTree::VectorDynSize &/*control*/,
                                                                                 const iDynTree::VectorDynSize &/*lambda*/,
                                                                                 iDynTree::MatrixDynSize &/*hessian*/)
{
    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTStateSparsity(iDynTree::optimalcontrol::SparsityStructure &stateSparsity)
{
    stateSparsity = m_pimpl->stateHessianSparsity;
    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTStateControlSparsity(iDynTree::optimalcontrol::SparsityStructure &stateControlSparsity)
{
    stateControlSparsity = m_pimpl->mixedHessianSparsity;
    return true;
}

bool ContactFrictionConstraint::constraintSecondPartialDerivativeWRTControlSparsity(iDynTree::optimalcontrol::SparsityStructure &controlSparsity)
{
    controlSparsity = m_pimpl->controlHessianSparsity;
    return true;
}
