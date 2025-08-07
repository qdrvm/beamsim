#pragma once

#include <ns3/ptr.h>

namespace ns3 {
  class Node;

  void InstallLsquic(const Ptr<Node> &node);
}  // namespace ns3
