#include "config.h"

#include <algorithm>
#include <cctype>

#include "util.h"


Config Config::get(ros::NodeHandle& nh) {
    Config instance;

    // cameras
    XmlRpc::XmlRpcValue cameras_xml;
    if (nh.getParam("cameras/", cameras_xml)) {
        for (XmlRpc::XmlRpcValue::iterator i = cameras_xml.begin(); i != cameras_xml.end(); i++) {
            VideoSource camera;
            if (_get(nh, ros::names::append("cameras/", (*i).first), camera)) {
                instance.cameras.push_back(camera);
            }
        }
    } else {
        ROS_INFO("missing 'cameras/' param");
    }

    // microphone
    _get(nh, "microphone", instance.microphone);

    // session constraints
    _get(nh, ros::names::append("session", "constraints"), instance.session_constraints);

    // ice_servers
    XmlRpc::XmlRpcValue ice_servers_xml;
    if (nh.getParam("ice_servers", ice_servers_xml)) {
        for (size_t i = 0; i != ice_servers_xml.size(); i++) {
            webrtc::PeerConnectionInterface::IceServer ice_server;
            if (_get(nh, ice_servers_xml[0], ice_server)) {
                instance.ice_servers.push_back(ice_server);
            }
        }
    } else {
        ROS_INFO("missing 'ice_servers/' param");
    }

    // flush_frequency
    instance.flush_frequency = 10 * 60;  // 10 minutes
    if (nh.hasParam("flush_frequency")) {
        if (!nh.getParam("flush_frequency", instance.flush_frequency)) {
            ROS_WARN("'flush_frequency' param type not int");
        }
    }

    // trace_file
    instance.trace_file.clear();  // empty
    if (nh.hasParam("trace/file")) {
        if (!nh.getParam("trace/file", instance.trace_file)) {
            ROS_WARN("'trace/file' param type not string");
        }
    }

    // trace_mask
    instance.trace_mask = webrtc::TraceLevel::kTraceDefault;
    if (nh.hasParam("trace/filter")) {
        std::vector<std::string> trace_filters;
        std::string trace_filter;
        if (nh.getParam("trace/filter", trace_filters)) {
            instance.trace_mask = 0;
            for (size_t i = 0; i != trace_filters.size(); i++) {
                std::string lc = trace_filters[i];
                std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                TraceLevels::const_iterator iter = _trace_levels.find(lc);
                if (iter == _trace_levels.end()) {
                    ROS_WARN(
                        "'trace_filter[%zu]' value '%s' invalid, using default ...",
                        i, trace_filters[i].c_str()
                    );
                    instance.trace_mask = webrtc::TraceLevel::kTraceDefault;
                    break;
                }
                instance.trace_mask |= (*iter).second;
            }
        } else if (nh.getParam("trace/filter", trace_filter)) {
            std::string lc = trace_filter;
            std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
            TraceLevels::const_iterator iter = _trace_levels.find(lc);
            if (iter == _trace_levels.end()) {
                ROS_WARN(
                    "'trace/filter' value '%s' invalid, using default ...",
                    trace_filter.c_str()
                );
                instance.trace_mask = webrtc::TraceLevel::kTraceDefault;
            } else {
                instance.trace_mask |= (*iter).second;
            }
        } else {
            ROS_WARN("'trace/filter' should be string or string array");
        }
    }

    return instance;
}

void Config::set() {
    throw std::runtime_error("Not implemented.");
}

bool Config::_get(ros::NodeHandle& nh, const std::string& root, VideoSource& value) {
    if (!nh.getParam(ros::names::append(root, "name"), value.name)) {
        return false;
    }
    if (value.name.find("sys://") == 0) {
        value.name = value.name.substr(6);
        value.type = VideoSource::SystemType;
    } else if (value.name.find("ros://") == 0) {
        value.name = value.name.substr(6);
        value.type = VideoSource::ROSType;
    } else {
        value.type = VideoSource::SystemType;
    }
    nh.getParam(ros::names::append(root, "label"), value.label);
    if (!_get(nh, ros::names::append(root, "constraints"), value.constraints)) {
        return false;
    }
    nh.getParam(ros::names::append(root, "publish"), value.publish);
    return true;
}

bool Config::_get(ros::NodeHandle& nh, const std::string& root, AudioSource& value) {
    nh.getParam(ros::names::append(root, "label"), value.label);
    if (!_get(nh, ros::names::append(root, "constraints"), value.constraints)) {
        return false;
    }
    nh.getParam(ros::names::append(root, "publish"), value.publish);
    return true;
}

bool Config::_get(ros::NodeHandle& nh, const std::string& root, MediaConstraints& value) {
    typedef std::map<std::string, std::string> Constraints;
    Constraints constraints;
    std::string key;

    key = ros::names::append(root, "mandatory");
    if (nh.getParam(key, constraints)) {
        for (Constraints::iterator i = constraints.begin(); i != constraints.end(); i++) {
            value.mandatory().push_back(MediaConstraints::Constraint((*i).first, (*i).second));
        }
    }

    key = ros::names::append(root, "optional");
    if (nh.getParam(key, constraints)) {
        for (Constraints::iterator i = constraints.begin(); i != constraints.end(); i++) {
            value.optional().push_back(MediaConstraints::Constraint((*i).first, (*i).second));
        }
    }

    return true;
}

bool Config::_get(ros::NodeHandle& nh, XmlRpc::XmlRpcValue& root, webrtc::PeerConnectionInterface::IceServer& value) {
    if (!root.hasMember("uri")) {
        return false;
    }
    value.uri = std::string(root["uri"]);
    if (root.hasMember("username"))
        value.username = std::string(root["username"]);
    else
        value.username.clear();
    if (root.hasMember("password"))
        value.password = std::string(root["password"]);
    else
        value.password.clear();
    return true;
}

Config::TraceLevels Config::_trace_levels = {
    {"stateinfo", webrtc::TraceLevel::kTraceStateInfo},
    {"warning", webrtc::TraceLevel::kTraceWarning},
    {"error", webrtc::TraceLevel::kTraceError},
    {"critical", webrtc::TraceLevel::kTraceCritical},
    {"apicall", webrtc::TraceLevel::kTraceApiCall},
    {"default", webrtc::TraceLevel::kTraceDefault},
    {"modulecall", webrtc::TraceLevel::kTraceModuleCall},
    {"memory", webrtc::TraceLevel::kTraceMemory},
    {"timer", webrtc::TraceLevel::kTraceTimer},
    {"stream", webrtc::TraceLevel::kTraceStream},
    {"debug", webrtc::TraceLevel::kTraceDebug},
    {"info", webrtc::TraceLevel::kTraceInfo},
    {"terseinfo", webrtc::TraceLevel::kTraceTerseInfo},
    {"all", webrtc::TraceLevel::kTraceAll}
};
