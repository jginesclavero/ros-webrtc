#include "video_capture.h"

#include <cv_bridge/cv_bridge.h>
#include <opencv/cv.hpp>
#include <sensor_msgs/Image.h>
#include <webrtc/system_wrappers/interface/ref_count.h>

// VideoCaptureModule

ROSVideoCaptureModule::ROSVideoCaptureModule(int32_t id) :
    VideoCaptureImpl(id),
    _capture_cs(webrtc::CriticalSectionWrapper::CreateCriticalSection()),
    _capturing(false),
    _capture_thd(NULL) {
    _nh.setCallbackQueue(&_image_q);
}

ROSVideoCaptureModule::~ROSVideoCaptureModule() {
    StopCapture();
    _subscriber.shutdown();
    _nh.setCallbackQueue(NULL);
    if (_capture_cs) {
        delete _capture_cs;
        _capture_cs = NULL;
    }
}

int32_t ROSVideoCaptureModule::init(const char* deviceUniqueIdUTF8) {
    // find topic by unique id
    ros::master::V_TopicInfo topics;
    if (!ros::master::getTopics(topics)) {
        ROS_WARN_STREAM("failed to get topics");
        return -1;
    }
    size_t index = -1;
    for (size_t i = 0; i < topics.size(); i++) {
        const ros::master::TopicInfo& topic = topics[i];
        if (topic.datatype == "sensor_msgs/Image" && topic.name == deviceUniqueIdUTF8) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        ROS_ERROR("no matching device  for '%s' found", deviceUniqueIdUTF8);
        return -1;
    }
    _topic = topics[index].name;

    // subscribe to the topic
    _subscriber = _nh.subscribe(_topic, 1, &ROSVideoCaptureModule::_image_callback, this);

    return 0;
}

bool ROSVideoCaptureModule::_capture_thread(void* obj) {
    return static_cast<ROSVideoCaptureModule*>(obj)->_capture_poll();
}

bool ROSVideoCaptureModule::_capture_poll() {
    {
        // lock
        webrtc::CriticalSectionScoped cs(_capture_cs);
        if (!_capturing)
            return false;

        // poll
        ros::CallbackQueue::CallOneResult result = ros::CallbackQueue::TryAgain;
        while (result == ros::CallbackQueue::TryAgain) {
            // NOTE: handler is ROSVideoCaptureModule::_image_callback
            result = _image_q.callOne();
        }
        if (result != ros::CallbackQueue::Called)
            return true;
    }

    usleep(0);  // yield
    return true;
}

void ROSVideoCaptureModule::_image_callback(const sensor_msgs::ImageConstPtr& msg) {
    // force to bgr8
    cv::Mat bgr = cv_bridge::toCvShare(msg, "bgr8")->image;

    // convert to I420
    cv::Mat yuv(bgr.rows, bgr.cols, CV_8UC4);
    cv::cvtColor(bgr, yuv, CV_BGR2YUV_I420);

    // adjust caps
    _capability.width = bgr.cols;
    _capability.height = bgr.rows;
    _capability.rawType = webrtc::kVideoI420;

    // send it along
    IncomingFrame(yuv.data, yuv.rows * yuv.step, _capability, msg->header.stamp.toNSec());
}

webrtc::VideoCaptureModule* ROSVideoCaptureModule::Create(const int32_t id, const char* deviceUniqueIdUTF8) {
    webrtc::RefCountImpl<ROSVideoCaptureModule>* obj = new webrtc::RefCountImpl<ROSVideoCaptureModule>(id);
    if (!obj || obj->init(deviceUniqueIdUTF8) != 0) {
        delete obj;
        obj = NULL;
    }
    return obj;
}

ROSVideoCaptureModule::DeviceInfo* ROSVideoCaptureModule::CreateDeviceInfo(const int32_t id) {
    return new ROSVideoCaptureDeviceInfo(id);
}

