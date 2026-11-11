#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr float kFrame = 1000.0f / 1080.0f;            // video square (1080) -> logical (1000)
    constexpr float cssToLogical = 1000.0f / 820.0f;        // preview pixels -> logical units
    const juce::Point<float> creeperOrigin { 500.0f, 660.0f };
    const juce::Point<float> knobCentre { 850.0f, 812.0f };
    constexpr float knobScale = 0.95f;                      // TNT art units -> logical
    constexpr int   numFuse = 97, numBoom = 97, numDust = 84;
    constexpr float A0 = -135.0f, A1 = 135.0f;

    // which fuse frames of the source video are white (flash) — measured from the video
    const char* whiteMask = "0000000000000000000001111111100000000111111111000000001111111100000000011111111000000000111111110";

    juce::Colour hex (juce::uint32 rgb, float alpha = 1.0f) { return juce::Colour (rgb).withAlpha (alpha); }

    int nearestFrame (bool wantWhite, float s)
    {
        int best = 0; float bestD = 1.0e9f;
        for (int i = 0; i < numFuse; ++i)
            if ((whiteMask[i] == '1') == wantWhite)
            {
                const float d = std::abs ((float) i - s);
                if (d < bestD) { bestD = d; best = i; }
            }
        return best;
    }

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

CreeperEditor::CreeperEditor (CreeperProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    chargeParam = proc.apvts.getParameter ("charge");

    for (int i = 0; i < numFuse; ++i)
    {
        Res r; r.data = BinaryData::getNamedResource (juce::String::formatted ("f%02d_png", i).toRawUTF8(), r.size);
        fuseFrames.push_back (r);
    }
    for (int i = 0; i < numBoom; ++i)
    {
        Res r; r.data = BinaryData::getNamedResource (juce::String::formatted ("b%02d_png", i).toRawUTF8(), r.size);
        boomFrames.push_back (r);
    }
    background = juce::ImageCache::getFromMemory (BinaryData::bg_jpg, BinaryData::bg_jpgSize);
    mcFace = juce::Typeface::createSystemTypefaceFor (BinaryData::Minecraft_ttf, BinaryData::Minecraft_ttfSize);

    buildArt();

    // warm the image cache for the first fuse frames
    for (int i = 0; i < 4; ++i) frame (fuseFrames[(size_t) i]);

    setOpaque (true);
    setResizable (true, true);
    setResizeLimits (420, 420, 1000, 1000);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1.0);
    setSize (640, 640);

    v = swell = chargeParam->getValue();
    state = v > 0.0f ? State::fuse : State::idle;
    lastMs = nowMs();
    nextHop = lastMs + 2500.0;
    nextLook = lastMs + 1500.0;
    startTimerHz (60);
}

CreeperEditor::~CreeperEditor()
{
    stopTimer();
}

juce::Image CreeperEditor::frame (const Res& r) const
{
    if (r.data == nullptr) return {};
    return juce::ImageCache::getFromMemory (r.data, r.size);
}

