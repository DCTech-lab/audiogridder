/*
 * Copyright (c) 2022 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#ifndef _PROCESSORCLIENT_HPP_
#define _PROCESSORCLIENT_HPP_

#include <JuceHeader.h>

#include "Utils.hpp"
#include "Message.hpp"
#include "ParameterValue.hpp"

namespace e47 {

class ProcessorClient : public Thread, public LogTag {
  public:
    ProcessorClient(const String& id, HandshakeRequest cfg)
        : Thread("ProcessorClient"), LogTag("processorclient"), m_port(getWorkerPort()), m_id(id), m_cfg(cfg) {}
    ~ProcessorClient() override { removeWorkerPort(m_port); }

    bool init();
    void shutdown();
    bool isOk();

    void run() override;

    std::function<void(int paramIdx, float val)> onParamValueChange;
    std::function<void(int paramIdx, bool gestureIsStarting)> onParamGestureChange;
    std::function<void(Message<Key>&)> onKeysFromSandbox;

    void handleMessage(std::shared_ptr<Message<Key>> msg);
    void handleMessage(std::shared_ptr<Message<ParameterValue>> msg);
    void handleMessage(std::shared_ptr<Message<ParameterGesture>> msg);

    bool load(const String& settings, String& err);
    void unload();

    bool isLoaded() const { return m_loaded; }

    const String getName();
    bool hasEditor();
    void showEditor(int x, int y);
    void hideEditor();
    bool supportsDoublePrecisionProcessing();
    bool isSuspended();
    double getTailLengthSeconds();
    void getStateInformation(juce::MemoryBlock&);
    void setStateInformation(const void*, int);
    bool checkBusesLayoutSupported(const AudioProcessor::BusesLayout&) { return true; }
    bool setBusesLayout(const AudioProcessor::BusesLayout&) { return true; }
    AudioProcessor::BusesLayout getBusesLayout() { return {}; }
    void setPlayHead(AudioPlayHead*);
    void enableAllBuses();
    const json& getParameters();
    int getNumPrograms();
    const String getProgramName(int);
    void setCurrentProgram(int);
    void suspendProcessing(bool);
    void suspendProcessingRemoteOnly(bool);
    int getTotalNumOutputChannels();
    int getLatencySamples();
    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages);
    void processBlock(AudioBuffer<double>& buffer, MidiBuffer& midiMessages);
    juce::Rectangle<int> getScreenBounds();
    void setParameterValue(int paramIdx, float value);
    float getParameterValue(int paramIdx);
    std::vector<Srv::ParameterValue> getAllParameterValues();

  private:
    int m_port;
    String m_id;
    HandshakeRequest m_cfg;
    ChildProcess m_process;
    std::unique_ptr<StreamingSocket> m_sockCmdIn, m_sockCmdOut, m_sockAudio;
    std::mutex m_mtx;
    std::shared_ptr<Meter> m_bytesOutMeter, m_bytesInMeter;

    bool m_loaded = false;
    String m_name;
    StringArray m_presets;
    json m_parameters;
    int m_latency = 0;
    bool m_hasEditor = false;
    bool m_scDisabled = false;
    bool m_supportsDoublePrecision = true;
    double m_tailSeconds = 0.0;
    int m_numOutputChannels = 0;
    AudioPlayHead* m_playhead = nullptr;
    std::atomic_bool m_suspended{false};
    String m_lastSettings;

    static std::unordered_set<int> m_workerPorts;
    static std::mutex m_workerPortsMtx;

    static int getWorkerPort();
    static void removeWorkerPort(int port);

    bool startSandbox();
    bool connectSandbox();

    template <typename T>
    void processBlockInternal(AudioBuffer<T>& buffer, MidiBuffer& midiMessages);
};

}  // namespace e47

#endif  // _PROCESSORCLIENT_HPP_