int32_t ROSVideoCaptureModule::StartCapture(const webrtc::VideoCaptureCapability& capability) {
    if (_capturing) {
        if (capability.width == _capability.width &&
            capability.height == _capability.height &&
            capability.rawType == _capability.rawType) {
            // already started w/ same profile
            return 0;
        } else {
            // profile changes, so stop
            StopCapture();
        }
    }

    webrtc::CriticalSectionScoped cs(_capture_cs);

    //start capture thread;
    if (_capture_thd == NULL) {
        _capture_thd = webrtc::ThreadWrapper::CreateThread(
            ROSVideoCaptureModule::_capture_thread, this, webrtc::kHighPriority
        );
        if (_capture_thd == NULL) {
            return -1;
        }
        unsigned int id;
        _capture_thd->Start(id);
    }

    // done
    _capability = capability;
    _capturing = true;

    return 0;
}

int32_t ROSVideoCaptureModule::StopCapture() {
    if (_capture_thd != NULL) {
        if (_capture_thd->Stop()) {
            delete _capture_thd;
            _capture_thd = NULL;
        } else {
            ROS_ERROR("could not stop capture thread, leaking it ...");
            assert(false);
            _capture_thd = NULL;
        }
    }

    {
        webrtc::CriticalSectionScoped cs(_capture_cs);
        if (_capturing) {
            _capturing = false;
            _image_q.clear();
        }
    }

    return 0;
}

bool ROSVideoCaptureModule::CaptureStarted() {
    return _capturing;
}

int32_t ROSVideoCaptureModule::CaptureSettings(webrtc::VideoCaptureCapability& settings) {
    settings = _capability;
    return 0;
}

// ROSVideoCaptureDeviceInfo

ROSVideoCaptureDeviceInfo::ROSVideoCaptureDeviceInfo(
    const int32_t id
    ) : DeviceInfoImpl(id) {
}

ROSVideoCaptureDeviceInfo::~ROSVideoCaptureDeviceInfo() {
}

uint32_t ROSVideoCaptureDeviceInfo::NumberOfDevices() {
    // count all published topics w/ date-type sensor_msgs/Image
    uint32_t count = 0;
    ros::master::V_TopicInfo topics;
    if (!ros::master::getTopics(topics)) {
        ROS_WARN_STREAM("failed to get topics");
        return count;
    }
    std::sort(topics.begin(), topics.end(), topic_name_less_than());
    for (size_t i = 0; i < topics.size(); i++) {
        const ros::master::TopicInfo& topic = topics[i];
        if (topic.datatype == "sensor_msgs/Image") {
            count++;
        }
    }
    return count;
}

int32_t ROSVideoCaptureDeviceInfo::GetDeviceName(
    uint32_t deviceNumber,
    char* deviceNameUTF8,
    uint32_t deviceNameLength,
    char* deviceUniqueIdUTF8,
    uint32_t deviceUniqueIdUTF8Length,
    char* productUniqueIdUTF8,
    uint32_t productUniqueIdUTF8Length
    ) {
    // find topic by index (brittle)
    ros::master::V_TopicInfo topics;
    if (!ros::master::getTopics(topics)) {
        ROS_WARN_STREAM("failed to get topics");
        return -1;
    }
    std::sort(topics.begin(), topics.end(), topic_name_less_than());
    size_t i = 0, number = 0, index = 0;
    for (; i < topics.size(); i++) {
        const ros::master::TopicInfo& topic = topics[i];
        if (topic.datatype == "sensor_msgs/Image") {
            if (number == deviceNumber)
                break;
            number++;
        }
    }
    if (number != deviceNumber) {
        return -1;
    }
    const ros::master::TopicInfo& topic = topics[i];

    // output topic name to id
    if (deviceNameLength >= topic.name.size() + 1) {
        memcpy(deviceNameUTF8, topic.name.c_str(), topic.name.size() + 1);
    } else {
        ROS_ERROR("buffer passed is too small");
        return -1;
    }

    // output topic name unique id
    if (deviceUniqueIdUTF8Length >= topic.name.size() + 1) {
        memcpy(deviceUniqueIdUTF8, topic.name.c_str(), topic.name.size() + 1);
    } else {
        ROS_ERROR("buffer passed is too small");
        return -1;
    }

    return 0;
}