// ------------------------------------------------------------------------------------------
// pixel art: TNT block (16x16, 6 units per pixel) and the redstone dust trail
void CreeperEditor::buildArt()
{
    const float PX = 6.0f, o = -8.0f * PX;
    tntPixels.clear();
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            juce::uint32 c;
            const bool band = y >= 5 && y <= 10;
            if (band) c = (y == 5 || y == 10) ? 0xcfc7b0 : 0xece6d3;
            else
            {
                const bool stick = (x / 2) % 2 == 1;
                c = stick ? 0xb3281f : 0xd93a2b;
                if (y == 0 || y == 4 || y == 11) c = stick ? 0x8e1d17 : 0xb02c21;
                if (x % 2 == 0 && y % 4 == 2) c = 0xe8574a;
            }
            if (x == 0 || x == 15 || y == 0 || y == 15) c = band ? 0xa9a18c : 0x6e1611;
            tntPixels.push_back ({ { o + x * PX, o + y * PX, PX, PX }, hex (c) });
        }

    const char* T[] = { "111", "010", "010", "010", "010" };
    const char* N[] = { "1001", "1101", "1011", "1001", "1001" };
    int cx = 2;
    for (int letter = 0; letter < 3; ++letter)
    {
        const char** L = letter == 1 ? N : T;
        const int w = (int) std::strlen (L[0]);
        for (int ry = 0; ry < 5; ++ry)
            for (int rx = 0; rx < w; ++rx)
                if (L[ry][rx] == '1')
                    tntPixels.push_back ({ { o + (cx + rx) * PX, o + (6 + ry) * PX, PX, PX }, hex (0x1b1512) });
        cx += w + 1;
    }

    // redstone trail along the knob travel (same seed as the preview)
    juce::int64 seed = 7;
    auto rnd01 = [&seed] { seed = (seed * 16807) % 2147483647; return (float) seed / 2147483647.0f; };
    auto snap = [] (float q) { return std::round (q / 3.0f) * 3.0f; };
    dust.clear();
    for (int i = 0; i < numDust; ++i)
    {
        const float f = (float) i / (float) (numDust - 1);
        const float ang = juce::degreesToRadians (A0 + (A1 - A0) * f - 90.0f);
        Dust d; d.c = { std::cos (ang) * 80.0f, std::sin (ang) * 80.0f };
        struct S { float dx, dy, w, h; };
        std::vector<S> sp { { 0, 0, 6, 6 } };
        const int k = 1 + (int) std::floor (rnd01() * 3.0f);
        for (int j = 0; j < k; ++j)
        {
            const float dx = (rnd01() * 2 - 1) * 7, dy = (rnd01() * 2 - 1) * 7;
            const float w = rnd01() < 0.5f ? 3.0f : 4.0f, h = rnd01() < 0.5f ? 3.0f : 4.0f;
            sp.push_back ({ dx, dy, w, h });
        }
        for (size_t j = 0; j < sp.size(); ++j)
            d.specks.push_back ({ { snap (d.c.x + sp[j].dx) - sp[j].w / 2, snap (d.c.y + sp[j].dy) - sp[j].h / 2, sp[j].w, sp[j].h }, j == 0 });
        dust.push_back (std::move (d));
    }
}

// ------------------------------------------------------------------------------------------
void CreeperEditor::explode (bool fromUser)
{
    state = State::boom;
    boomT0 = nowMs();
    shake = 1.0f;
    flashAlpha = 1.0f;
    if (fromUser) proc.triggerImpact();   // explosion sound only when pushed by hand, never from automation
}

void CreeperEditor::respawn()
{
    state = v > 0.0f ? State::fuse : State::idle;
    spawnT = nowMs();
}

