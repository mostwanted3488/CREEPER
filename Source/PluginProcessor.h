#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// CREEPER — one-knob build-up riser by BUYORBEALOSER
//
// Charge 0 -> 100 %:
//   resonant low-cut sweep, light drive, tempo-synced stutter gate (1/4 -> 1/32),
//   stereo widening, reverb wash, noise riser.
//   97 -> 100 %: pre-drop cut — the dry track drops out, only the reverb tail rings out.
//   Brickwall limiter on the output. At 0 % (and once tails have died) the plugin is fully transparent.
//   The explosion sound ("impact") is only played when the knob is pushed to 100 % by hand in the
//   plugin window, never from host automation.

class CreeperProcessor : public juce::AudioProcessor
{
public:
    CreeperProcessor();
    ~CreeperProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CREEPER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // Called by the editor when the user pushes the knob to 100 % by hand.
    void triggerImpact() { impactRequest.store (true); }

    // Read by the editor (creature reacts to the music).
    std::atomic<float> outputLevel { 0.0f };

private:
    static constexpr int maxChunk = 1024;

    void updateReverbParams (int tailIndex);
    float nextNoise() { return rng.nextFloat() * 2.0f - 1.0f; }

    // parameters
    std::atomic<float>* pCharge = nullptr;
    std::atomic<float>* pImpact = nullptr;
    std::atomic<float>* pTail = nullptr;
    std::atomic<float>* pIntensity = nullptr;
    std::atomic<float>* pMaxStutter = nullptr;
    std::atomic<float>* pStereo = nullptr;

    double sr = 44100.0;
    juce::SmoothedValue<float> charge;

    // DSP blocks
    juce::dsp::StateVariableTPTFilter<float> lowCut;   // stereo
    juce::dsp::StateVariableTPTFilter<float> noiseBand; // mono
    juce::dsp::StateVariableTPTFilter<float> haasHp;    // mono
    juce::dsp::Reverb reverb;
    juce::Random rng;

    std::vector<float> haasBuf;
    int haasWrite = 0, haasDelay = 0;

    juce::AudioBuffer<float> dryIn, wet, revBuf;

    // smoothed gains
    static constexpr float limCeiling = 0.7943f;   // -2 dBFS
    float limGain = 1.0f;
    float gateGain = 1.0f, cutGain = 1.0f, sendCut = 1.0f, revRet = 0.9f, mix = 0.0f;
    int sendCutCountdown = 0;
    bool wasCutting = false;
    int activeHold = 0;
    int lastTail = -1;

    // transport for the stutter gate (in 32nd notes)
    double freePos32 = 0.0;
    double flashPhase = 0.0;

    // impact synth
    std::atomic<bool> impactRequest { false };
    int impactPos = -1;
    double impactOscPhase = 0.0;
    float impactLp = 0.0f;

    float levelAcc = 0.0f;
    int levelCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CreeperProcessor)
};