int32_t ROSVideoCaptureDeviceInfo::CreateCapabilityMap (const char* deviceUniqueIdUTF8) {
    // find topic by unique id
    ros::master::V_TopicInfo topics;
    if (!ros::master::getTopics(topics)) {
        ROS_WARN_STREAM("failed to get topics");
        return -1;
    }
    size_t index = -1;
    for (size_t i = 0; i < topics.size(); i++) {
        const ros::master::TopicInfo& topic = topics[i];
        if (topic.datatype == "sensor_msgs/Image" && topic.name == deviceUniqueIdUTF8) {
            index = i;
            break;
        }
    }
    if (index == -1) {
        ROS_ERROR_STREAM("no matching device  for '" << deviceUniqueIdUTF8 << "'found");
        return -1;
    }
    const ros::master::TopicInfo& topic = topics[index];

    webrtc::RawVideoType formats[] {
//        webrtc::kVideoI420,
//        webrtc::kVideoYV12,
//        webrtc::kVideoYUY2,
//        webrtc::kVideoUYVY,
//        webrtc::kVideoIYUV,
//        webrtc::kVideoARGB,
        webrtc::kVideoRGB24,
//        webrtc::kVideoRGB565,
//        webrtc::kVideoARGB4444,
//        webrtc::kVideoARGB1555,
//        webrtc::kVideoMJPEG,
//        webrtc::kVideoNV12,
//        webrtc::kVideoNV21,
//        webrtc::kVideoBGRA,
    };

    unsigned int sizes[][2] = {
        { 128, 96 },
        { 160, 120 },
        { 176, 144 },
        { 320, 240 },
        { 352, 288 },
        { 640, 480 },
        { 704, 576 },
        { 800, 600 },
        { 960, 720 },
        { 1280, 720 },
        { 1024, 768 },
        { 1440, 1080 },
        { 1920, 1080 }
    };

    for (size_t format_idx = 0; format_idx < sizeof(formats) / sizeof(formats[0]); format_idx++) {
        for (size_t size_idx = 0; size_idx < sizeof(sizes) / sizeof(sizes[0]); size_idx++) {
            webrtc::VideoCaptureCapability cap;

            cap.codecType = webrtc::kVideoCodecUnknown;
            cap.interlaced = false;
            cap.expectedCaptureDelay = 120; // TODO: what's this?
            cap.maxFPS = 30; // TODO: or 15?
            cap.rawType = formats[format_idx];
            cap.width = sizes[size_idx][0];
            cap.height = sizes[size_idx][1];

            _captureCapabilities.push_back(cap);
        }
    }

   // record topic whose capabilities are in _captureCapabilities
   _lastUsedDeviceNameLength = strlen(deviceUniqueIdUTF8);
   _lastUsedDeviceName = (char*) realloc(_lastUsedDeviceName, _lastUsedDeviceNameLength + 1);
   memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8, _lastUsedDeviceNameLength + 1);

   ROS_INFO_STREAM(
        "loaded " << _captureCapabilities.size() << " capabilities for '" << deviceUniqueIdUTF8 << "'";
   );
   return _captureCapabilities.size();
}

int32_t ROSVideoCaptureDeviceInfo::DisplayCaptureSettingsDialogBox(
    const char* deviceUniqueIdUTF8,
    const char* dialogTitleUTF8,
    void* parentWindow,
    uint32_t positionX,
    uint32_t positionY
    ) {
    return -1;  // not supported
}

int32_t ROSVideoCaptureDeviceInfo::Init() {
    return 0;  // do nothing
}