void CreeperEditor::timerCallback()
{
    const double now = nowMs();
    const float dt = (float) juce::jmin (0.05, (now - lastMs) / 1000.0);
    lastMs = now;
    const double t = now / 1000.0;

    v = chargeParam->getValue();

    // state machine (same as the preview)
    if (state == State::boom || state == State::gone)
    {
        if (state == State::gone && v < 0.9f) respawn();
    }
    else
    {
        state = v > 0.0f ? State::fuse : State::idle;
        if (v >= 0.999f) explode (dragging || now - lastUserTouchMs < 300.0);
    }

    const float lvl = proc.outputLevel.load();
    env = lvl > env ? env + (lvl - env) * 0.6f : env + (lvl - env) * 0.08f;

    float sx = 1, sy = 1, rot = 0, tx = 0, ty = 0, op = 1;
    white = false;

    if (state == State::boom)
    {
        const int f = (int) std::floor ((now - boomT0) / 1000.0 * 30.0);
        boomFrame = true;
        frameIndex = f;
        if (f > numBoom - 1)
        {
            state = State::gone; frameIndex = -1;
            if (v < 0.9f) respawn();
        }
    }
    else if (state == State::gone)
    {
        frameIndex = -1;
    }

    if (state == State::idle || state == State::fuse)
    {
        boomFrame = false;
        swell += (v - swell) * juce::jmin (1.0f, dt * 10.0f);
        const float throb = swell > 0.1f ? (float) std::sin (t * (4.0 + swell * 22.0)) * swell * 5.0f : 0.0f;
        const float s = juce::jlimit (0.0f, (float) (numFuse - 1), swell * (numFuse - 1) + throb);
        const float rate = swell < 0.08f ? 0.0f : 0.7f + std::pow ((swell - 0.08f) / 0.92f, 1.6f) * 10.0f;
        flashPh += dt * rate;
        white = rate > 0 && std::fmod (flashPh, 1.0) < 0.42;
        frameIndex = nearestFrame (white, s);

        // breathing, faster and shorter when charged; squash on the music
        const float br = 1.6f + swell * 5.0f, ba = 0.018f * (1.0f - swell * 0.5f);
        sy = 1.0f + (float) std::sin (t * br) * ba;
        sx = 1.0f - (float) std::sin (t * br) * ba * 0.7f;
        sx *= 1.0f + env * 0.10f; sy *= 1.0f - env * 0.06f;

        // sway + looking around
        if (now > nextLook) { lookTarget = (rnd.nextFloat() * 2 - 1) * (1.0f - swell); nextLook = now + 1200.0 + rnd.nextFloat() * 2600.0; }
        lookDir += (lookTarget - lookDir) * juce::jmin (1.0f, dt * 3.0f);
        rot = (float) std::sin (t * 0.9) * 1.4f + lookDir * 2.5f;
        tx = lookDir * 6.0f;

        // little hops when calm, jitter when charged
        if (swell < 0.45f && now > nextHop) { hopT = now; nextHop = now + 2500.0 + rnd.nextFloat() * 4000.0; }
        const float hp = (float) ((now - hopT) / 320.0);
        if (hp >= 0 && hp < 1) { ty -= std::sin (hp * juce::MathConstants<float>::pi) * 14.0f; sy *= 1.0f + std::sin (hp * juce::MathConstants<float>::pi) * 0.04f; }
        if (swell > 0.45f)
        {
            const float j = (swell - 0.45f) / 0.55f;
            tx += (rnd.nextFloat() * 2 - 1) * j * 2.5f;
            ty += (rnd.nextFloat() * 2 - 1) * j * 1.5f;
            rot += (rnd.nextFloat() * 2 - 1) * j * 1.5f;
        }

        // respawn pop
        const float sp = (float) ((now - spawnT) / 420.0);
        if (sp < 1)
        {
            const float e = 1.0f - std::pow (1.0f - juce::jmax (0.0f, sp), 3.0f);
            const float b = std::sin (sp * juce::MathConstants<float>::pi);
            op = e;
            sx *= 0.6f + 0.4f * e + b * 0.12f;
            sy *= 0.4f + 0.6f * e + b * 0.18f;
        }
    }

    tx *= cssToLogical; ty *= cssToLogical;
    creeperXf = juce::AffineTransform::translation (-creeperOrigin.x, -creeperOrigin.y)
                    .scaled (sx, sy)
                    .rotated (juce::degreesToRadians (rot))
                    .translated (creeperOrigin.x + tx, creeperOrigin.y + ty);
    creeperOpacity = op;

    const bool alive = state == State::idle || state == State::fuse;
    shadowAlpha = alive ? op * (1.0f - juce::jmax (0.0f, -ty) / (30.0f * cssToLogical)) : 0.0f;
    shadowScale = sx * (1.0f + swell * 0.25f);
    shadowTx = tx;

    const bool craterOn = (state == State::boom && now - boomT0 > 120.0) || state == State::gone;
    craterAlpha += ((craterOn ? 1.0f : 0.0f) - craterAlpha) * juce::jmin (1.0f, dt * 2.5f);

    flashAlpha = juce::jmax (0.0f, flashAlpha - dt / 0.9f);
    glowAlpha = alive ? (white ? 0.15f + 0.4f * swell : env * 0.15f) : 0.0f;

    // shake: grows on the fuse, huge on the boom
    float amp = 0.0f;
    if (state == State::fuse && v > 0.6f) amp = std::pow ((v - 0.6f) / 0.4f, 2.0f) * 3.0f;
    if (state == State::boom || shake > 0.01f) { amp = juce::jmax (amp, 38.0f * shake); shake *= 0.94f; }
    if (amp > 0.05f)
    {
        worldX = (rnd.nextFloat() * 2 - 1) * amp * cssToLogical;
        worldY = (rnd.nextFloat() * 2 - 1) * amp * cssToLogical;
        worldRot = (rnd.nextFloat() * 2 - 1) * amp * 0.08f;
    }
    else worldX = worldY = worldRot = 0.0f;

    // TNT jitter
    const float tj = (state == State::fuse && v > 0.5f) ? (v - 0.5f) * 2.0f * 3.0f : (state == State::boom ? 6.0f * shake : 0.0f);
    tntJx = tj > 0 ? std::round ((rnd.nextFloat() * 2 - 1) * tj) : 0.0f;
    tntJy = tj > 0 ? std::round ((rnd.nextFloat() * 2 - 1) * tj) : 0.0f;

    // redstone particles rising from the head of the lit trail
    if (v > 0.01f && (state == State::fuse || state == State::boom) && rnd.nextFloat() < 0.25f + v * 0.6f)
    {
        const auto& h = dust[(size_t) juce::jlimit (0, numDust - 1, juce::roundToInt (v * (numDust - 1)))];
        particles.push_back ({ { std::round (h.c.x + (rnd.nextFloat() * 2 - 1) * 6), std::round (h.c.y + (rnd.nextFloat() * 2 - 1) * 6) },
                               { (rnd.nextFloat() * 2 - 1) * 0.25f, -(0.3f + rnd.nextFloat() * 0.7f) }, 1.0f, rnd.nextBool() });
    }
    for (auto it = particles.begin(); it != particles.end();)
    {
        it->life -= 0.025f;
        if (it->life <= 0) { it = particles.erase (it); continue; }
        it->p += it->v;
        ++it;
    }

    repaint();
}

