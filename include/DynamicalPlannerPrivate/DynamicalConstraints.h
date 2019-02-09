/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */
#ifndef DPLANNER_DYNAMICALCONSTRAINTS_H
#define DPLANNER_DYNAMICALCONSTRAINTS_H

#include <iDynTree/DynamicalSystem.h>
#include <iDynTree/Core/VectorDynSize.h>
#include <iDynTree/Core/Position.h>
#include <DynamicalPlannerPrivate/VariablesLabeller.h>
#include <DynamicalPlannerPrivate/TimelySharedKinDynComputations.h>
#include <DynamicalPlannerPrivate/HyperbolicTangent.h>
#include <iDynTree/SparsityStructure.h>
#include <memory>

namespace DynamicalPlanner {
    namespace Private {
        class DynamicalConstraints;
    }
}

class DynamicalPlanner::Private::DynamicalConstraints : public iDynTree::optimalcontrol::DynamicalSystem {

    class Implementation;
    std::unique_ptr<Implementation> m_pimpl;

public:

    DynamicalConstraints(const VariablesLabeller& stateVariables, const VariablesLabeller& controlVariables, std::shared_ptr<TimelySharedKinDynComputations> timelySharedKinDyn, const DynamicalPlanner::Private::HyperbolicTangent &planarVelocityActivation);

    ~DynamicalConstraints() override;

    virtual bool dynamics(const iDynTree::VectorDynSize& state, double time,
                          iDynTree::VectorDynSize& stateDynamics) override;

    virtual bool dynamicsStateFirstDerivative(const iDynTree::VectorDynSize& state, double time,
                                              iDynTree::MatrixDynSize& dynamicsDerivative) override;

    virtual bool dynamicsControlFirstDerivative(const iDynTree::VectorDynSize& state, double time,
                                                iDynTree::MatrixDynSize& dynamicsDerivative) override;

    virtual bool dynamicsStateFirstDerivativeSparsity(iDynTree::optimalcontrol::SparsityStructure& stateSparsity) override;

    virtual bool dynamicsControlFirstDerivativeSparsity(iDynTree::optimalcontrol::SparsityStructure& controlSparsity) override;

    virtual bool dynamicsSecondPartialDerivativeWRTState(double time,
                                                         const iDynTree::VectorDynSize& state,
                                                         const iDynTree::VectorDynSize& lambda,
                                                         iDynTree::MatrixDynSize& partialDerivative) override;

    virtual bool dynamicsSecondPartialDerivativeWRTControl(double time,
                                                           const iDynTree::VectorDynSize& state,
                                                           const iDynTree::VectorDynSize& lambda,
                                                           iDynTree::MatrixDynSize& partialDerivative) override;

    virtual bool dynamicsSecondPartialDerivativeWRTStateControl(double time,
                                                                const iDynTree::VectorDynSize& state,
                                                                const iDynTree::VectorDynSize& lambda,
                                                                iDynTree::MatrixDynSize& partialDerivative) override;

};

#endif // DPLANNER_DYNAMICALCONSTRAINTS_H
