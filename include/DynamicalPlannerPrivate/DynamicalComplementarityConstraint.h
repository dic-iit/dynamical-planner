/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */
#ifndef DPLANNER_DYNAMICALCOMPLEMENTARITYCONSTRAINT_H
#define DPLANNER_DYNAMICALCOMPLEMENTARITYCONSTRAINT_H

#include <iDynTree/Constraint.h>
#include <DynamicalPlannerPrivate/VariablesLabeller.h>
#include <memory>
#include <string>

namespace DynamicalPlanner {
    namespace Private {
        class DynamicalComplementarityConstraint;
    }
}

class DynamicalPlanner::Private::DynamicalComplementarityConstraint : public iDynTree::optimalcontrol::Constraint {

    class Implementation;
    std::unique_ptr<Implementation> m_pimpl;

public:

    DynamicalComplementarityConstraint(const VariablesLabeller& stateVariables, const VariablesLabeller& controlVariables,
                                       const std::string &footName, size_t contactIndex, double dissipationGain);

    ~DynamicalComplementarityConstraint() override;

    virtual bool evaluateConstraint(double,
                                    const iDynTree::VectorDynSize& state,
                                    const iDynTree::VectorDynSize& control,
                                    iDynTree::VectorDynSize& constraint) override;

    virtual bool constraintJacobianWRTState(double,
                                            const iDynTree::VectorDynSize& state,
                                            const iDynTree::VectorDynSize& control,
                                            iDynTree::MatrixDynSize& jacobian) override;

    virtual bool constraintJacobianWRTControl(double,
                                              const iDynTree::VectorDynSize& state,
                                              const iDynTree::VectorDynSize& control,
                                              iDynTree::MatrixDynSize& jacobian) override;


    virtual size_t expectedStateSpaceSize() const override;

    virtual size_t expectedControlSpaceSize() const override;

};



#endif // DPLANNER_DYNAMICALCOMPLEMENTARITYCONSTRAINT_H