// ------------------------------------------------------------------------------------------
void CreeperEditor::drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float height,
                               juce::Justification just, juce::Colour colour, int shadow) const
{
    juce::Font f = mcFace != nullptr ? juce::Font (juce::FontOptions (mcFace).withHeight (height))
                                     : juce::Font (juce::FontOptions (height));
    g.setFont (f);
    if (shadow > 0)
    {
        g.setColour (hex (0x3f3f3f));
        g.drawText (text, r.translated ((float) shadow, (float) shadow), just, false);
    }
    g.setColour (colour);
    g.drawText (text, r, just, false);
}

void CreeperEditor::drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on) const
{
    g.setColour (on ? hex (0x5b8731) : hex (0x000000, 0.66f));
    g.fillRect (r);
    g.setColour (hex (0xffffff, 0.13f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 3.0f);
    g.setColour (hex (0x000000, 0.5f));
    g.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth(), 3.0f);
    g.setColour (on ? hex (0x7cff4f) : hex (0xffffff, 0.25f));
    g.drawRect (r, 2.0f);
    drawLabel (g, text, r, 22.0f, juce::Justification::centred, juce::Colours::white, 2);
}

void CreeperEditor::drawTNT (juce::Graphics& g) const
{
    for (const auto& p : tntPixels)
    {
        g.setColour (p.c);
        g.fillRect (p.r);
    }
}

void CreeperEditor::drawDust (juce::Graphics& g)
{
    const int lit = v > 0.0f ? juce::roundToInt (v * numDust) : 0;
    const bool boom = state == State::boom;
    const auto litCore  = boom ? (rnd.nextBool() ? hex (0xff6a4a) : hex (0xff2414)) : white ? hex (0xff6b4f) : hex (0xff2a17);
    const auto litSpeck = boom ? litCore : white ? hex (0xff5a40) : hex (0xe01a0c);
    const float alpha = (white || boom) ? 1.0f : 0.82f + 0.18f * (float) std::sin (lastMs / 1000.0 * (3.0 + v * 14.0));

    for (int i = 0; i < numDust; ++i)
    {
        const bool on = i < lit;
        for (const auto& s : dust[(size_t) i].specks)
        {
            if (on)
            {
                // fake glow
                g.setColour (litCore.withAlpha (0.18f * alpha));
                g.fillEllipse (s.r.expanded (5.0f));
                g.setColour ((s.core ? litCore : litSpeck).withAlpha (alpha));
            }
            else
                g.setColour (s.core ? hex (0x6e130c) : hex (0x4a0a06));
            g.fillRect (s.r);
        }
    }

    for (const auto& p : particles)
    {
        const auto c = p.bright ? hex (0xff3a22) : hex (0xc8170b);
        g.setColour (c.withAlpha (p.life * 0.3f));
        g.fillEllipse (p.p.x - 3, p.p.y - 3, 9, 9);
        g.setColour (c.withAlpha (p.life));
        g.fillRect (p.p.x, p.p.y, 3.0f, 3.0f);
    }
}

