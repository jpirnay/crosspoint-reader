#include "OpdsFormatLabel.h"

#include <algorithm>
#include <iterator>

#include "util/UrlUtils.h"

namespace {
bool hasDuplicateFormatKey(const OpdsAcquisitionLink& acquisition,
                           const std::vector<OpdsAcquisitionLink>& acquisitionLinks) {
  size_t sameFormatCount = 0;
  for (const auto& link : acquisitionLinks) {
    if (link.formatKey == acquisition.formatKey) {
      sameFormatCount++;
      if (sameFormatCount > 1) {
        return true;
      }
    }
  }

  return false;
}

std::string resolvedHostname(const OpdsAcquisitionLink& acquisition, const std::string& serverUrl) {
  const std::string resolvedUrl =
      acquisition.href.rfind("http", 0) == 0 ? acquisition.href : UrlUtils::buildUrl(serverUrl, acquisition.href);
  return UrlUtils::extractHostname(resolvedUrl);
}

std::vector<std::string> resolvedHostnames(const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                           const std::string& serverUrl) {
  std::vector<std::string> hostnames;
  hostnames.reserve(acquisitionLinks.size());
  std::transform(acquisitionLinks.begin(), acquisitionLinks.end(), std::back_inserter(hostnames),
                 [&serverUrl](const OpdsAcquisitionLink& link) { return resolvedHostname(link, serverUrl); });
  return hostnames;
}

std::string buildDuplicateAwareLabel(const OpdsAcquisitionLink& acquisition,
                                     const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                     const std::vector<std::string>& hostnames, const size_t currentIndex) {
  std::string label = opdsBaseFormatLabel(acquisition);
  if (!hasDuplicateFormatKey(acquisition, acquisitionLinks)) {
    return label;
  }

  const std::string& hostname = hostnames[currentIndex];
  if (hostname.empty()) {
    return label;
  }

  label.reserve(label.size() + hostname.size() + 8);
  label += " - ";
  label += hostname;

  size_t duplicateCount = 0;
  size_t duplicateIndex = 0;
  for (size_t i = 0; i < acquisitionLinks.size(); i++) {
    if (acquisitionLinks[i].formatKey == acquisition.formatKey && hostnames[i] == hostname) {
      duplicateCount++;
      if (i <= currentIndex) {
        duplicateIndex++;
      }
    }
  }

  if (duplicateCount > 1) {
    label += " (";
    label += std::to_string(duplicateIndex);
    label += ")";
  }

  return label;
}
}  // namespace

const char* opdsBaseFormatLabel(const OpdsAcquisitionLink& acquisition) {
  if (acquisition.formatKey == "kepub") {
    return "KEPUB";
  }
  if (acquisition.formatKey == "epub") {
    return "EPUB";
  }
  if (acquisition.formatKey == "txt") {
    return "TXT";
  }
  if (acquisition.formatKey == "md") {
    return "MD";
  }
  if (acquisition.formatKey == "xtc") {
    return "XTC";
  }
  if (acquisition.formatKey == "xtch") {
    return "XTCH";
  }

  return "";
}

std::string opdsFormatSelectionLabel(const OpdsAcquisitionLink& acquisition,
                                     const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                     const std::string& serverUrl) {
  const auto hostnames = resolvedHostnames(acquisitionLinks, serverUrl);
  for (size_t i = 0; i < acquisitionLinks.size(); i++) {
    if (acquisitionLinks[i].href == acquisition.href && acquisitionLinks[i].formatKey == acquisition.formatKey) {
      return buildDuplicateAwareLabel(acquisition, acquisitionLinks, hostnames, i);
    }
  }

  return opdsBaseFormatLabel(acquisition);
}

std::vector<std::string> buildOpdsFormatSelectionLabels(const std::vector<OpdsAcquisitionLink>& acquisitionLinks,
                                                        const std::string& serverUrl) {
  const auto hostnames = resolvedHostnames(acquisitionLinks, serverUrl);
  std::vector<std::string> labels;
  labels.reserve(acquisitionLinks.size());
  for (size_t i = 0; i < acquisitionLinks.size(); i++) {
    labels.push_back(buildDuplicateAwareLabel(acquisitionLinks[i], acquisitionLinks, hostnames, i));
  }
  return labels;
}
