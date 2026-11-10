#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // one-pole smoothing coefficient for a time constant in seconds
    inline float coeff (double sr, double seconds)
    {
        return (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0e-5, seconds) * sr)));
    }

    const float intensityValues[] = { 0.65f, 1.0f, 1.25f };
    const int   maxStutterDiv[]   = { 4, 2, 1 };          // in 32nd notes: 1/8, 1/16, 1/32
}

juce::AudioProcessorValueTreeState::ParameterLayout CreeperProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "charge", 1 }, "Charge",
                                                       NormalisableRange<float> (0.0f, 1.0f, 0.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")
                                                           .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "impact", 1 }, "Impact", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tail", 1 }, "Tail",
                                                        StringArray { "Short", "Normal", "Long" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "intensity", 1 }, "Intensity",
                                                        StringArray { "Subtle", "Normal", "Hard" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "maxStutter", 1 }, "Max Stutter",
                                                        StringArray { "1/8", "1/16", "1/32" }, 2));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "stereo", 1 }, "Stereo", true));
    return layout;
}

CreeperProcessor::CreeperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CREEPER", createLayout())
{
    pCharge     = apvts.getRawParameterValue ("charge");
    pImpact     = apvts.getRawParameterValue ("impact");
    pTail       = apvts.getRawParameterValue ("tail");
    pIntensity  = apvts.getRawParameterValue ("intensity");
    pMaxStutter = apvts.getRawParameterValue ("maxStutter");
    pStereo     = apvts.getRawParameterValue ("stereo");
}

bool CreeperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CreeperProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    charge.reset (sr, 0.03);
    charge.setCurrentAndTargetValue (pCharge->load());

    juce::dsp::ProcessSpec stereoSpec { sr, (juce::uint32) maxChunk, 2 };
    juce::dsp::ProcessSpec monoSpec   { sr, (juce::uint32) maxChunk, 1 };

    lowCut.prepare (stereoSpec);
    lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    lowCut.setCutoffFrequency (20.0f);
    lowCut.setResonance (0.7071f);

    noiseBand.prepare (monoSpec);
    noiseBand.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    noiseBand.setCutoffFrequency (400.0f);
    noiseBand.setResonance (1.2f);

    haasHp.prepare (monoSpec);
    haasHp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    haasHp.setCutoffFrequency (250.0f);

    reverb.prepare (stereoSpec);
    reverb.reset();
    lastTail = -1;
    updateReverbParams ((int) pTail->load());

    limGain = 1.0f;

    haasDelay = juce::jmax (1, (int) std::round (0.012 * sr));
    haasBuf.assign ((size_t) juce::jmax (8, (int) (0.05 * sr)), 0.0f);
    haasWrite = 0;

    dryIn.setSize (2, maxChunk);
    wet.setSize (2, maxChunk);
    revBuf.setSize (2, maxChunk);

    gateGain = 1.0f; cutGain = 1.0f; sendCut = 1.0f; revRet = 0.9f;
    mix = pCharge->load() > 0.0005f ? 1.0f : 0.0f;
    sendCutCountdown = 0; wasCutting = false; activeHold = 0;
    freePos32 = 0.0; flashPhase = 0.0;
    impactPos = -1; impactOscPhase = 0.0; impactLp = 0.0f;
}

void CreeperProcessor::updateReverbParams (int tailIndex)
{
    if (tailIndex == lastTail) return;
    lastTail = tailIndex;
    juce::dsp::Reverb::Parameters p;
    const float room[] = { 0.55f, 0.82f, 0.93f };
    const float damp[] = { 0.45f, 0.40f, 0.30f };
    p.roomSize   = room[juce::jlimit (0, 2, tailIndex)];
    p.damping    = damp[juce::jlimit (0, 2, tailIndex)];
    p.wetLevel   = 0.33f;
    p.dryLevel   = 0.0f;
    p.width      = 1.0f;
    p.freezeMode = 0.0f;
    reverb.setParameters (p);
}

void CreeperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0) return;

    // ---- host transport -------------------------------------------------------------
    double bpm = 140.0, ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (30.0, 300.0, *b);
            if (auto q = pos->getPpqPosition()) { ppq = *q; hostPlaying = pos->getIsPlaying(); }
        }
    const double pos32PerSample = bpm / 60.0 * 8.0 / sr;   // 32nd notes per sample
    double pos32 = hostPlaying ? ppq * 8.0 : freePos32;

    // ---- settings ---------------------------------------------------------------------
    charge.setTargetValue (pCharge->load());
    const float I       = intensityValues[juce::jlimit (0, 2, (int) pIntensity->load())];
    const int   maxDiv  = maxStutterDiv[juce::jlimit (0, 2, (int) pMaxStutter->load())];
    const bool  stereoOn = pStereo->load() > 0.5f;
    updateReverbParams ((int) pTail->load());

    if (impactRequest.exchange (false) && pImpact->load() > 0.5f)
    {
        impactPos = 0; impactOscPhase = 0.0; impactLp = 0.0f;
    }

    const float kGateUp   = coeff (sr, 0.003);
    const float kGateDown = coeff (sr, 0.006);
    const float kGateIdle = coeff (sr, 0.010);
    const float kCutDown  = coeff (sr, 0.008);
    const float kCutUp    = coeff (sr, 0.020);
    const float kSend     = coeff (sr, 0.020);
    const float kRet      = coeff (sr, 0.050);
    const float kMix      = coeff (sr, 0.030);
    const int   holdLen   = (int) (6.0 * sr);
    const int   cutDelay  = (int) (0.03 * sr);
    const float kLimRel   = coeff (sr, 0.080);

    for (int start = 0; start < total; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, total - start);

        for (int ch = 0; ch < 2; ++ch)
            dryIn.copyFrom (ch, 0, buffer, juce::jmin (ch, numCh - 1), start, n);

        auto* inL = dryIn.getWritePointer (0);
        auto* inR = dryIn.getWritePointer (1);
        auto* wL = wet.getWritePointer (0);
        auto* wR = wet.getWritePointer (1);
        auto* rL = revBuf.getWritePointer (0);
        auto* rR = revBuf.getWritePointer (1);

        // per-16-sample control values
        float x = charge.getCurrentValue();
        float e = 0, driveK = 1, driveMix = 0, dryGain = 1, revSend = 0, low = 1, sideMul = 1, haasAmt = 0;
        float noiseAmp = 0, retTarget = 0.9f;
        int gateDiv = 0;
        bool cutting = false;

        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                charge.skip (juce::jmin (16, n - i));
                x = charge.getCurrentValue();
                const float e0 = std::pow (x, 1.6f);
                e = juce::jmin (1.15f, e0 * I);
                const float e1 = juce::jmin (1.0f, e);

                lowCut.setCutoffFrequency (juce::jmin ((float) (sr * 0.45), 20.0f * std::pow (45.0f, e)));
                lowCut.setResonance (0.7071f + e * 7.0f);
                driveK = 1.0f + e * 5.0f;
                driveMix = e1;
                dryGain = 1.0f - e1 * 0.35f;
                revSend = e * 0.55f;

                gateDiv = x < 0.45f ? 0 : juce::jmax (maxDiv, x < 0.62f ? 8 : x < 0.77f ? 4 : x < 0.89f ? 2 : 1);
                low = gateDiv ? 1.0f - juce::jmin (0.95f, (0.45f + (x - 0.45f) / 0.52f * 0.47f) * juce::jmin (1.05f, I)) : 1.0f;

                cutting = x >= 0.97f;
                const float w = (stereoOn && ! cutting) ? e1 : 0.0f;
                sideMul = 1.0f + w * 0.9f;
                haasAmt = w * 0.35f;

                // noise riser, blinking with the creeper (same clock as the GUI)
                const float rate = x < 0.08f ? 0.0f : 0.7f + std::pow ((x - 0.08f) / 0.92f, 1.6f) * 10.0f;
                flashPhase += rate * 16.0 / sr;
                flashPhase -= std::floor (flashPhase);
                const bool white = rate > 0.0f && flashPhase < 0.42;
                noiseAmp = juce::jmin (1.2f, std::pow (x, 1.8f) * I) * 0.16f * (white ? 1.3f : 0.75f);
                noiseBand.setCutoffFrequency (juce::jmin ((float) (sr * 0.45), 300.0f * std::pow (30.0f, e1)));

                if (cutting && ! wasCutting) sendCutCountdown = cutDelay;   // throw the last 30 ms into the reverb
                wasCutting = cutting;
                retTarget = cutting ? 1.5f : 0.9f;

                if (x > 0.0005f || impactPos >= 0) activeHold = holdLen;
            }

            // --- low cut + drive
            float l = lowCut.processSample (0, inL[i]);
            float r = lowCut.processSample (1, inR[i]);
            const float tk = std::tanh (driveK);
            l += driveMix * (std::tanh (l * driveK) / tk - l);
            r += driveMix * (std::tanh (r * driveK) / tk - r);

            // --- tempo-synced stutter gate
            if (gateDiv > 0)
            {
                const double ph = std::fmod (pos32, (double) gateDiv) / (double) gateDiv;
                const float target = ph < 0.55 ? 1.0f : low;
                gateGain += (target - gateGain) * (target > gateGain ? kGateUp : kGateDown);
            }
            else
                gateGain += (1.0f - gateGain) * kGateIdle;
            pos32 += pos32PerSample;
            l *= gateGain; r *= gateGain;

            // --- stereo widener (mid/side + Haas copy of the mid, low end kept mono)
            const float m = 0.5f * (l + r);
            const float s = 0.5f * (l - r);
            haasBuf[(size_t) haasWrite] = haasHp.processSample (0, m);
            int rd = haasWrite - haasDelay;
            if (rd < 0) rd += (int) haasBuf.size();
            const float hz = haasBuf[(size_t) rd];
            if (++haasWrite >= (int) haasBuf.size()) haasWrite = 0;
            const float s2 = s * sideMul + hz * haasAmt;

            // --- noise riser
            const float nz = noiseBand.processSample (0, nextNoise()) * noiseAmp;

            // --- pre-drop cut and reverb feed
            cutGain += ((cutting ? 0.0f : 1.0f) - cutGain) * (cutting ? kCutDown : kCutUp);
            if (sendCutCountdown > 0) --sendCutCountdown;
            const float sendTarget = (cutting && sendCutCountdown == 0) ? 0.0f : 1.0f;
            sendCut += (sendTarget - sendCut) * kSend;
            revRet  += (retTarget - revRet) * kRet;

            rL[i] = (l + nz) * revSend * sendCut;
            rR[i] = (r + nz) * revSend * sendCut;
            wL[i] = ((m + s2) * dryGain + nz) * cutGain;
            wR[i] = ((m - s2) * dryGain + nz) * cutGain;
        }

        // --- reverb (wet only), tail rings out on its own during the cut
        {
            juce::dsp::AudioBlock<float> rb (revBuf);
            auto sub = rb.getSubBlock (0, (size_t) n);
            reverb.process (juce::dsp::ProcessContextReplacing<float> (sub));
        }

        // --- add reverb return + impact
        for (int i = 0; i < n; ++i)
        {
            wL[i] += rL[i] * revRet;
            wR[i] += rR[i] * revRet;

            if (impactPos >= 0)
            {
                const double t = impactPos / sr;
                const double f = 120.0 * std::pow (30.0 / 120.0, juce::jmin (1.0, t / 1.1));
                impactOscPhase += twoPi * f / sr;
                if (impactOscPhase > twoPi) impactOscPhase -= twoPi;
                const float subAmp = 0.8f * (float) std::pow (0.001 / 0.8, juce::jmin (1.0, t / 1.4));
                const float sub = (float) std::sin (impactOscPhase) * subAmp;
                const double cutoff = 5000.0 * std::pow (150.0 / 5000.0, juce::jmin (1.0, t / 1.5));
                const float a = (float) (1.0 - std::exp (-twoPi * cutoff / sr));
                const float env = (float) std::pow (juce::jmax (0.0, 1.0 - t / 1.8), 4.0);
                impactLp += (nextNoise() * env - impactLp) * a;
                const float blast = impactLp * 0.5f;
                wL[i] += sub + blast;
                wR[i] += sub + blast;
                if (++impactPos > (int) (1.8 * sr)) impactPos = -1;
            }
        }

        // --- brickwall peak limiter: nothing leaves above -2 dBFS (no make-up gain)
        for (int i = 0; i < n; ++i)
        {
            const float pk = juce::jmax (std::abs (wL[i]), std::abs (wR[i]));
            const float target = pk > limCeiling ? limCeiling / pk : 1.0f;
            limGain = target < limGain ? target : limGain + (target - limGain) * kLimRel;
            wL[i] *= limGain; wR[i] *= limGain;
        }

        // --- transparent at rest: crossfade back to the untouched input once everything is quiet
        for (int i = 0; i < n; ++i)
        {
            if (activeHold > 0) --activeHold;
            mix += ((activeHold > 0 ? 1.0f : 0.0f) - mix) * kMix;
            const float ol = inL[i] + mix * (wL[i] - inL[i]);
            const float orr = inR[i] + mix * (wR[i] - inR[i]);
            wL[i] = ol; wR[i] = orr;
            levelAcc += 0.5f * (ol * ol + orr * orr);
        }
        levelCount += n;

        if (numCh == 1)
        {
            auto* o = buffer.getWritePointer (0, start);
            for (int i = 0; i < n; ++i) o[i] = 0.5f * (wL[i] + wR[i]);
        }
        else
        {
            buffer.copyFrom (0, start, wet, 0, 0, n);
            buffer.copyFrom (1, start, wet, 1, 0, n);
        }
    }

    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, total);

    freePos32 = hostPlaying ? 0.0 : pos32;

    if (levelCount > (int) (sr * 0.02))
    {
        outputLevel.store (juce::jmin (1.0f, std::sqrt (levelAcc / (float) levelCount) * 3.2f));
        levelAcc = 0.0f; levelCount = 0;
    }
}

void CreeperProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CreeperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CreeperProcessor::createEditor()
{
    return new CreeperEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CreeperProcessor();
}