void CreeperEditor::drawSettings (juce::Graphics& g)
{
    segs.clear();
    if (! settingsOpen) return;

    juce::Rectangle<float> panel (30.0f, 92.0f, 620.0f, 300.0f);
    g.setColour (hex (0x1e1e1e, 0.95f));
    g.fillRect (panel);
    g.setColour (hex (0x5a5a5a));
    g.fillRect (panel.getX() + 3, panel.getY() + 3, panel.getWidth() - 6, 3.0f);
    g.fillRect (panel.getX() + 3, panel.getY() + 3, 3.0f, panel.getHeight() - 6);
    g.setColour (hex (0x000000));
    g.drawRect (panel, 3.0f);

    drawLabel (g, "SETTINGS", { panel.getX() + 22, panel.getY() + 16, 300, 30 }, 26.0f, juce::Justification::centredLeft, hex (0xffff55), 2);

    struct Row { const char* label; const char* id; std::vector<std::pair<const char*, float>> opts; };
    const std::vector<Row> rows {
        { "IMPACT", "impact", { { "ON", 1 }, { "OFF", 0 } } },
        { "TAIL", "tail", { { "SHORT", 0 }, { "NORMAL", 1 }, { "LONG", 2 } } },
        { "INTENSITY", "intensity", { { "SUBTLE", 0 }, { "NORMAL", 1 }, { "HARD", 2 } } },
        { "MAX STUTTER", "maxStutter", { { "1/8", 0 }, { "1/16", 1 }, { "1/32", 2 } } },
        { "STEREO", "stereo", { { "ON", 1 }, { "OFF", 0 } } },
    };

    float y = panel.getY() + 62.0f;
    for (const auto& row : rows)
    {
        drawLabel (g, row.label, { panel.getX() + 22, y, 250, 40 }, 22.0f, juce::Justification::centredLeft, juce::Colours::white, 2);
        const float current = proc.apvts.getRawParameterValue (row.id)->load();
        float x = panel.getRight() - 22.0f;
        for (auto it = row.opts.rbegin(); it != row.opts.rend(); ++it)
        {
            const float w = 22.0f + 14.0f * (float) std::strlen (it->first);
            juce::Rectangle<float> r (x - w, y + 4, w, 34);
            const bool on = std::abs (current - it->second) < 0.5f;
            g.setColour (on ? hex (0x5b8731) : hex (0x3a3a3a));
            g.fillRect (r);
            g.setColour (hex (0xffffff, 0.19f));
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 2.0f);
            g.setColour (hex (0x000000, 0.55f));
            g.fillRect (r.getX(), r.getBottom() - 2.0f, r.getWidth(), 2.0f);
            drawLabel (g, it->first, r, 19.0f, juce::Justification::centred, juce::Colours::white, 2);
            segs.push_back ({ row.id, it->second, r });
            x -= w + 6.0f;
        }
        y += 46.0f;
    }
}

void CreeperEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const float scale = (float) getWidth() / 1000.0f;
    g.addTransform (juce::AffineTransform::scale (scale));
    g.reduceClipRegion (juce::Rectangle<int> (0, 0, 1000, 1000));
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);

    // whole world shakes
    g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (worldRot), 500.0f, 500.0f).translated (worldX, worldY));

    // background (slight parallax against the shake)
    if (background.isValid())
        g.drawImage (background, juce::Rectangle<float> (-30.0f - worldX * 0.6f, -30.0f - worldY * 0.6f, 1060.0f, 1060.0f));

    // crater
    if (craterAlpha > 0.01f)
    {
        juce::Rectangle<float> cr (500.0f - 310.0f, 670.0f - 80.0f, 620.0f, 160.0f);
        juce::ColourGradient cg (hex (0x120c08, 0.94f * craterAlpha), cr.getCentre(), hex (0x3b2a1a, 0.0f), { cr.getRight(), cr.getCentreY() }, true);
        cg.addColour (0.45, hex (0x2a1d12, 0.75f * craterAlpha));
        cg.addColour (0.7, hex (0x3b2a1a, 0.38f * craterAlpha));
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (1.0f, cr.getHeight() / cr.getWidth(), cr.getCentreX(), cr.getCentreY()));
        g.setGradientFill (cg);
        g.fillEllipse (cr.withSizeKeepingCentre (cr.getWidth(), cr.getWidth()));
        g.restoreState();
    }

    // shadow under the feet
    if (shadowAlpha > 0.01f)
    {
        const float w = 260.0f * shadowScale, h = 50.0f;
        const juce::Point<float> c (500.0f + shadowTx, 660.0f);
        juce::ColourGradient sg (hex (0x000000, 0.53f * shadowAlpha), c, hex (0x000000, 0.0f), { c.x + w / 2, c.y }, true);
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (1.0f, h / w, c.x, c.y));
        g.setGradientFill (sg);
        g.fillEllipse (c.x - w / 2, c.y - w / 2, w, w);
        g.restoreState();
    }

    // glow behind the creeper when it blinks
    if (glowAlpha > 0.01f)
    {
        const juce::Point<float> c (500.0f, 420.0f);
        juce::ColourGradient gg (hex (0xffffff, glowAlpha), c, hex (0xffffff, 0.0f), { c.x + 350.0f, c.y }, true);
        g.setGradientFill (gg);
        g.fillEllipse (c.x - 350.0f, c.y - 350.0f, 700.0f, 700.0f);
    }

    // the creeper / the explosion
    if (frameIndex >= 0)
    {
        if (boomFrame)
        {
            const auto img = frame (boomFrames[(size_t) juce::jlimit (0, numBoom - 1, frameIndex)]);
            if (img.isValid())
                g.drawImage (img, juce::Rectangle<float> (0, 0, 1000, 1000));
        }
        else
        {
            const auto img = frame (fuseFrames[(size_t) juce::jlimit (0, numFuse - 1, frameIndex)]);
            if (img.isValid())
            {
                g.setOpacity (creeperOpacity);
                const auto place = juce::AffineTransform::scale (360.0f * kFrame / (float) img.getWidth(), 640.0f * kFrame / (float) img.getHeight())
                                       .translated (380.0f * kFrame, 160.0f * kFrame);
                g.drawImageTransformed (img, place.followedBy (creeperXf), false);
                g.setOpacity (1.0f);
            }
        }
    }

    // white flash
    if (flashAlpha > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (std::pow (flashAlpha, 1.6f)));
        g.fillRect (-60, -60, 1120, 1120);
    }

    // ---- UI -------------------------------------------------------------------------------
    drawButton (g, optionsRect, "OPTIONS", settingsOpen);
    drawSettings (g);

    // title
    {
        const auto led = state == State::boom ? hex (0xff5a2a) : white ? hex (0xffffff) : v > 0 ? hex (0x7cff4f) : hex (0xffffff, 0.33f);
        juce::Rectangle<float> tr (560.0f, 30.0f, 410.0f, 50.0f);
        juce::Font f = mcFace != nullptr ? juce::Font (juce::FontOptions (mcFace).withHeight (44.0f)) : juce::Font (juce::FontOptions (44.0f));
        const float tw = juce::GlyphArrangement::getStringWidth (f, "CREEPER");
        const float ledX = tr.getRight() - tw - 34.0f;
        g.setColour (hex (0x3f3f3f));
        g.fillRect (ledX + 3, tr.getCentreY() - 6 + 3, 14.0f, 14.0f);
        g.setColour (led);
        g.fillRect (ledX, tr.getCentreY() - 6, 14.0f, 14.0f);
        drawLabel (g, "CREEPER", tr, 44.0f, juce::Justification::centredRight, juce::Colours::white, 4);
    }

    // credit
    drawLabel (g, "@buyorbealoser", { 30.0f, 945.0f, 400.0f, 30.0f }, 20.0f, juce::Justification::centredLeft, hex (0xffffff, 0.92f), 2);

    // knob: redstone trail + TNT block + value
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (knobScale).translated (knobCentre.x, knobCentre.y));
        drawDust (g);
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians ((v - 0.5f) * 24.0f)).translated (tntJx, tntJy));
        drawTNT (g);
        float tf = 0.0f;
        if (state == State::boom) tf = (float) juce::jmax (0.0, 1.0 - (lastMs - boomT0) / 600.0);
        else if (white) tf = 0.5f;
        if (tf > 0)
        {
            g.setColour (juce::Colours::white.withAlpha (tf));
            g.fillRect (-48.0f, -48.0f, 96.0f, 96.0f);
        }
        g.restoreState();

        drawLabel (g, juce::String (juce::roundToInt (v * 100.0f)) + "%",
                   { knobCentre.x - 100.0f, knobCentre.y + 100.0f, 200.0f, 36.0f }, 30.0f, juce::Justification::centred, juce::Colours::white, 3);
    }
}

