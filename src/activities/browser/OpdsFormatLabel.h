#pragma once

#include <OpdsParser.h>

#include <string>
#include <vector>

const char* opdsBaseFormatLabel(const OpdsAcquisitionLink& acquisition);
std::string opdsFormatSelectionLabel(const OpdsAcquisitionLink& acquisition,
                                     const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                     const std::string& serverUrl);
std::vector<std::string> buildOpdsFormatSelectionLabels(const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                                        const std::string& serverUrl);
