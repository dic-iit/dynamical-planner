/*
* Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
* Authors: Stefano Dafarra
* CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
*
*/
#ifndef DPLANNER_QUATERNIONEXPRESSIONS_H
#define DPLANNER_QUATERNIONEXPRESSIONS_H

#include <levi/ForwardDeclarations.h>
#include <string>
#include <memory>

namespace DynamicalPlanner {
    namespace Private {

        levi::Expression NormalizedQuaternion(const levi::Variable& notNormalizedQuaternion);

        levi::Expression RotationExpression(const levi::Expression& normalizedQuaternion);

        levi::Expression E_Expression(const levi::Variable& normalizedQuaternion);

        levi::Expression G_Expression(const levi::Expression &quaternionVariable);

        levi::Expression BodyTwistFromQuaternionVelocity(const levi::Variable& linearVelocity, const levi::Variable& quaternionVelocity,
                                                         const levi::Expression &normalizedQuaternion, const std::string &name);
    }
}

#endif // DPLANNER_QUATERNIONEXPRESSIONS_H