// ------------------------------------------------------------------------------------------
juce::Point<float> CreeperEditor::toLogical (juce::Point<float> p) const
{
    return p * (1000.0f / (float) juce::jmax (1, getWidth()));
}

bool CreeperEditor::overKnob (juce::Point<float> l) const
{
    return l.getDistanceFrom (knobCentre) < 100.0f;
}

void CreeperEditor::setCharge (float newValue)
{
    chargeParam->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, newValue));
    lastUserTouchMs = nowMs();
}

void CreeperEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto l = toLogical (e.position);

    if (optionsRect.contains (l)) { settingsOpen = ! settingsOpen; repaint(); return; }

    if (settingsOpen)
    {
        for (const auto& s : segs)
            if (s.r.contains (l))
            {
                if (auto* prm = proc.apvts.getParameter (s.paramId))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->convertTo0to1 (s.value));
                    prm->endChangeGesture();
                }
                repaint();
                return;
            }
        if (juce::Rectangle<float> (30.0f, 92.0f, 620.0f, 300.0f).contains (l)) return;
    }

    // right-click on the TNT knob: the host's own parameter menu (FL Studio: "Create automation clip", "Link to controller"...)
    if (e.mods.isPopupMenu())
    {
        if (overKnob (l))
            if (auto* ctx = getHostContext())
                if (auto menu = ctx->getContextMenuForParameter (chargeParam))
                {
                    const auto pos = getMouseXYRelative();
                    juce::Component::SafePointer<CreeperEditor> safe (this);
                    std::shared_ptr<juce::HostProvidedContextMenu> m (std::move (menu));
                    // show it outside the mouse callback so the host menu can take the mouse
                    juce::MessageManager::callAsync ([safe, m, pos]
                    {
                        if (safe != nullptr) m->showNativeMenu (pos);
                    });
                }
        return;
    }

    if (overKnob (l))
    {
        dragging = true;
        dragStartY = e.position.y;
        dragStartV = chargeParam->getValue();
        chargeParam->beginChangeGesture();
        lastUserTouchMs = nowMs();
    }
}

void CreeperEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float px = e.mods.isShiftDown() ? 1000.0f : 250.0f;
    setCharge (dragStartV + (dragStartY - e.position.y) / px);
}

void CreeperEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        chargeParam->endChangeGesture();
        dragging = false;
        lastUserTouchMs = nowMs();
    }
}

void CreeperEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! overKnob (toLogical (e.position))) return;
    chargeParam->beginChangeGesture();
    setCharge (0.0f);
    chargeParam->endChangeGesture();
}

void CreeperEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! overKnob (toLogical (e.position))) return;
    chargeParam->beginChangeGesture();
    setCharge (chargeParam->getValue() + w.deltaY * 0.15f);
    chargeParam->endChangeGesture();
}
