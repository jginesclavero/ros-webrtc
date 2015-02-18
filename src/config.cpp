#include "config.h"

#include "util.h"


Config Config::get() {
    Config instance;
    ros::NodeHandle nh;

    // cameras
    XmlRpc::XmlRpcValue cameras_xml;
    if (nh.getParam(param_for("cameras/"), cameras_xml)) {
        for (XmlRpc::XmlRpcValue::iterator i = cameras_xml.begin(); i != cameras_xml.end(); i++) {
            VideoSource camera;
            if (_get(nh, ros::names::append(param_for("cameras/"), (*i).first), camera)) {
                instance.cameras.push_back(camera);
            }
        }
    } else {
        ROS_INFO_STREAM("missing 'cameras/' param");
    }

    // microphone
    _get(nh, param_for("microphone"), instance.microphone);

    // session constraints
    _get(nh, param_for(ros::names::append("session", "constraints")), instance.session_constraints);

    // ice_servers
    XmlRpc::XmlRpcValue ice_servers_xml;
    if (nh.getParam(param_for("ice_servers"), ice_servers_xml)) {
        for (XmlRpc::XmlRpcValue::iterator i = cameras_xml.begin(); i != cameras_xml.end(); i++) {
            webrtc::PeerConnectionInterface::IceServer ice_server;
            if (_get(nh, ros::names::append(param_for("ice_servers"), (*i).first), ice_server)) {
                instance.ice_servers.push_back(ice_server);
            }
        }
    } else {
        ROS_INFO_STREAM("missing 'ice_servers/' param");
    }

    // flush_frequency
    instance.flush_frequency = 10 * 60;  // 10 minutes
    if (nh.hasParam(param_for("flush_frequency"))) {
        if (!nh.getParam(param_for("flush_frequency"), instance.flush_frequency)) {
            ROS_INFO_STREAM("'flush_frequency' param type not int");
        }
    }

    return instance;
}

void Config::set() {
    ros::NodeHandle nh;

    // cameras
    for(size_t i = 0; i != cameras.size(); i++) {
        _set(nh, ros::names::append("webrtc/cameras", cameras[i].name), cameras[i]);
    }

    // microphone
    _set(nh, "microphone", microphone);

    // session constraints
    _set(nh, "webrtc/session/constraints", session_constraints);

    // ice servers
    size_t index = 0;
    for(webrtc::PeerConnectionInterface::IceServers::const_iterator i = ice_servers.begin(); i != ice_servers.end(); i++) {
        std::ostringstream oss;
        oss << index;
        _set(nh, ros::names::append("webrtc/ice_servers", oss.str()), (*i));
    }
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

void Config::_set(ros::NodeHandle& nh, const std::string& root, const VideoSource& value) {
    std::string scheme;
    switch (value.type) {
        case VideoSource::ROSType:
            scheme = "ros://";
            break;
    }
    nh.setParam(ros::names::append(root, "name"), scheme + value.name);
    nh.setParam(ros::names::append(root, "label"), value.label);
    _set(nh, ros::names::append(root, "constraints"), value.constraints);
    nh.setParam(ros::names::append(root, "publish"), value.publish);
}

bool Config::_get(ros::NodeHandle& nh, const std::string& root, AudioSource& value) {
    nh.getParam(ros::names::append(root, "label"), value.label);
    if (!_get(nh, ros::names::append(root, "constraints"), value.constraints)) {
        return false;
    }
    nh.getParam(ros::names::append(root, "publish"), value.publish);
    return true;
}

void Config::_set(ros::NodeHandle& nh, const std::string& root, const AudioSource& value) {
    nh.setParam(ros::names::append(root, "label"), value.label);
    _set(nh, ros::names::append(root, "constraints"), value.constraints);
    nh.setParam(ros::names::append(root, "publish"), value.publish);
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

void Config::_set(ros::NodeHandle& nh, const std::string& root, const MediaConstraints& value) {
    typedef std::map<std::string, std::string> Constraints;
    Constraints constraints;
    std::string key;

    key = ros::names::append(root, "mandatory");
    constraints.clear();
    for(MediaConstraints::Constraints::const_iterator i = value.mandatory().begin(); i != value.mandatory().begin(); i++) {
        constraints.insert(Constraints::value_type((*i).key, (*i).value));
    }
    nh.setParam(key, constraints);

    key = ros::names::append(root, "optional");
    constraints.clear();
    for(MediaConstraints::Constraints::const_iterator i = value.optional().begin(); i != value.optional().begin(); i++) {
        constraints.insert(Constraints::value_type((*i).key, (*i).value));
    }
    nh.setParam(key, constraints);
}

bool Config::_get(ros::NodeHandle& nh, const std::string& root, webrtc::PeerConnectionInterface::IceServer& value) {
    if (!nh.getParam(ros::names::append(root, "uri"), value.uri)) {
        return false;
    }
    nh.getParam(ros::names::append(root, "username"), value.username);
    nh.getParam(ros::names::append(root, "password"), value.password);
    return true;
}

void Config::_set(ros::NodeHandle& nh, const std::string& root, const webrtc::PeerConnectionInterface::IceServer& value) {
    nh.setParam(ros::names::append(root, "uri"), value.uri);
    nh.setParam(ros::names::append(root, "username"), value.username);
    nh.setParam(ros::names::append(root, "password"), value.password);
}
