//
// LibSourcey
// Copyright (C) 2005, Sourcey <http://sourcey.com>
//
// LibSourcey is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// LibSourcey is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//


#include "scy/media/mediafactory.h"
#include "scy/logger.h"


#if defined(HAVE_OPENCV) && defined(HAVE_RTAUDIO)


using std::endl;


namespace scy {
namespace av {

    
static Singleton<MediaFactory> singleton;
    

MediaFactory& MediaFactory::instance() 
{
    return *singleton.get();
}

    
void MediaFactory::shutdown()
{
    singleton.destroy();
}


MediaFactory::MediaFactory()
{    
    _devices = DeviceManagerFactory::create();
    _devices->initialize();
    //_devices->DevicesChanged += sdelegate(this, &MediaFactory::onDevicesChanged);
}


MediaFactory::~MediaFactory()
{    
    if (_devices) {
        //_devices->DevicesChanged -= sdelegate(this, &MediaFactory::onDevicesChanged);
        _devices->uninitialize();
        delete _devices;
    }
}


IDeviceManager& MediaFactory::devices() 
{ 
    Mutex::ScopedLock lock(_mutex);
    return *_devices; 
}


FormatRegistry& MediaFactory::formats() 
{ 
    Mutex::ScopedLock lock(_mutex);
    return _formats; 
}


void MediaFactory::loadVideoCaptures()
{
    DebugL << "Loading video captures" << endl;
    assert(Thread::mainID == Thread::currentID());

    // Initialize a VideoCapture object for each available device.
    // The video capture object will begin capturing frames when it's
    // reference count becomes positive.
    std::vector<Device> devs;
    devices().getVideoCaptureDevices(devs);
    for (std::size_t i = 0; i < devs.size(); ++i) {
        try {
            createVideoCapture(devs[0].id);
        } 
        catch (std::exception& exc) {
            ErrorL << "Cannot load video capture: "
                << devs[0].id << ": " << exc.what() << endl;
        }
    }
}


void MediaFactory::reloadFailedVideoCaptures()
{        
    DebugL << "Reloading failed video captures" << endl;
    assert(Thread::mainID == Thread::currentID());

    // Loop through captures and attempt to reopen any
    // that may have been unplugged
    auto videoCaptures = this->videoCaptures();
    for (auto& kv : videoCaptures) {            
        if (kv.second->error().any()) {
            TraceL << "Reloading capture " << kv.second->deviceId() 
                << ": " << kv.second->error() << endl;    
            try {
                kv.second->open();
                kv.second->start();        

                // Manually emit the capture loaded signal if the 
                // capture was successfully reloaded
                if (!kv.second->error().any()) {
                    VideoCaptureLoaded.emit(this, kv.second);
                }
            }
            catch (std::exception& exc) {
                ErrorL << "Capture initialization error: " << exc.what() << endl;
            }
        }
    }
}


std::map<int, VideoCapture::Ptr> MediaFactory::videoCaptures() const
{
    Mutex::ScopedLock lock(_mutex);
    return _videoCaptures;
}


void MediaFactory::unloadVideoCaptures()
{
    Mutex::ScopedLock lock(_mutex);
    _videoCaptures.clear();
}


VideoCapture::Ptr MediaFactory::createVideoCapture(int deviceId) //, unsigned flags
{    
    TraceL << "Creating video capture: " << deviceId << endl;

    if (deviceId < 0)
        throw std::runtime_error("Invalid video device ID");

    Mutex::ScopedLock lock(_mutex);

    auto it = _videoCaptures.find(deviceId);
    if (it != _videoCaptures.end())
        return it->second;

    auto capture = std::make_shared<VideoCapture>(deviceId);
    _videoCaptures[deviceId] = capture;    
    VideoCaptureLoaded.emit(this, capture);

    // Listen for errors.
    // Note: The capture is opened ad started in the constructor, 
    // so exceptions thrown during startup will not be handled
    // via this callback.
    capture->Error += sdelegate(this, &MediaFactory::onVideoCaptureError);
    return capture;
}

        
void MediaFactory::onVideoCaptureError(void* sender, const scy::Error& err)
{
    auto capture = reinterpret_cast<VideoCapture*>(sender);
    auto videoCaptures = this->videoCaptures();
    auto it = videoCaptures.find(capture->deviceId());
    if (it != videoCaptures.end()) {
        VideoCaptureError.emit(this, it->second);
    }
    else assert(0);
}


VideoCapture::Ptr MediaFactory::createFileCapture(const std::string& file)
{
    TraceL << "Create video file capture: " << file << endl;
    
    return std::make_shared<VideoCapture>(file);
}


AudioCapture::Ptr MediaFactory::createAudioCapture(int deviceId, int channels, int sampleRate, RtAudioFormat format)
{
    TraceL << "Create audio capture: " << deviceId << endl;
    if (deviceId < 0)
        throw std::runtime_error("Invalid audio device ID");

    return std::make_shared<AudioCapture>(deviceId, channels, sampleRate, format);
}


} } // namespace scy::av


#endif