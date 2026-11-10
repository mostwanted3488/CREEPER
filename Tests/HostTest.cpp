// Offline test of CREEPER: renders a test signal through the processor and checks the behaviour.
// Build with -DCREEPER_BUILD_TESTS=ON, run: CreeperTest <output-dir>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "OK    " : "FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    CreeperProcessor p;
    const double sr = 48000.0;
    const int block = 480;
    p.setPlayConfigDetails (2, 2, sr, block);
    p.prepareToPlay (sr, block);
    auto* charge = p.apvts.getParameter ("charge");

    double phaseL = 0, phaseR = 0;
    juce::AudioBuffer<float> all (2, (int) (sr * 40));
    int written = 0;

    auto render = [&] (double seconds, std::function<float (double)> chargeAt, float& maxDiffToInput, float& peak,
                       std::vector<float>* rmsOut = nullptr)
    {
        maxDiffToInput = 0; peak = 0;
        const int blocks = (int) (seconds * sr / block);
        juce::AudioBuffer<float> buf (2, block), in (2, block);
        for (int b = 0; b < blocks; ++b)
        {
            charge->setValueNotifyingHost (chargeAt ((double) b * block / sr));
            for (int i = 0; i < block; ++i)
            {
                // a little "beat": tone gated at 4 Hz + steady pad
                const float l = 0.35f * (float) std::sin (phaseL) + 0.15f * (float) std::sin (phaseL * 0.5);
                const float r = 0.35f * (float) std::sin (phaseR) + 0.15f * (float) std::sin (phaseR * 0.5);
                phaseL += juce::MathConstants<double>::twoPi * 220.0 / sr;
                phaseR += juce::MathConstants<double>::twoPi * 277.0 / sr;
                in.setSample (0, i, l); in.setSample (1, i, r);
            }
            buf.makeCopyOf (in);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            float sq = 0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float o = buf.getSample (ch, i);
                    if (! std::isfinite (o)) { peak = 999; continue; }
                    maxDiffToInput = juce::jmax (maxDiffToInput, std::abs (o - in.getSample (ch, i)));
                    peak = juce::jmax (peak, std::abs (o));
                    sq += o * o;
                }
            if (rmsOut) rmsOut->push_back (std::sqrt (sq / (2 * block)));
            if (written + block <= all.getNumSamples())
            {
                all.copyFrom (0, written, buf, 0, 0, block);
                all.copyFrom (1, written, buf, 1, 0, block);
                written += block;
            }
        }
    };

    float diff, peak;

    render (1.0, [] (double) { return 0.0f; }, diff, peak);
    check (diff < 1.0e-6f, "0 %: output identical to input (diff " + juce::String (diff, 8) + ")");

    std::vector<float> rms;
    render (9.6, [] (double t) { return (float) juce::jmin (0.96, t / 10.0); }, diff, peak, &rms);
    check (peak < 0.83f && peak > 0.05f, "build-up 0 -> 96 %: peak " + juce::String (peak, 3) + " (limiter ceiling ~0.79)");
    {
        // stutter: in the last second (charge ~86-96 %) the level must jump up and down many times
        int swings = 0; bool lowState = false;
        float mx = 0; for (size_t i = rms.size() - 100; i < rms.size(); ++i) mx = juce::jmax (mx, rms[i]);
        for (size_t i = rms.size() - 100; i < rms.size(); ++i)
        {
            const bool lo = rms[i] < mx * 0.45f;
            if (lo != lowState) { ++swings; lowState = lo; }
        }
        check (swings >= 8, "tempo-synced stutter active near the top (" + juce::String (swings) + " level swings / s)");
    }

    rms.clear();
    render (4.0, [] (double) { return 1.0f; }, diff, peak, &rms);
    {
        float early = 0; for (int i = 5; i < 25; ++i) early = juce::jmax (early, rms[(size_t) i]);
        float late = 0;  for (size_t i = rms.size() - 20; i < rms.size(); ++i) late = juce::jmax (late, rms[i]);
        check (early > 0.003f, "100 %: reverb tail rings after the cut (rms " + juce::String (early, 4) + ")");
        check (late < early * 0.2f, "100 %: tail dies out (rms " + juce::String (late, 5) + ")");
    }

    p.triggerImpact();
    render (2.0, [] (double) { return 1.0f; }, diff, peak);
    check (peak > 0.2f && peak < 0.83f, "impact sound plays, limited (peak " + juce::String (peak, 3) + ")");

    p.apvts.getParameter ("impact")->setValueNotifyingHost (0.0f);
    p.triggerImpact();
    render (1.0, [] (double) { return 1.0f; }, diff, peak);
    check (peak < 0.05f, "IMPACT OFF: no impact (peak " + juce::String (peak, 4) + ")");

    render (8.0, [] (double) { return 0.0f; }, diff, peak);
    render (0.5, [] (double) { return 0.0f; }, diff, peak);
    check (diff < 1.0e-5f, "back to 0 %: transparent again after the tails (diff " + juce::String (diff, 7) + ")");

    // write the whole render
    {
        outDir.createDirectory();
        auto f = outDir.getChildFile ("creeper_test_render.wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
        {
            os.release();
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            writer->writeFromAudioSampleBuffer (all, 0, written);
        }
    }

    // GUI snapshots at a few charge values
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (800, 800);
        const float shots[] = { 0.0f, 0.55f, 0.9f };
        for (auto c : shots)
        {
            charge->setValueNotifyingHost (c);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("gui_" + juce::String (juce::roundToInt (c * 100)) + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
        }
        charge->setValueNotifyingHost (1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (350);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("gui_boom.png");
        f.deleteFile();
        juce::FileOutputStream fos (f);
        juce::PNGImageFormat().writeImageToStream (img, fos);
        check (img.isValid(), "editor renders");
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
