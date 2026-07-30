#include "rtc/realtime/cluster_bus.hpp"

#include "rtc/utils/env.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::realtime {

std::string make_node_id() {
    // Prefer an operator-supplied identity: in Kubernetes this is the pod name
    // (see deploy/k8s/deployment.yaml), which makes cluster-bus logs and the
    // service.instance.id trace attribute directly traceable to a pod.
    if (auto configured = utils::get_env("RTC_NODE_ID");
        configured.has_value() && !configured->empty()) {
        return *configured;
    }
    if (auto hostname = utils::get_env("HOSTNAME"); hostname.has_value() && !hostname->empty()) {
        return *hostname;
    }
    return "node-" + utils::generate_hex_token(6);
}

}  // namespace rtc::realtime
