/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 * Authors: Stefano Dafarra
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 *
 */
#ifndef DPLANNER_LOGGER_H
#define DPLANNER_LOGGER_H

#include <DynamicalPlanner/State.h>
#include <DynamicalPlanner/Control.h>
#include <memory>
#include <vector>
#include <string>

namespace DynamicalPlanner {
    class Logger;
}

class DynamicalPlanner::Logger {
public:

    static void saveSolutionVectorsToFile(const std::string& matFileName, const std::vector<DynamicalPlanner::State>& states = {},
                                          const std::vector<DynamicalPlanner::Control>& controls = {});
};

#endif // DPLANNERLOGGER_H
