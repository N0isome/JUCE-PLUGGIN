#include "PluginProcessor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <thread>
namespace
{
void check(bool b, const char* message)
{ if (!b) { std::cerr << "SYSTEM AUDIO FAILED: " << message << '\n'; std::exit(1); } }
void closed(LoopCaptureAudioProcessor& p)
{
    for (int i = 0; i < 1000; ++i)
    {
        const auto s = p.getSnapshot().state;
        if (s == capture::State::ready || s == capture::State::error) return;
        juce::Thread::sleep(2);
    }
    check(false, "stop deadline");
}
}
void runSystemAudioTests()
{
    using namespace systemaudio;
    float l[5] {}, r[5] {};
    const std::uint8_t pcm8[] { 0, 255, 128, 128 };
    check(decode(pcm8, 2, {2,8,2,false}, l,r,false) && l[0] == -1 && r[0] == 127.0f/128 && l[1] == 0, "unsigned PCM8");
    const std::uint8_t pcm16[] {0,128,255,127, 0,64,0,192};
    check(decode(pcm16,2,{2,16,4,false},l,r,false) && l[0]==-1 && r[0]==32767.0f/32768 && l[1]==0.5f && r[1]==-0.5f, "PCM16 stereo");
    const std::uint8_t pcm24[] {0,0,128,255,255,127, 0,0,64,0,0,192};
    check(decode(pcm24,2,{2,24,6,false},l,r,false) && l[0]==-1 && std::abs(r[0]-1)<0.000001f && l[1]==0.5f && r[1]==-0.5f, "PCM24 sign extension");
    const std::uint8_t pcm32[] {0,0,0,128,0,0,0,64};
    check(decode(pcm32,2,{1,32,4,false},l,r,false) && l[0]==-1 && l[1]==0.5f && r[1]==l[1], "PCM32 mono duplicate");
    const float floating[] {0.25f,-0.75f,99,99, std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),99,99};
    check(decode(floating,2,{4,32,16,true},l,r,false) && l[0]==0.25f && r[0]==-0.75f && l[1]==0 && r[1]==0, "float, front stereo, sanitise, stride");
    check(decode(nullptr,5,{2,24,6,false},l,r,true) && l[4]==0 && r[4]==0, "silent packet ignores pointer");
    check(!decode(nullptr,1,{2,16,4,false},l,r,false), "null live packet rejected");
    check(!decode(pcm8,1,{2,16,1,false},l,r,false), "invalid stride rejected");
    check(!decode(pcm8,1,{33,8,33,false},l,r,false), "excess channels rejected");
    check(!Format{2,std::numeric_limits<int>::max(),4,false}.valid(), "unknown format rejected without overflow");

#if JUCE_WINDOWS
    {
        Capture endpoint;
        check(endpoint.start("{JUCE-PLUGGIN-INVALID-DEVICE-FOR-TEST}"), "start async invalid endpoint");
        for(int i=0;i<2500 && !endpoint.status().failed;++i) juce::Thread::sleep(2);
        check(endpoint.status().failed && !endpoint.ready(), "invalid/missing Windows endpoint reports failure");
        endpoint.stop();
    }
