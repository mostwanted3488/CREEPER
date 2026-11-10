#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// The window is drawn in a 1000 x 1000 logical space and scaled to the editor size.
class CreeperEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit CreeperEditor (CreeperProcessor&);
    ~CreeperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class State { idle, fuse, boom, gone };

    struct Res { const char* data = nullptr; int size = 0; };
    struct Speck { juce::Rectangle<float> r; bool core; };
    struct Dust { std::vector<Speck> specks; juce::Point<float> c; };
    struct Particle { juce::Point<float> p; juce::Point<float> v; float life; bool bright; };
    struct Seg { juce::String paramId; float value; juce::Rectangle<float> r; };

    void timerCallback() override;
    void buildArt();
    void explode (bool fromUser);
    void respawn();
    void setCharge (float newValue);   // inside a gesture
    juce::Image frame (const Res&) const;
    juce::Point<float> toLogical (juce::Point<float> p) const;
    bool overKnob (juce::Point<float> logical) const;

    void drawTNT (juce::Graphics&) const;
    void drawDust (juce::Graphics&);
    void drawSettings (juce::Graphics&);
    void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height,
                    juce::Justification, juce::Colour colour = juce::Colours::white, int shadow = 3) const;
    void drawButton (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool on) const;

    CreeperProcessor& proc;
    juce::RangedAudioParameter* chargeParam = nullptr;

    // assets
    std::vector<Res> fuseFrames, boomFrames;
    juce::Image background;
    juce::Typeface::Ptr mcFace;

    // pixel art
    struct Px { juce::Rectangle<float> r; juce::Colour c; };
    std::vector<Px> tntPixels;
    std::vector<Dust> dust;
    std::vector<Particle> particles;

    // animation state
    State state = State::idle;
    float v = 0.0f, swell = 0.0f, env = 0.0f, shake = 0.0f;
    double flashPh = 0.0, lastMs = 0.0, boomT0 = 0.0, spawnT = -1.0e9;
    double hopT = -1.0e9, nextHop = 0.0, nextLook = 0.0;
    float lookDir = 0.0f, lookTarget = 0.0f;
    bool white = false;
    int frameIndex = 0;
    bool boomFrame = false;
    juce::AffineTransform creeperXf;
    float creeperOpacity = 1.0f, shadowScale = 1.0f, shadowTx = 0.0f, shadowAlpha = 1.0f;
    float craterAlpha = 0.0f, flashAlpha = 0.0f, glowAlpha = 0.0f;
    float worldX = 0.0f, worldY = 0.0f, worldRot = 0.0f;
    float tntJx = 0.0f, tntJy = 0.0f;
    juce::Random rnd;

    // interaction
    bool dragging = false, settingsOpen = false;
    float dragStartY = 0.0f, dragStartV = 0.0f;
    double lastUserTouchMs = -1.0e9;
    std::vector<Seg> segs;
    juce::Rectangle<float> optionsRect { 30.0f, 30.0f, 170.0f, 50.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CreeperEditor)
};