#endif
    {
        Capture capture; std::atomic<bool> done {false};
        std::thread consumer([&] {
            juce::AudioBuffer<float> output(2,127);
            while(!done.load()) capture.render(output,44100);
        });
        for(int i=0;i<100;++i)
        {
            capture.testBegin(i%2 ? 48000 : 96000);
            juce::AudioBuffer<float> packet(2,5000); packet.clear();
            check(capture.testPush(packet), "concurrent reconnect queue");
        }
        capture.requestStop(); done.store(true); consumer.join();
        check(!capture.ready(), "100 reconnects concurrent with callback");
    }

    for (double sourceRate : {8000.0,44100.0,48000.0,96000.0,384000.0})
        for (double hostRate : {8000.0,44100.0,48000.0,96000.0,192000.0})
        {
            Capture capture; capture.testBegin(sourceRate);
            juce::AudioBuffer<float> packet(2,static_cast<int>(sourceRate * 0.05));
            for(int i=0;i<packet.getNumSamples();++i) { packet.setSample(0,i,0.25f); packet.setSample(1,i,-0.125f); }
            check(capture.testPush(packet), "prime queue");
            juce::AudioBuffer<float> output(2,128);
            for(int block=0;block<50;++block)
            {
                capture.render(output,hostRate);
                for(int c=0;c<2;++c) for(int i=0;i<128;++i) check(std::isfinite(output.getSample(c,i)), "all rate combinations finite");
                if (block > 15 && (block + 1) * 128 / hostRate > 0.03)
                {
                    if (std::abs(output.getSample(0,127)-0.25f)>=0.003f || std::abs(output.getSample(1,127)+0.125f)>=0.003f)
                        std::cerr << "rates " << sourceRate << " -> " << hostRate << " block " << block << " samples " << output.getSample(0,127) << ',' << output.getSample(1,127) << '\n';
                    check(std::abs(output.getSample(0,127)-0.25f)<0.003f && std::abs(output.getSample(1,127)+0.125f)<0.003f, "resampling preserves stereo/DC after filter latency");
                }
                juce::AudioBuffer<float> next(2,static_cast<int>(std::ceil(128 * sourceRate / hostRate)));
                for(int i=0;i<next.getNumSamples();++i) { next.setSample(0,i,0.25f); next.setSample(1,i,-0.125f); }
                check(capture.testPush(next), "stream queue capacity");
            }
            capture.testFail(); output.applyGain(0.0f); capture.render(output,hostRate);
            check(capture.status().failed && output.getMagnitude(0,128)==0, "device failure clears output");
            capture.testBegin(sourceRate); capture.render(output,hostRate);
            check(output.getMagnitude(0,128)==0, "reconnect never reuses previous session samples");
        }

    // High-frequency content above the destination Nyquist must not alias strongly.
    {
        Capture capture; capture.testBegin(96000);
        juce::AudioBuffer<float> packet(2,20000);
        for(int i=0;i<20000;++i) for(int c=0;c<2;++c)
            packet.setSample(c,i,static_cast<float>(0.5*std::sin(juce::MathConstants<double>::twoPi*40000*i/96000)));
        check(capture.testPush(packet), "alias test input");
        juce::AudioBuffer<float> output(2,4096); capture.render(output,44100);
        check(output.getRMSLevel(0,1024,3072)<0.001f, "anti-alias rejection");
    }
    // Processor integration: loopback replaces the host input, applies gain,
    // records stereo at the host rate and cannot monitor back into its own input.
    {
        LoopCaptureAudioProcessor p; p.prepareToPlay(48000,256);
        auto& capture=p.testConnectSystem(48000);
        auto* monitor=p.parameters.getParameter("monitor"); monitor->setValueNotifyingHost(1);
        juce::AudioBuffer<float> packet(2,24000);
        for(int i=0;i<24000;++i) { packet.setSample(0,i,0.25f); packet.setSample(1,i,-0.125f); }
        check(capture.testPush(packet), "integration source");
        check(p.startRecording(), "system record starts");
        for(int i=0;i<1000 && p.getSnapshot().state!=capture::State::recording;++i) juce::Thread::sleep(2);
        check(p.getSnapshot().state==capture::State::recording,"record opening deadline");
        juce::AudioBuffer<float> output(2,256); juce::MidiBuffer midi;
        for(int block=0;block<30;++block)
        {
            for(int c=0;c<2;++c) for(int i=0;i<256;++i) output.setSample(c,i,0.9f);
            p.processBlock(output,midi);
            check(output.getMagnitude(0,256)==0,"loopback monitoring cannot feed back, including automation");
        }
        capture.testFail(); closed(p); // Stop even with no subsequent host callback.
        const auto measured = p.consumePeaks();
        const auto s=p.getSnapshot(); check(s.hasUsableFile && s.frames==30*256,"device loss closes continuous take");
        juce::WavAudioFormat wav; auto stream=s.file.createInputStream();
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(),true));
        check(reader && reader->numChannels==2 && reader->sampleRate==48000,"system WAV format");
        juce::AudioBuffer<float> tail(2,256); reader->read(&tail,0,256,reader->lengthInSamples-256,true,true);
        if (std::abs(tail.getSample(0,255)-0.25f)>=0.001f || std::abs(tail.getSample(1,255)+0.125f)>=0.001f)
            std::cerr << "WAV tail " << tail.getSample(0,255) << ',' << tail.getSample(1,255) << " rate " << reader->sampleRate << " frames " << reader->lengthInSamples << " peaks " << measured[0] << ',' << measured[1] << " gain " << p.parameters.getRawParameterValue("inputGain")->load() << '\n';
        check(std::abs(tail.getSample(0,255)-0.25f)<0.001f && std::abs(tail.getSample(1,255)+0.125f)<0.001f,"WAV contains endpoint audio, not host input");
        reader.reset(); s.file.deleteFile();
        p.disconnectInput(); check(!p.startRecording(),"disconnected system cannot silently record DAW");
        check(p.connectInput(false),"switch back to DAW");
    }
    std::cout << "PASS: system PCM/float/silence, 25 rate pairs, anti-alias, stereo WAV, device failure/reconnect, feedback prevention\n";
}
